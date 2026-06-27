#include "KqueueServer.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace common;

namespace tcp
{
    IServer* NewKqueueServer()
    {
        return new KqueueServer();
    }

    KqueueServer::KqueueServer()
        : onlines_(nullptr),
          fdIndex_(nullptr),
          notifyAccept_(nullptr),
          notifySecure_(nullptr),
          notifyDisconnect_(nullptr),
          notifyCommand_(nullptr),
          connectCount_(0),
          secureCount_(0),
          running_(false),
          listenFd_(-1),
          kqueueFd_(-1)
    {
    }

    KqueueServer::~KqueueServer()
    {
        StopServer();
    }

    int KqueueServer::ConnnectCount()
    {
        std::lock_guard<std::mutex> guard(connectCountMutex_);
        return connectCount_;
    }

    int KqueueServer::SecureCount()
    {
        std::lock_guard<std::mutex> guard(secureCountMutex_);
        return secureCount_;
    }

    void KqueueServer::StartServer()
    {
        InitConnections();
        if (InitSocket() != 0)
        {
            return;
        }

        running_ = true;
        eventThread_ = std::thread(&KqueueServer::EventLoop, this);
    }

    void KqueueServer::StopServer()
    {
        const bool wasRunning = running_.exchange(false);
        if (!wasRunning && listenFd_ < 0 && kqueueFd_ < 0)
        {
            return;
        }

        if (kqueueFd_ >= 0)
        {
            close(kqueueFd_);
            kqueueFd_ = -1;
        }

        if (listenFd_ >= 0)
        {
            shutdown(listenFd_, SHUT_RDWR);
            close(listenFd_);
            listenFd_ = -1;
        }

        if (eventThread_.joinable())
        {
            eventThread_.join();
        }
    }

    void KqueueServer::InitConnections()
    {
        tempBuffer_.assign(ServerXML->recvBytesOne, 0);

        onlines_ = new HashContainer<S_CONNECT_BASE>(ServerXML->appMaxConnect);
        for (int i = 0; i < onlines_->Count(); ++i)
        {
            onlines_->Value(i)->Init();
        }

        fdIndex_ = new HashContainer<S_CONNECT_INDEX>(MAX_SOCKETFD_LEN);
        for (int i = 0; i < MAX_SOCKETFD_LEN; ++i)
        {
            fdIndex_->Value(i)->Reset();
        }
    }

