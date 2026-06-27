# serverStudy Linux TCP Server Architecture

本文档解释 `serverStudy` 下 Linux 服务端如何管理 TCP 连接、如何包装 fd 事件、如何把网络包交给业务层，以及这个设计在继续重构时需要注意的问题。

## 入口

服务端入口在 `myserver/src/GameManager.cpp`。

```text
StartApp()
  -> entry::Init()
  -> app::Init()
      -> tcp::NewIServer()
      -> SetNotify_Connect()
      -> SetNotify_Secure()
      -> SetNotify_DisConnect()
      -> SetNotify_Command()
      -> StartServer()
  -> while true
      -> app::Update()
```

`app::Update()` 每 2ms 调一次：

```text
__IServer->Update()
__Player->Update()
```

因此这个系统不是纯事件驱动业务模型。fd 可读事件由 epoll 线程发现，但协议解包、心跳检查、发送缓冲刷新和业务命令分发是在主循环 `LinuxServer::Update()` 中完成的。

## 核心对象

### IServer

`core/ITcp.h` 定义网络层对业务层暴露的接口：

```cpp
class IServer {
public:
    virtual void StartServer() = 0;
    virtual void StopServer() = 0;
    virtual void Update() = 0;

    virtual S_CONNECT_BASE* FindClient(const int socketfd, bool issecure) = 0;
    virtual S_CONNECT_BASE* FindClient(const int index) = 0;

    virtual void CreatePackage(const int index, const uint16_t cmd, void* v, const int len) = 0;
    virtual void ReadPackage(const int index, void* v, const int len) = 0;

    virtual void SetNotify_Connect(ISERVER_NOTIFY e) = 0;
    virtual void SetNotify_Secure(ISERVER_NOTIFY e) = 0;
    virtual void SetNotify_DisConnect(ISERVER_NOTIFY e) = 0;
    virtual void SetNotify_Command(ISERVER_NOTIFY e) = 0;
};
```

业务层不直接调用 socket API，而是通过：

- `CreatePackage()` 写发送缓冲。
- `ReadPackage()` 从当前包体中读取结构体。
- `FindClient()` 根据 fd 或连接池 index 找连接。
- `SetNotify_*()` 注册网络事件和业务命令回调。

### S_CONNECT_BASE

`S_CONNECT_BASE` 是单个 TCP 连接的包装对象：

```cpp
struct S_CONNECT_BASE {
    int index;
    int socketfd;
    int8_t state;
    int8_t closeState;
    char ip[MAX_IP_LEN];
    uint16_t port;
    uint8_t xorCode;
    int32_t appID;

    S_BUFFS_BASE recvs;
    S_BUFFS_BASE sends;
    int packageLength;

    int32_t temp_ConnectTime;
    int32_t temp_HeartTime;
    int32_t temp_CloseTime;
    int32_t temp_ShutDown;
};
```

它把 fd、连接状态、协议加密 xor、收发缓冲、心跳时间、关闭原因放在一个对象中。网络层和业务层都围绕这个对象工作。

### 连接池和 fd 索引

`LinuxServer::StartServer()` 启动时预分配两个数组：

```text
m_Onlines       : HashContainer<S_CONNECT_BASE>
m_OnlinesIndexs : HashContainer<S_CONNECT_INDEX>
```

含义：

```text
socket fd -> m_OnlinesIndexs[fd].index -> m_Onlines[index] -> S_CONNECT_BASE
```

这样 epoll 返回 fd 后，可以 O(1) 找到连接对象。

连接对象不是每次 `accept()` 动态创建，而是从 `m_Onlines` 连接池中找 `E_SSS_Free` 的槽位。断线后业务层调用 `Reset()` 释放槽位供下一个连接复用。

## 线程模型

Linux 版本启动三个后台线程：

```text
Thread_Manager
  epoll_wait()
  listenfd readable -> notify Thread_Accept
  clientfd readable -> push fd into m_SocketfdArr -> notify Thread_Recv

Thread_Accept
  wait condition
  Event_Accept()

Thread_Recv
  wait condition
  pop fd from m_SocketfdArr
  Event_Recv(fd)
```

再加上业务主线程：

```text
main thread
  GameManager::Update()
    LinuxServer::Update()
      scan all connection slots
      UpdateDisconnect()
      ReadPackage_Head()
      Event_Send()
    GamePlayer::Update()
```

所以它是：

```text
epoll event thread + accept thread + recv thread + main update thread
```

不是每个连接一个线程，也不是完全的 Reactor 单线程模型。

## 连接生命周期

### 1. Server start

`LinuxServer::StartServer()` 做四件事：

```text
1. 分配 temp_Buf
2. 初始化连接池 m_Onlines
3. 初始化 fd 映射 m_OnlinesIndexs
4. InitSocket()
5. InitThread()
```

`InitSocket()`：

```text
socket(AF_INET, SOCK_STREAM, 0)
SetNonblock(listenfd)
setsockopt(SO_REUSEADDR)
bind(INADDR_ANY, appPort)
listen(SOMAXCONN)
epoll_create()
epoll_ctl(ADD, listenfd, EPOLLIN)
```

