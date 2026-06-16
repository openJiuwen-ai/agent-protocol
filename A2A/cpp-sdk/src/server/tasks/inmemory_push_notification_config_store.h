/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_INMEMORY_PUSH_NOTIFICATION_STORE
#define A2A_INMEMORY_PUSH_NOTIFICATION_STORE

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "push_notification_config_store.h"

namespace A2A::Server {

class InMemoryPushNotificationConfigStore : public PushNotificationConfigStore {
public:
    void SetInfo(const std::string& taskId, A2A::PushNotificationConfig notificationConfig) override;

    ~InMemoryPushNotificationConfigStore() override = default;

    std::vector<A2A::PushNotificationConfig> GetInfo(const std::string& taskId) override;

    void DeleteInfo(const std::string& taskId, const std::optional<std::string>& configId = std::nullopt) override;

private:
    std::mutex m;
    std::unordered_map<std::string, std::vector<A2A::PushNotificationConfig>> data_;
};

} // namespace A2A::Server

#endif