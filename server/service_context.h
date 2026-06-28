#pragma once

#include "worker_protocol.h"

#include <mutex>
#include <queue>

class WorkerGameServer;

class ServiceContext {
public:
    explicit ServiceContext(ServiceId id);
    virtual ~ServiceContext() = default;

    ServiceId id() const noexcept;
    bool push(SkynetMessage message);
    bool popOne(SkynetMessage& message);
    bool finishBatch();
    void clearPending();

    virtual void dispatch(WorkerGameServer& server, const SkynetMessage& message) = 0;

private:
    ServiceId id_;
    std::mutex mutex_;
    std::queue<SkynetMessage> queue_;
    bool scheduled_ = false;
};
