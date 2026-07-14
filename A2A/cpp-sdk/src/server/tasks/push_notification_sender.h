/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_PUSH_NOTIFICATION_SENDER
#define A2A_PUSH_NOTIFICATION_SENDER

#include "types.h"

namespace A2A::Server {

struct PushNotificationSender {
    virtual ~PushNotificationSender() = default;

    virtual void SendNotification(const std::shared_ptr<Task>& task) = 0;
};

} // namespace A2A::Server

#endif