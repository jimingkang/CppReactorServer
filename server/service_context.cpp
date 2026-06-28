#include "service_context.h"

ServiceContext::ServiceContext(ServiceId id) : id_(id) {}

ServiceId ServiceContext::id() const noexcept {
    return id_;
}

bool ServiceContext::push(SkynetMessage message) {
    std::lock_guard lock(mutex_);
    queue_.push(std::move(message));
    if (scheduled_) {
        return false;
    }
    scheduled_ = true;
    return true;
}

bool ServiceContext::popOne(SkynetMessage& message) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    message = std::move(queue_.front());
    queue_.pop();
    return true;
}

bool ServiceContext::finishBatch() {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        scheduled_ = false;
        return false;
    }
    return true;
}

void ServiceContext::clearPending() {
    std::lock_guard lock(mutex_);
    std::queue<SkynetMessage> empty;
    queue_.swap(empty);
    scheduled_ = false;
}
