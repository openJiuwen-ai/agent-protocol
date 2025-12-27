/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_PUSH_NOTIFICATION_SENDER
#define A2A_PUSH_NOTIFICATION_SENDER

#include <memory>

#include "utils/types.h"

namespace a2a::server {

struct PushNotificationSender {
    virtual ~PushNotificationSender() = default;

    virtual void SendNotification(const a2a::Task& task) = 0;
};

} // namespace a2a::server

#endif
