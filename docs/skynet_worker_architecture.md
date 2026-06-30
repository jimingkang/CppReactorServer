# Skynet-Style Worker Architecture

## Overview

The worker server is no longer a pure "message queue + callback" dispatcher.
It is now a hybrid model:

- fd ownership and all real socket I/O stay in one socket loop
- each logical service still owns a mailbox queue
- workers schedule ready services from a global queue
- several services execute as long-lived coroutines on top of those queues
- request/response style service calls use `requestId` and `replyTo`

That gives the code Skynet-style message passing without forcing every workflow
into deeply nested callbacks.

```text
socket loop
  -> SocketMessage
  -> Gate mailbox
  -> global ready queue
  -> worker thread
  -> service mailbox dispatch
  -> service coroutine resumes
  -> service sends SkynetMessage / SocketCommand
  -> socket loop flushes send/close
```

## Dispatch Redesign: Queue + Coroutine

### Before

The original worker model treated each service as a queue plus a callback:

```text
fd event
  -> queue one message
  -> worker pops one message
  -> handleXxxService(message)
  -> maybe enqueue more messages
```

That works for fire-and-forget flows, but it becomes awkward when a service
needs to call another service and wait for a reply, such as:

- login -> db -> login -> session
- hall -> redis -> hall -> session
- session -> hall -> session -> world -> session

### Now

The current design keeps the mailbox queue, but services can run as resumable
coroutines:

```text
ServiceContext
  -> queue + scheduled flag

CoroutineServiceContext
  -> queue consumer coroutine
  -> co_await nextMessage()

RequestReplyServiceContext
  -> queue consumer coroutine
  -> co_await nextMessage()
  -> co_await callService(...)
  -> resume when replyTo matches requestId
```

This is the core redesign:

1. `ServiceContext` still provides the per-service queue and ready/scheduled
   behavior.
2. Workers still batch-pop messages from ready services.
3. `dispatch()` no longer has to finish the whole logical operation in one
   synchronous callback.
4. A service coroutine can suspend on:
   - `nextMessage()`
   - `callService(request)`
5. When the reply arrives, `RequestReplyServiceContext` matches
   `message.replyTo == requestId` and resumes the suspended coroutine.

## Runtime Topology

```text
                 +----------------------------+
                 |        socket loop         |
                 | epoll/kqueue + all fd I/O  |
                 +-------------+--------------+
                               |
                               v
                 +----------------------------+
                 |       SocketMessage        |
                 | Accept/Data/Close/Error    |
                 +-------------+--------------+
                               |
                               v
                 +----------------------------+
                 |       SkynetMessage        |
                 | source/destination/kind    |
                 | requestId/replyTo/payload  |
                 +-------------+--------------+
                               |
                               v
            +----------------------------------------+
            |       global queue of ready services   |
            +----+-----------+------------+----------+
                 |           |            |
                 v           v            v
          +----------+ +------------+ +-------------+
          | Gate     | | Connection | | GameWorld   |
          +----------+ +------------+ +-------------+
          | Logger   | | Room       | | Login       |
          +----------+ +------------+ +-------------+
          | Db       | | Hall       | | Redis       |
          +----------+ +------------+ +-------------+
                 |           |            |
                 +-----------+------------+
                             |
                             v
              +----------+ +----------+ +----------+
              | worker 0 | | worker 1 | | worker N |
              +----------+ +----------+ +----------+
                             |
                             v
                 +----------------------------+
                 |      SocketCommand         |
                 |        Send / Close        |
                 +-------------+--------------+
                               |
                               v
                 +----------------------------+
                 | socket loop flushes output |
                 +----------------------------+
```

## Service Types

### Plain queue service

These services do not maintain their own coroutine main loop:

- `GateServiceContext`
- `ConnectionServiceContext`

They dispatch directly into `WorkerGameServer::handleGateService()` and
`WorkerGameServer::handleConnectionService()`.

### Coroutine mailbox service

These services consume messages with a long-lived mailbox coroutine:

