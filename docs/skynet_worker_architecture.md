# Skynet-Style Worker Architecture

```text
                 +----------------------------+
                 |        socket loop         |
                 | epoll/kqueue + all fd I/O  |
                 +-------------+--------------+
                               |
                               | fd event
                               v
                 +----------------------------+
                 |       SocketMessage        |
                 | Accept/Data/Close/Error    |
                 +-------------+--------------+
                               |
                               | wrap
                               v
                 +----------------------------+
                 |       SkynetMessage        |
                 | source/session/type/payload|
                 +-------------+--------------+
                               |
                               | push to GateService
                               v
            +----------------------------------------+
            |              global queue              |
            | elements are ready service queues      |
            +----+-----------+------------+----------+
                 |           |            |
                 v           v            v
          +----------+ +------------+ +-------------+
          | Gate q   | | Conn q     | | GameWorld q |
          +----------+ +------------+ +-------------+
          +----------+ +------------+ +-------------+
          | Logger q | | Room q     | | Db q        |
          +----------+ +------------+ +-------------+
                 |           |            |
                 +-----------+------------+
                             |
                             v
              +----------+ +----------+ +----------+
              | worker 0 | | worker 1 | | worker N |
              +----------+ +----------+ +----------+
                             |
                             | service callbacks send messages
                             v
          +------------------------------------------------+
          | Gate -> Connection -> GameWorld -> Connection  |
          | Room / Db / Logger have independent queues     |
          +--------------------+---------------------------+
                               |
                               | SocketCommand Send/Close
                               v
                 +----------------------------+
                 | socket command queue       |
                 +-------------+--------------+
                               |
                               | wake pipe
                               v
                 +----------------------------+
                 | socket loop flushes output |
                 +----------------------------+
```

This keeps the socket loop as the only owner of socket fds. Workers never call
`send` or `close` directly; they return `SocketCommand` objects to the socket
loop. That preserves output ordering per fd and avoids multiple worker threads
mutating the same socket state.

The implementation is intentionally small:

- `SocketMessage` is the local equivalent of Skynet's `socket_message`.
- `SkynetMessage` is the generic message envelope pushed into a service queue.
- `ServiceQueue` is a per-service mailbox.
- The global queue stores ready `ServiceQueue*` entries, not individual messages.
- `WorkerGameServer` owns the socket loop, worker pool, services, and command
  queue back to the socket loop.

Services:

- `GateService`: receives socket events and forwards them to connection logic.
- `ConnectionService`: owns `fd -> player/session` state and line parsing.
- `GameWorldService`: owns `GameWorld` and applies game commands.
- `RoomService`: placeholder queue for future room/map sharding.
- `DbService`: placeholder queue for async persistence.
- `LoggerService`: independent queue for async logs.

Compared with the coroutine reactor:

```text
Coroutine reactor:
fd event -> coroutine_handle.resume()

Worker server:
fd event -> SocketMessage -> SkynetMessage -> Gate queue -> global queue -> worker
```

## Adding Games

Server-side game logic is split behind `IGameService`:

```text
IGameService
  -> PlatformGameWorld
  -> TicTacToeWorld
  -> FuturePokerWorld
  -> FutureMahjongWorld
```

`GameWorld` is now a router/facade. It owns concrete game services, forwards
`join`/`leave` to every game service, and routes command lines by command name.
To add a new game:

```text
1. Implement IGameService.
2. Add the instance to GameWorld::games_.
3. Make canHandle(command) return true for that game's protocol commands.
4. Add a client view that parses the new server lines and renders controls.
```

Client-side views follow the same shape:

```text
IGameClientView
  -> SuperMarioNetGame

IPanelClientView
  -> TicTacToeClientView
```

The main client owns networking and delegates rendering/commands to views, so a
new game client should avoid changing socket code.

## Pressure Test

Build the pressure client:

```bash
cmake --build cmake-build-debug --target stress_client
```

Start the worker server:

```bash
./cmake-build-debug/game_server_workers 127.0.0.1 7779 4
```

Run the default million-request test:

```bash
./cmake-build-debug/stress_client 127.0.0.1 7779 1000000 256
```

Arguments:

```text
stress_client [host] [port] [total_requests] [concurrency] [command] [expected_prefix]
```

The default command is `PING`, and the expected response prefix is `PONG`.
