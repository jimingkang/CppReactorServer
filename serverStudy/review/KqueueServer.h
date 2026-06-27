#ifndef SERVERSTUDY_REVIEW_KQUEUE_SERVER_H
#define SERVERSTUDY_REVIEW_KQUEUE_SERVER_H

#include "../core/ITcp.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace tcp
{
    // macOS/iMac kqueue backend draft.
    //
    // This class intentionally keeps the old IServer surface:
    // connection pool, fd->index mapping, CreatePackage/ReadPackage and notify callbacks.
    // Only the event backend is changed from Linux epoll to BSD kqueue.
    class KqueueServer final : public IServer
    {
    public:
        KqueueServer();
        ~KqueueServer() override;

        int ConnnectCount() override;
        int SecureCount() override;

        void StartServer() override;
        void StopServer() override;

        bool IsCloseClient(const int index, int secure) override;
        S_CONNECT_BASE* FindClient(const int socketfd, bool issecure) override;
        S_CONNECT_BASE* FindClient(const int index) override;

        void Update() override;

        void CreatePackage(const int index, const uint16_t cmd, void* v, const int len) override;
        void ReadPackage(const int index, void* v, const int len) override;

        void SetNotify_Connect(ISERVER_NOTIFY e) override;
        void SetNotify_Secure(ISERVER_NOTIFY e) override;
        void SetNotify_DisConnect(ISERVER_NOTIFY e) override;
        void SetNotify_Command(ISERVER_NOTIFY e) override;

    private:
        int InitSocket();
        void InitConnections();
        void EventLoop();

        bool SetNonblock(int fd);
        bool AddReadEvent(int fd);
        void DeleteReadEvent(int fd);

        void AcceptAll();
        void AcceptOne();
        void RecvAll(int socketfd);
        int SendPending(S_CONNECT_BASE* c);

        void ReadPackageHead(S_CONNECT_BASE* c);
        void ReadPackageCommand(S_CONNECT_BASE* c, uint16_t cmd);

        void UpdateDisconnect(S_CONNECT_BASE* c);
        void ShutDownSocket(int socketfd, S_CONNECT_BASE* c, int kind);
        int32_t ReleaseSocket(int socketfd, S_CONNECT_BASE* c, int kind);

        S_CONNECT_BASE* FindFreeConnection();
        S_CONNECT_INDEX* FindFdIndex(int socketfd);

        void ComputeSecureNum(bool isadd);
        void ComputeConnectNum(bool isadd);

    private:
        common::HashContainer<S_CONNECT_BASE>* onlines_;
        common::HashContainer<S_CONNECT_INDEX>* fdIndex_;

        ISERVER_NOTIFY notifyAccept_;
        ISERVER_NOTIFY notifySecure_;
        ISERVER_NOTIFY notifyDisconnect_;
        ISERVER_NOTIFY notifyCommand_;

        std::mutex connectCountMutex_;
        std::mutex secureCountMutex_;
        std::mutex freeConnectionMutex_;
        std::mutex ioMutex_;

        int32_t connectCount_;
        int32_t secureCount_;

        std::atomic_bool running_;
        std::thread eventThread_;

        int listenFd_;
        int kqueueFd_;
        std::vector<char> tempBuffer_;
    };

    IServer* NewKqueueServer();
}

#endif

