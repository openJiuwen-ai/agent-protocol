/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_PUSH_NOTIFICATION_STORE
#define A2A_PUSH_NOTIFICATION_STORE

#include <optional>
#include <string>
#include <vector>

#include "utils/types.h"

namespace a2a::server {

struct PushNotificationConfigStore {
    virtual ~PushNotificationConfigStore() = default;

    virtual void SetInfo(const std::string& taskId, a2a::PushNotificationConfig notificationConfig) = 0;

    virtual std::vector<a2a::PushNotificationConfig> GetInfo(const std::string& taskId) = 0;

    virtual void DeleteInfo(const std::string& taskId, const std::optional<std::string>& configId = std::nullopt) = 0;
};

} // namespace a2a::server

#endif
