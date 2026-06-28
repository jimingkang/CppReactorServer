#pragma once

#include "worker_protocol.h"

class IServiceDispatcher {
public:
    virtual ~IServiceDispatcher() = default;
    virtual void dispatch(const SkynetMessage& message) = 0;
};