### 2. Accept

`Thread_Manager` 在 `epoll_wait()` 中发现 `listenfd` 可读后，只通知 accept 线程。

`Thread_Accept` 调用 `Event_Accept()`：

```text
accept()
FindNoStateData() 找一个空闲 S_CONNECT_BASE
FindOnlinesIndex(socketfd) 找 fd 映射槽
cindex->index = c->index
c->socketfd = socketfd
SetNonblock(socketfd)
epoll_ctl(ADD, socketfd, EPOLLIN | EPOLLET)
记录 ip/port/connect time/heart time
state = E_SSS_Connect
CreatePackage(CMD_XOR)
notify accept callback
```

`CMD_XOR` 是连接建立后的第一步握手。服务端生成随机 xor key 发给客户端，之后协议头用这个 key 做 XOR。

### 3. Recv

`Thread_Manager` 在 `epoll_wait()` 中发现 client fd 可读后，把 fd 放入 `m_SocketfdArr`。

`Thread_Recv` 取出 fd 后调用 `Event_Recv(fd)`：

```text
FindClient(fd, true)
while true:
    recv(fd, temp_Buf)
    EINTR  -> continue
    EAGAIN -> break
    0      -> shutdown
    error  -> shutdown
    append temp_Buf to c->recvs.buf
c->recvs.isCompleted = true
```

因为 epoll 使用 `EPOLLET` 边缘触发，所以 `Event_Recv()` 必须一直读到 `EAGAIN`。

### 4. Decode package

`LinuxServer::Update()` 扫描连接池，对每个有效连接调用 `ReadPackage_Head(c)`。

包头固定 8 字节：

```text
0..1 : Head, 2 bytes
2..5 : length, uint32
6..7 : cmd, uint16
8..  : body
```

包头字段会和当前连接的 `xorCode` 做 XOR。

解包逻辑：

```text
if recv buffer < 8 bytes:
    wait next update

decode Head
if Head invalid:
    shutdown

decode length and cmd
if full package not received:
    wait next update

c->packageLength = length
ReadPackage_Command(c, cmd)
c->recvs.head += length
```

一次 `ReadPackage_Head()` 最多处理 1000 个包，防止单个连接长时间占住主循环。

### 5. Dispatch command

`ReadPackage_Command(c, cmd)`：

```text
c->temp_HeartTime = now

if cmd < 65000:
    m_Notify_Command(this, c, cmd)
    return

switch cmd:
    CMD_HEART:
        CreatePackage(CMD_HEART)

    CMD_SECURITY:
        validate appVersion
        validate MD5(SecureCode_xorCode)
        c->appID = secure.appID
        c->state = E_SSS_Secure
        CreatePackage(CMD_SECURITY result)
        notify secure callback
```

业务命令都小于 `65000`，系统命令从 `65000` 开始。

### 6. Business layer

业务回调在 `myserver/src/GameCommand.cpp`：

```text
Event_Server_Command(tcp, c, cmd)
  -> check secure state
  -> switch cmd
      CMD_LOGIN      -> __Player->ServerCommand()
      CMD_MOVE       -> __Player->ServerCommand()
      CMD_PLAYERDATA -> __Player->ServerCommand()
      9000           -> __Test->ServerCommand()
```

`GamePlayer` 维护业务在线玩家：

```text
__Onlines: memid -> S_PLAYER_BASE*
__PlayersPool: reusable S_PLAYER_BASE pool
```

登录时：

```text
c->state = E_SSS_Login
player->socketfd = c->socketfd
__Onlines[memid] = player
CreatePackage(CMD_LOGIN)
```

移动时：

```text
ReadPackage(CMD_MOVE body)
update player position
broadcast CMD_MOVE to other online players
```

### 7. Send

业务层调用 `CreatePackage(index, cmd, body, len)` 不会立刻阻塞等待发送完成，而是把编码后的包写入 `c->sends.buf`。

`LinuxServer::Update()` 每帧调用 `Event_Send(c)`：

```text
if c->sends.tail > c->sends.head:
    send(fd, sends.buf + head, tail - head)
    advance head
```

Linux 版本没有注册 `EPOLLOUT`，发送是主循环扫描触发的。

### 8. Disconnect

断线有两步：

```text
ShutDownSocket()
  -> 标记 closeState = E_SSC_ShutDown
  -> 记录关闭原因和关闭时间
  -> shutdown(fd)

UpdateDisconnect()
  -> 如果收发都完成，ReleaseSocket()
  -> 如果超过 1 秒仍未完成，ReleaseSocket()
```

`ReleaseSocket()`：

```text
decrease count
epoll_ctl(DEL)
shutdown()
close()
notify disconnect callback
```

业务断线回调：

```text
if state is Connect/Secure:
    c->Reset()
else if state is Login:
    c->state = E_SSS_NeedSave
```

