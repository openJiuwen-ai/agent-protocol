/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "inmemory_push_notification_config_store.h"
#include "utils/types.h"

namespace a2a::server {

void InMemoryPushNotificationConfigStore::SetInfo(const std::string& task_id,
                                                  a2a::PushNotificationConfig notification_config)
{
    std::lock_guard<std::mutex> g(m_);
    auto& vec = data_[task_id];
    if (!notification_config.id)
        notification_config.id = task_id;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it->id == notification_config.id) {
            vec.erase(it);
            break;
        }
    }
    vec.push_back(std::move(notification_config));
}

std::vector<a2a::PushNotificationConfig> InMemoryPushNotificationConfigStore::GetInfo(const std::string& task_id)
{
    std::lock_guard<std::mutex> g(m_);
    auto it = data_.find(task_id);
    if (it == data_.end()) {
        return {};
    }
    return it->second;
}

void InMemoryPushNotificationConfigStore::DeleteInfo(const std::string& task_id,
                                                     const std::optional<std::string>& config_id)
{
    std::lock_guard<std::mutex> g(m_);
    auto it = data_.find(task_id);
    if (it == data_.end()) {
        return;
    }
    auto& vec = it->second;
    std::string cid = config_id.value_or(task_id);
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

} // namespace a2a::server