- `LoggerServiceContext`
- `RoomServiceContext`
- `DbServiceContext`
- `RedisServiceContext`

They use `co_await nextMessage()` and process one message at a time in mailbox
order.

### Request/reply coroutine service

These services consume messages with a long-lived coroutine and can also await a
reply from another service:

- `SessionServiceContext`
- `GameWorldServiceContext`
- `HallServiceContext`
- `LoginServiceContext`

They use:

- `co_await nextMessage()` for mailbox input
- `co_await callService(request)` for nested service calls

## Worker Scheduling Rules

The scheduler is still queue-based.

- every service has one private mailbox queue
- `ServiceContext::push()` marks the service as scheduled only once
- the global queue stores `ServiceContext*`, not individual messages
- a worker pops up to 64 messages from one service in one batch
- `finishBatch()` decides whether the service needs to be requeued

This means the redesign changed service execution style, not queue ownership or
fd ownership.

## Socket Ownership

`WorkerGameServer` keeps exclusive ownership of:

- listen fd
- client fds
- output buffers
- close timing
- epoll/kqueue registration

Workers and service coroutines never call `send()` or `close()` directly.
Instead they emit:

- `SocketCommandType::Send`
- `SocketCommandType::Close`

The socket loop drains those commands and flushes output in fd order.

## Session Flow

`SessionServiceContext` is now the center of per-connection control flow.

It wraps one `ISessionAgent` and converts agent output into either:

- immediate socket commands
- fire-and-forget service messages
- exactly one blocking service call at a time

The blocking call extraction is intentional. `processSessionActions()` scans the
agent output and turns the first of these into a coroutine `callService()`:

- login request
- hall request
- world join / leave / quit request

Everything else is forwarded immediately. When the reply arrives, the same
session coroutine resumes and feeds the response back into the agent.

That gives each session a linear control flow while still using shared worker
threads.

## Service Flows

### 1. Accept / data / close

```text
socket loop
  -> SocketMessage
  -> Gate
  -> Connection
  -> SessionServiceContext(fd)
  -> ISessionAgent
  -> SocketCommand / ServiceCall / ServiceMessage
```

`ConnectionServiceContext` owns the fd -> `SessionServiceContext` lookup.
Accept creates a session context. Data, close, login result, hall result, and
game responses are routed back to that session.

### 2. Login -> db -> session

```text
SessionServiceContext
  -> LoginServiceContext
  -> DbServiceContext
  -> LoginServiceContext
  -> SessionServiceContext
  -> SocketCommand
```

Detailed behavior:

1. The session agent emits `LoginMessageType::Request`.
2. `SessionServiceContext` turns it into `co_await callService(loginRequest)`.
3. `LoginServiceContext` receives the request and issues a nested
   `DbMessageType::CheckCredentials`.
4. `DbServiceContext` checks `userCredentials_` and returns
   `DbMessageType::CredentialsResult`.
5. `LoginServiceContext` maps that into `LoginMessageType::Result`, logs the
   outcome, and replies to `Connection`.
6. The suspended session coroutine resumes and delivers the login result back to
   the agent.

### 3. Hall / auto-match / redis / session

```text
SessionServiceContext
  -> HallServiceContext
  -> RedisServiceContext
  -> HallServiceContext
  -> SessionServiceContext
  -> GameWorldServiceContext (Join)
```

`HallServiceContext` now owns:

- room registry
- auto-match queues keyed by `(gameType, seatCount)`
- room id allocation

For hall operations:

- `ListRooms`: returns an in-memory snapshot
- `CreateRoom`: updates in-memory rooms, writes room and hall index to Redis,
  logs creation
- `JoinRoom`: updates room membership, writes Redis mirrors, logs join
- `LeaveRoom`: updates membership or removes room, writes Redis mirrors, logs leave
- `AutoMatch`: enqueues a pending match request, updates Redis queue size, then
  tries to build a room
- `CancelMatch`: removes the pending request and updates Redis queue size

When auto-match reaches `seatCount`, `HallServiceContext`:

