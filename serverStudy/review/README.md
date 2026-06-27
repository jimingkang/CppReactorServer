# serverStudy review: macOS kqueue server draft

本目录放的是 `serverStudy/core/LinuxServer.*` 的 macOS kqueue 重构草案，不直接覆盖原代码。

## 文件

- `KqueueServer.h`
- `KqueueServer.cpp`

## 目标

这版代码只替换事件通知后端：

```text
Linux epoll
  epoll_create()
  epoll_ctl()
  epoll_wait()

macOS kqueue
  kqueue()
  EV_SET()
  kevent()
```

保留原系统已有设计：

- `IServer` 接口。
- `S_CONNECT_BASE` 连接对象。
- `HashContainer<S_CONNECT_BASE>` 连接池。
- `HashContainer<S_CONNECT_INDEX>` fd 到连接池 index 映射。
- 8 字节包头：`Head + length + cmd`。
- `CreatePackage()` / `ReadPackage()`。
- `SetNotify_Connect/Secure/Disconnect/Command()` 业务回调。
- `CMD_XOR`、`CMD_SECURITY`、`CMD_HEART` 系统命令。

## 与 LinuxServer 的区别

### 1. kqueue 替代 epoll

原 Linux 版本：

```text
Thread_Manager -> epoll_wait()
Thread_Accept  -> condition wake -> accept()
Thread_Recv    -> condition wake -> recv()
```

kqueue 草案：

```text
eventThread_ -> kevent()
  listenFd_ readable -> AcceptAll()
  client fd readable -> RecvAll(fd)
```

也就是先把事件线程收敛成一个。这样代码更容易 review 和验证。后续如果压力测试证明 recv 成为瓶颈，再把 `RecvAll(fd)` 产出的数据转换成消息队列，交给 worker pool。

### 2. accept 会读到 EAGAIN

监听 fd 是 non-blocking。kqueue 触发 listen readable 后，`AcceptAll()` 会循环 `accept()`，直到 `EAGAIN/EWOULDBLOCK`。

这和 epoll ET 模式里的原则一致：一次事件要尽量 drain。

### 3. 释放 fd 时清空 fd index

原 Linux 版本 `ReleaseSocket()` 关闭 fd 后，没有直接 reset：

```text
m_OnlinesIndexs[socketfd]
```

kqueue 草案在 `ReleaseSocket()` 中增加：

```cpp
S_CONNECT_INDEX* fdSlot = FindFdIndex(socketfd);
if (fdSlot != nullptr) {
    fdSlot->Reset();
}
```

这样 fd 被系统复用时更安全。

### 4. 修正发送缓冲检查条件

原代码里 `CreatePackage()` 的判断存在优先级风险：

```cpp
c->state <= 0 ||
c->socketfd <= 0 &&
c->sends.tail + 8 + len > ServerXML->sendBytesMax
```

kqueue 草案改为明确的三段：

```cpp
c->state <= 0 ||
c->socketfd <= 0 ||
c->sends.tail + 8 + len > ServerXML->sendBytesMax
```

## 接入方式

如果要让业务层使用 kqueue 版本，有两种方式。

### 方式一：显式使用 NewKqueueServer

在 `GameManager.cpp` 中：

```cpp
#include "../../review/KqueueServer.h"

__IServer = tcp::NewKqueueServer();
```

### 方式二：替换 NewIServer

把构建里的 `LinuxServer.cpp` 替换为 `review/KqueueServer.cpp`，并让：

```cpp
tcp::NewIServer()
```

返回 `new tcp::KqueueServer()`。

这种方式会影响原 Linux 构建，不建议第一步就做。

## 后续建议

当前 kqueue 草案仍然保持了旧的主循环扫描连接池模型：

```text
GameManager::Update()
  -> KqueueServer::Update()
      -> scan all connections
      -> decode packages
      -> dispatch command callback
      -> send pending bytes
```

下一步更像 Skynet 的改造应该是：

```text
kqueue fd event
  -> socket_message
  -> skynet_message
  -> service queue
  -> worker thread
  -> service callback
```

也就是说，kqueue 只是替换 `epoll`，不是最终架构。最终目标应该减少全连接池扫描，并让业务消息进入 service queue，由 worker 消费。

