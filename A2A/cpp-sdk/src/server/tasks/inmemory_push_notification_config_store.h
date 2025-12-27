/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_INMEMORY_PUSH_NOTIFICATION_STORE
#define A2A_INMEMORY_PUSH_NOTIFICATION_STORE

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "push_notification_config_store.h"

namespace a2a::server {

class InMemoryPushNotificationConfigStore : public PushNotificationConfigStore {
public:
    void SetInfo(const std::string& taskId, a2a::PushNotificationConfig notificationConfig) override;

    std::vector<a2a::PushNotificationConfig> GetInfo(const std::string& taskId) override;

    void DeleteInfo(const std::string& taskId, const std::optional<std::string>& configId = std::nullopt) override;

private:
    std::mutex m_;
    std::unordered_map<std::string, std::vector<a2a::PushNotificationConfig>> data_;
};

} // namespace a2a::server

#endif