1. builds a room
2. writes room/index/queue counters to Redis
3. sends a `HallMessageType::Result` reply to each waiting session
4. each resumed session then sends `GameCommandType::Join` to `GameWorld`

### 4. GameWorld -> room/db/connection

```text
SessionServiceContext
  -> GameWorldServiceContext
  -> IGameWorld
  -> RoomServiceContext / DbServiceContext / Connection
  -> SessionServiceContext
```

`GameWorldServiceContext` is mostly an async shell over
`WorkerGameServer::handleGameWorldService()`.

Current behavior:

- `Join`:
  - calls `world_->join`
  - emits `RoomMessageType::PlayerJoined`
  - replies with `GameResponseType::Joined`
- `Leave`:
  - calls `world_->leave`
  - emits `RoomMessageType::PlayerLeft`
  - emits `DbMessageType::SavePlayer`
  - replies with `GameResponse`
- `Command`:
  - calls `world_->handleCommand`
  - if it becomes a leave/quit path, also emits room/db side effects
  - replies with `GameResponse`
- after each command it also drains `world_->takePendingResponses()` and pushes
  broadcast or delayed responses back to `Connection`

### 5. Logger flow

```text
any service
  -> WorkerGameServer::logText(...)
  -> LoggerServiceContext
  -> stdout
```

Logging is intentionally decoupled from the calling service queue, so logging
latency does not block hall/login/session progression.

### 6. Redis flow

```text
HallServiceContext
  -> RedisServiceContext
  -> in-memory key/value store
  -> HallServiceContext
```

`RedisServiceContext` is currently an in-process fake Redis:

- `Get`
- `Set`
- `Delete`
- `KeysByPrefix`

The main reason it exists as a service instead of a direct map is to preserve
the request/reply shape that a real external Redis integration would need.

## Tick Thread

`WorkerGameServer` also starts a fixed-step tick thread:

```text
100 ms interval
  -> world_->tick(dtMs)
```

That thread is separate from mailbox scheduling. It is used for authoritative
simulation updates inside the game world.

## Comparison With The Coroutine Reactor

```text
Coroutine reactor:
fd event -> coroutine_handle.resume()

Worker server:
fd event -> service queue -> worker -> service coroutine resume
```

The important difference is where suspension happens:

- reactor server suspends directly on fd readiness
- worker server suspends on service mailbox input and service replies

## Current Service Inventory

- `Logger`
- `Gate`
- `Connection`
- `GameWorld`
- `Room`
- `Login`
- `Db`
- `Hall`
- `Redis`

## Build And Smoke Test

Configure the current workspace build directory:

```bash
cmake -S . -B build -DBUILD_IMGUI_CLIENT=ON
```

Build the guess-number worker server and client:

```bash
cmake --build build --target guess_number_server_workers
cmake --build build --target guess_number_client
```

Start the guess-number worker server:

```bash
./build/guess_number_server_workers 127.0.0.1 7799 1
```

Expected startup log:

```text
worker_game_server listening on 127.0.0.1:7799 workers=1 services=logger,gate,connection,gameworld,room,login,db,hall,redis
```

Run the ImGui client in another terminal:

```bash
./build/guess_number_client
```

At the moment the repository does not register automated `ctest` cases for the
worker server:

```bash
ctest --test-dir build -N
```

Expected result:

```text
Total Tests: 0
```

So the practical verification path today is:

1. build `guess_number_server_workers`
2. build `guess_number_client`
3. start the server and confirm it binds and stays up
4. connect one or more clients and verify hall match -> room join -> gameplay

## Notes For Extending

When adding a new service, pick the base class by control-flow needs:

- simple queue + synchronous dispatch: `ServiceContext`
- sequential mailbox consumer: `CoroutineServiceContext`
- mailbox consumer that must await other services:
  `RequestReplyServiceContext`

If the new service needs nested async flows, do not add more callback plumbing
to `WorkerGameServer`; put the logic in the service coroutine and use
`callService()`.
