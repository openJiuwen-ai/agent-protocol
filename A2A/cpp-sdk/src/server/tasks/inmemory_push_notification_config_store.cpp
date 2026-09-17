/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "inmemory_push_notification_config_store.h"
#include "types.h"

namespace A2A::Server {

void InMemoryPushNotificationConfigStore::SetInfo(const std::string& taskId,
    A2A::PushNotificationConfig notificationConfig)
{
    std::lock_guard<std::mutex> g(m);
    auto& vec = data_[taskId];
    if (!notificationConfig.id) {
        notificationConfig.id = taskId;
    }
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it->id == notificationConfig.id) {
            vec.erase(it);
            break;
        }
    }
    vec.push_back(std::move(notificationConfig));
}

std::vector<A2A::PushNotificationConfig> InMemoryPushNotificationConfigStore::GetInfo(const std::string& taskId)
{
    std::lock_guard<std::mutex> g(m);
    auto it = data_.find(taskId);
    if (it == data_.end()) {
        return {};
    }
    return it->second;
}

void InMemoryPushNotificationConfigStore::DeleteInfo(const std::string& taskId,
    const std::optional<std::string>& configId)
{
    std::lock_guard<std::mutex> g(m);
    auto it = data_.find(taskId);
    if (it == data_.end()) {
        return;
    }
    auto& vec = it->second;
    std::string cid = configId.value_or(taskId);
    for (auto vit = vec.begin(); vit != vec.end(); ++vit) {
        if (vit->id && *vit->id == cid) {
            vec.erase(vit);
            break;
        }
    }
    if (vec.empty()) {
        data_.erase(it);
    }
}

} // namespace A2A::Server