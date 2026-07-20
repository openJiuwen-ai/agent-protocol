/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_PUSH_NOTIFICATION_STORE
#define A2A_PUSH_NOTIFICATION_STORE

#include <optional>
#include <string>
#include <vector>

#include "types.h"

namespace A2A::Server {

struct PushNotificationConfigStore {
    virtual ~PushNotificationConfigStore() = default;

    virtual void SetInfo(const std::string& taskId, A2A::PushNotificationConfig notificationConfig) = 0;

    virtual std::vector<A2A::PushNotificationConfig> GetInfo(const std::string& taskId) = 0;

    virtual void DeleteInfo(const std::string& taskId, const std::optional<std::string>& configId = std::nullopt) = 0;
};

} // namespace A2A::Server

#endif