    bool KqueueServer::SetNonblock(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
        {
            return false;
        }
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    int KqueueServer::InitSocket()
    {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0)
        {
            perror("socket");
            return -1;
        }

        SetNonblock(listenFd_);

        int reuse = 1;
        if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
        {
            perror("setsockopt SO_REUSEADDR");
            return -1;
        }

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ServerXML->appPort);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            perror("bind");
            return -1;
        }

        if (listen(listenFd_, SOMAXCONN) != 0)
        {
            perror("listen");
            return -1;
        }

        kqueueFd_ = kqueue();
        if (kqueueFd_ < 0)
        {
            perror("kqueue");
            return -1;
        }

        if (!AddReadEvent(listenFd_))
        {
            perror("kevent listen add");
            return -1;
        }

        return 0;
    }

    bool KqueueServer::AddReadEvent(int fd)
    {
        struct kevent change;
        EV_SET(&change, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        return kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr) == 0;
    }

    void KqueueServer::DeleteReadEvent(int fd)
    {
        if (kqueueFd_ < 0 || fd < 0)
        {
            return;
        }

        struct kevent change;
        EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr);
    }

    void KqueueServer::EventLoop()
    {
        struct kevent events[1024];

        while (running_)
        {
            const int count = kevent(kqueueFd_, nullptr, 0, events, 1024, nullptr);
            if (count < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                if (!running_)
                {
                    break;
                }
                perror("kevent wait");
                break;
            }

            for (int i = 0; i < count; ++i)
            {
                const int fd = static_cast<int>(events[i].ident);

                if (events[i].flags & EV_ERROR)
                {
                    if (fd != listenFd_)
                    {
                        ShutDownSocket(fd, nullptr, 9001);
                    }
                    continue;
                }

                if (fd == listenFd_)
                {
                    AcceptAll();
                }
                else if (events[i].filter == EVFILT_READ)
                {
                    RecvAll(fd);
                }
            }
        }
    }

    void KqueueServer::AcceptAll()
    {
        while (running_)
        {
            AcceptOne();
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                errno = 0;
                break;
            }
        }
    }

    void KqueueServer::AcceptOne()
    {
        errno = 0;
        sockaddr_in addr;
        socklen_t len = sizeof(addr);
        const int socketfd = accept(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
        if (socketfd < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                perror("accept");
            }
            return;
        }

        S_CONNECT_BASE* c = FindFreeConnection();
        S_CONNECT_INDEX* index = FindFdIndex(socketfd);
        if (c == nullptr || index == nullptr)
        {
            close(socketfd);
            return;
        }

        index->index = c->index;
        c->socketfd = socketfd;
        SetNonblock(socketfd);

        if (!AddReadEvent(socketfd))
        {
            ReleaseSocket(socketfd, c, 101);
            return;
        }

        std::memcpy(c->ip, inet_ntoa(addr.sin_addr), MAX_IP_LEN);
        c->port = ntohs(addr.sin_port);
        c->temp_ConnectTime = static_cast<int>(time(nullptr));
        c->temp_HeartTime = static_cast<int>(time(nullptr));
        c->state = E_SSS_Connect;

        ComputeConnectNum(true);

        srand(static_cast<unsigned int>(time(nullptr)));
        const uint8_t value = static_cast<uint8_t>(rand() % 125 + 1);
        S_CMD_XOR code;
        code.xorCode = value ^ c->xorCode;
        CreatePackage(c->index, CMD_XOR, &code, sizeof(S_CMD_XOR));
        c->xorCode = value;

        if (notifyAccept_ != nullptr)
        {
            notifyAccept_(this, c, 0);
        }
    }

    void KqueueServer::RecvAll(int socketfd)
    {
        std::lock_guard<std::mutex> guard(ioMutex_);

        S_CONNECT_BASE* c = FindClient(socketfd, true);
        if (c == nullptr)
        {
            return;
        }

        while (running_)
        {
            const int recvBytes = static_cast<int>(recv(socketfd, tempBuffer_.data(), tempBuffer_.size(), 0));
            if (recvBytes < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                c->recvs.isCompleted = true;
                ShutDownSocket(socketfd, c, 2001);
                return;
            }

            if (recvBytes == 0)
            {
                ShutDownSocket(socketfd, c, 2002);
                return;
            }

            if (c->recvs.head == c->recvs.tail)
            {
                c->recvs.head = 0;
                c->recvs.tail = 0;
            }

            if (c->recvs.tail + recvBytes > ServerXML->recvBytesMax)
            {
                ShutDownSocket(socketfd, c, 2003);
                return;
            }

            std::memcpy(&c->recvs.buf[c->recvs.tail], tempBuffer_.data(), recvBytes);
            c->recvs.tail += recvBytes;
        }

        c->recvs.isCompleted = true;
    }

    void KqueueServer::Update()
    {
        std::lock_guard<std::mutex> guard(ioMutex_);

        for (int i = 0; i < onlines_->Count(); ++i)
        {
            S_CONNECT_BASE* c = onlines_->Value(i);
            if (c->index == -1 || c->state == E_SSS_Free || c->state == E_SSS_NeedSave)
            {
                continue;
            }

            UpdateDisconnect(c);
            if (c->closeState == E_SSC_ShutDown)
            {
                continue;
            }

            ReadPackageHead(c);
            SendPending(c);
        }
    }

    void KqueueServer::ReadPackageHead(S_CONNECT_BASE* c)
    {
        if (!c->recvs.isCompleted)
        {
            return;
        }

        for (int i = 0; i < 1000; ++i)
        {
            const int available = c->recvs.tail - c->recvs.head;
            if (available < 8)
            {
                break;
            }

            char head[2];
            head[0] = c->recvs.buf[c->recvs.head] ^ c->xorCode;
            head[1] = c->recvs.buf[c->recvs.head + 1] ^ c->xorCode;
            if (head[0] != ServerXML->Head[0] || head[1] != ServerXML->Head[1])
            {
                ShutDownSocket(c->socketfd, c, 4001);
                return;
            }

            const int32_t length = (*reinterpret_cast<uint32_t*>(c->recvs.buf + c->recvs.head + 2)) ^ c->xorCode;
            const uint16_t cmd = (*reinterpret_cast<uint16_t*>(c->recvs.buf + c->recvs.head + 6)) ^ c->xorCode;

            if (length < 8 || c->recvs.tail < c->recvs.head + length)
            {
                break;
            }

            c->packageLength = length;
            ReadPackageCommand(c, cmd);
            if (c->state < E_SSS_Connect)
            {
                return;
            }

            c->recvs.head += length;
        }

        c->recvs.isCompleted = false;
    }

    void KqueueServer::ReadPackageCommand(S_CONNECT_BASE* c, uint16_t cmd)
    {
        c->temp_HeartTime = static_cast<int>(time(nullptr));

        if (cmd < 65000)
        {
            if (notifyCommand_ != nullptr)
            {
                notifyCommand_(this, c, cmd);
            }
            return;
        }

        switch (cmd)
        {
        case CMD_HEART:
            CreatePackage(c->index, CMD_HEART, nullptr, 0);
            break;
        case CMD_SECURITY:
        {
            char expectedMd5[MAX_MD5_LEN];
            char seed[20];
            std::snprintf(seed, sizeof(seed), "%s_%d", ServerXML->SecureCode, c->xorCode);
            std::memset(expectedMd5, 0, sizeof(expectedMd5));
            if (MD5_FunPoint != nullptr)
            {
                MD5_FunPoint(expectedMd5, reinterpret_cast<unsigned char*>(seed), static_cast<int>(std::strlen(seed)));
            }

            S_CMD_SECURE secure;
            std::memset(&secure, 0, sizeof(secure));
            ReadPackage(c->index, &secure, sizeof(secure));

            S_CMD_RESULT result;
            result.type = 0;
            if (secure.appVersion != ServerXML->appVersion)
            {
                result.type = 1;
                CreatePackage(c->index, CMD_SECURITY, &result, sizeof(result));
                return;
            }

            if (strcasecmp(expectedMd5, secure.appMD5) != 0)
            {
                result.type = 2;
                CreatePackage(c->index, CMD_SECURITY, &result, sizeof(result));
                return;
            }

            c->appID = secure.appID;
            c->state = E_SSS_Secure;
            CreatePackage(c->index, CMD_SECURITY, &result, sizeof(result));
            ComputeSecureNum(true);
            if (notifySecure_ != nullptr)
            {
                notifySecure_(this, c, 0);
            }
            break;
        }
        default:
            break;
        }
    }

    int KqueueServer::SendPending(S_CONNECT_BASE* c)
    {
        if (c->index < 0 || c->state == E_SSS_Free || c->closeState == E_SSC_ShutDown || c->socketfd < 0)
        {
            return -1;
        }

        if (c->sends.tail <= c->sends.head)
        {
            return 0;
        }

        const int pending = c->sends.tail - c->sends.head;
        const int sendBytes = static_cast<int>(send(c->socketfd, &c->sends.buf[c->sends.head], pending, 0));
        if (sendBytes > 0)
        {
            c->sends.head += sendBytes;
            c->sends.isCompleted = true;
            return 0;
        }

        if (sendBytes < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return 0;
            }
            ShutDownSocket(c->socketfd, c, 3001);
            return -1;
        }

        ShutDownSocket(c->socketfd, c, 3002);
        return -1;
    }

    void KqueueServer::CreatePackage(const int index, const uint16_t cmd, void* v, const int len)
    {
        S_CONNECT_BASE* c = FindClient(index);
        if (c == nullptr)
        {
            return;
        }

        if (c->state <= 0 || c->socketfd <= 0 || c->sends.tail + 8 + len > ServerXML->sendBytesMax)
        {
            ShutDownSocket(c->socketfd, c, 2004);
            return;
        }

        if (c->sends.head == c->sends.tail)
        {
            c->sends.head = 0;
            c->sends.tail = 0;
        }

        int tail = c->sends.tail;
        c->sends.buf[tail] = ServerXML->Head[0] ^ c->xorCode;
        c->sends.buf[tail + 1] = ServerXML->Head[1] ^ c->xorCode;

        uint32_t length = static_cast<uint32_t>(8 + len) ^ c->xorCode;
        std::memcpy(&c->sends.buf[tail + 2], &length, sizeof(length));

        uint16_t encodedCmd = cmd ^ c->xorCode;
        std::memcpy(&c->sends.buf[tail + 6], &encodedCmd, sizeof(encodedCmd));

        tail += 8;
        if (len > 0 && v != nullptr)
        {
            std::memcpy(&c->sends.buf[tail], v, len);
            tail += len;
        }

        c->sends.tail = tail;
    }

    void KqueueServer::ReadPackage(const int index, void* v, const int len)
    {
        S_CONNECT_BASE* c = FindClient(index);
        if (c == nullptr)
        {
            return;
        }

        const uint32_t bodyHead = c->recvs.head + 8;
        const uint32_t packageTail = c->recvs.head + c->packageLength;

        if (c->index == -1 ||
            c->state == E_SSS_Free ||
            c->recvs.buf == nullptr ||
            bodyHead + len > static_cast<uint32_t>(ServerXML->recvBytesMax) ||
            bodyHead + len > packageTail)
        {
            return;
        }

        std::memcpy(v, &c->recvs.buf[bodyHead], len);
    }

    void KqueueServer::UpdateDisconnect(S_CONNECT_BASE* c)
    {
        int elapsed = 0;
        if (c->closeState == E_SSC_ShutDown)
        {
            elapsed = static_cast<int>(time(nullptr)) - c->temp_CloseTime;
            if (c->recvs.isCompleted && c->sends.isCompleted)
            {
                ReleaseSocket(c->socketfd, c, 1001);
            }
            else if (elapsed > 1)
            {
                ReleaseSocket(c->socketfd, c, 1002);
            }
            return;
        }

        elapsed = static_cast<int>(time(nullptr)) - c->temp_ConnectTime;
        if (c->state == E_SSS_Connect && elapsed > 10)
        {
            ShutDownSocket(c->socketfd, c, 1001);
            return;
        }

        elapsed = static_cast<int>(time(nullptr)) - c->temp_HeartTime;
        if (elapsed > ServerXML->maxHeartTime)
        {
            ShutDownSocket(c->socketfd, c, 1002);
        }
    }

    void KqueueServer::ShutDownSocket(int socketfd, S_CONNECT_BASE* c, int kind)
    {
        if (c == nullptr)
        {
            c = FindClient(socketfd, true);
        }

        if (c == nullptr || c->state == E_SSS_Free || c->closeState == E_SSC_ShutDown)
        {
            return;
        }

        c->recvs.isCompleted = true;
        c->sends.isCompleted = true;
        c->temp_ShutDown = kind;
        c->temp_CloseTime = static_cast<int>(time(nullptr));
        c->closeState = E_SSC_ShutDown;
        shutdown(socketfd, SHUT_RDWR);
    }

    int32_t KqueueServer::ReleaseSocket(int socketfd, S_CONNECT_BASE* c, int kind)
    {
        if (socketfd < 0)
        {
            return -1;
        }

        if (c != nullptr)
        {
            if (c->state == E_SSS_Free)
            {
                return 0;
            }
            if (c->state >= E_SSS_Secure)
            {
                ComputeSecureNum(false);
            }
        }

        DeleteReadEvent(socketfd);

        S_CONNECT_INDEX* fdSlot = FindFdIndex(socketfd);
        if (fdSlot != nullptr)
        {
            fdSlot->Reset();
        }

        if (kind != 101)
        {
            ComputeConnectNum(false);
            shutdown(socketfd, SHUT_RDWR);
        }
        close(socketfd);

        if (notifyDisconnect_ != nullptr)
        {
            notifyDisconnect_(this, c, kind);
        }

        return 0;
    }

    bool KqueueServer::IsCloseClient(const int index, int secure)
    {
        if (index < 0 || index >= onlines_->Count())
        {
            return false;
        }

        S_CONNECT_BASE* c = onlines_->Value(index);
        if (c == nullptr || c->state >= secure)
        {
            return false;
        }

        ShutDownSocket(c->socketfd, c, 7001);
        return true;
    }

    S_CONNECT_BASE* KqueueServer::FindClient(const int socketfd, bool issecure)
    {
        if (socketfd < 0 || socketfd >= MAX_SOCKETFD_LEN)
        {
            return nullptr;
        }

        S_CONNECT_INDEX* fdSlot = fdIndex_->Value(socketfd);
        if (fdSlot == nullptr || fdSlot->index < 0)
        {
            return nullptr;
        }

        S_CONNECT_BASE* c = FindClient(fdSlot->index);
        if (c == nullptr)
        {
            return nullptr;
        }

        if (issecure && !c->IsEqual(socketfd))
        {
            return nullptr;
        }

        return c;
    }

    S_CONNECT_BASE* KqueueServer::FindClient(const int index)
    {
        if (index < 0 || index >= onlines_->Count())
        {
            return nullptr;
        }
        return onlines_->Value(index);
    }

    S_CONNECT_BASE* KqueueServer::FindFreeConnection()
    {
        std::lock_guard<std::mutex> guard(freeConnectionMutex_);

        for (int i = 0; i < onlines_->Count(); ++i)
        {
            S_CONNECT_BASE* c = onlines_->Value(i);
            if (c->state == E_SSS_Free)
            {
                c->Reset();
                c->index = i;
                c->state = E_SSS_Connect;
                return c;
            }
        }

        return nullptr;
    }

    S_CONNECT_INDEX* KqueueServer::FindFdIndex(int socketfd)
    {
        if (socketfd < 0 || socketfd >= MAX_SOCKETFD_LEN)
        {
            return nullptr;
        }
        return fdIndex_->Value(socketfd);
    }

    void KqueueServer::ComputeSecureNum(bool isadd)
    {
        std::lock_guard<std::mutex> guard(secureCountMutex_);
        secureCount_ += isadd ? 1 : -1;
    }

    void KqueueServer::ComputeConnectNum(bool isadd)
    {
        std::lock_guard<std::mutex> guard(connectCountMutex_);
        connectCount_ += isadd ? 1 : -1;
    }

    void KqueueServer::SetNotify_Connect(ISERVER_NOTIFY e)
    {
        notifyAccept_ = e;
    }

    void KqueueServer::SetNotify_Secure(ISERVER_NOTIFY e)
    {
        notifySecure_ = e;
    }

    void KqueueServer::SetNotify_DisConnect(ISERVER_NOTIFY e)
    {
        notifyDisconnect_ = e;
    }

    void KqueueServer::SetNotify_Command(ISERVER_NOTIFY e)
    {
        notifyCommand_ = e;
    }
}