`GamePlayer::Update()` 再发现 `E_SSS_NeedSave`，广播离线，回收 player，最后 `c->Reset()`。

## 状态机

连接状态：

```text
E_SSS_Free
  -> E_SSS_Connect     accept 后
  -> E_SSS_Secure      CMD_SECURITY 验证成功
  -> E_SSS_Login       CMD_LOGIN 成功
  -> E_SSS_NeedSave    登录玩家断线后等待业务保存
  -> E_SSS_Free        业务保存完成后 Reset()
```

关闭状态：

```text
E_SSC_Free
  -> E_SSC_ShutDown
  -> ReleaseSocket()
```

## 架构图

```text
                               +----------------------+
                               |      listenfd        |
                               +----------+-----------+
                                          |
                                          v
                               +----------------------+
                               |   Thread_Manager     |
                               |   epoll_wait()       |
                               +----+------------+----+
                                    |            |
                  listen readable   |            | client readable
                                    v            v
                          +--------------+   +----------------+
                          | Thread_Accept|   | m_SocketfdArr  |
                          +------+-------+   +--------+-------+
                                 |                    |
                                 v                    v
                          +--------------+   +----------------+
                          | Event_Accept |   |  Thread_Recv   |
                          +------+-------+   +--------+-------+
                                 |                    |
                                 v                    v
          +-------------------------------+   +----------------+
          | m_Onlines connection pool     |<--| Event_Recv(fd) |
          | S_CONNECT_BASE[]              |   +----------------+
          +---------------+---------------+
                          |
                          v
               +----------------------+
               | GameManager::Update  |
               +----------+-----------+
                          |
                          v
               +----------------------+
               | LinuxServer::Update  |
               | scan connection pool |
               +----------+-----------+
                          |
             +------------+------------+
             |                         |
             v                         v
    +------------------+      +------------------+
    | ReadPackage_Head |      |   Event_Send     |
    +--------+---------+      +------------------+
             |
             v
    +---------------------+
    | ReadPackage_Command |
    +----------+----------+
               |
               v
    +----------------------+
    | Event_Server_Command |
    +----------+-----------+
               |
               v
    +----------------------+
    | GamePlayer/GameTest  |
    +----------------------+
```

## 当前设计的主要问题

### 1. Update 扫描整个连接池

`LinuxServer::Update()` 每帧遍历 `m_Onlines->Count()`。如果 `appMaxConnect` 很大，但活跃连接很少，会浪费 CPU。

更好的方式：

- 维护 active connection queue。
- 只有收包、待发送、待关闭的连接进入 update list。
- 或者改成 message queue，业务 worker 消费已经解好的消息。

### 2. recv 线程只有一个

所有 client readable fd 都进入同一个 `m_SocketfdArr`，由一个 `Thread_Recv` 处理。连接数和包量上来后，这个线程会成为瓶颈。

### 3. send 不使用 EPOLLOUT

发送缓冲由 `Update()` 主循环扫描触发。如果很多连接有待发送数据，会变成全池扫描。

更好的方式：

- 发送缓冲非空时注册 writable event。
- 写完后取消 writable event。

### 4. fd 映射释放不彻底

`ReleaseSocket()` 关闭 fd 后，原代码没有直接清空：

```text
m_OnlinesIndexs[socketfd].index
```

它依赖 `S_CONNECT_BASE::Reset()` 和 `socketfd` 校验规避误用。更稳妥的做法是在释放 fd 时立即 reset fd index。

### 5. 多线程共享连接对象缺少完整同步

recv 线程写 `c->recvs`，主线程读 `c->recvs`。send/state 也可能被不同线程访问。现有代码只有少量锁保护队列和计数，没有保护每个连接对象的收发缓冲和状态。

如果继续保留多线程，需要为每个连接加细粒度锁，或改成网络线程只产出 message，业务线程只消费 message。

### 6. close 和业务保存耦合

登录玩家断线后，网络层释放 fd，业务层再把连接状态置为 `E_SSS_NeedSave`。这个模型可以工作，但连接对象和玩家对象生命周期耦合较重。

更清晰的做法：

```text
connection closed event
  -> socket_message
  -> skynet_message
  -> PlayerService queue
  -> PlayerService 保存玩家并释放业务状态
```

## 与后续 kqueue 重构的关系

macOS 没有 epoll，等价事件通知机制是 `kqueue`。

从 epoll 到 kqueue，不应该改变业务协议和 `IServer` 使用方式。建议先保持这些不变：

- `S_CONNECT_BASE` 连接对象。
- `m_Onlines` 连接池。
- `m_OnlinesIndexs` fd 到连接池 index 映射。
- 8 字节协议头。
- `CreatePackage()` 和 `ReadPackage()`。
- `SetNotify_*()` 业务回调。

替换的只是事件后端：

```text
epoll_create / epoll_ctl / epoll_wait
```

替换成：

```text
kqueue / EV_SET / kevent
```

建议的 kqueue 版先放在 `serverStudy/review`，作为重构草案，不直接覆盖原 Linux 代码。

