/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <curl/curl.h>

#include <iostream>
#include <nlohmann/json.hpp>

#include "base_push_notification_sender.h"
#include "utils/errors.h"

namespace a2a::server {

BasePushNotificationSender::BasePushNotificationSender(std::shared_ptr<PushNotificationConfigStore> config_store)
    : configStore_(std::move(config_store))
{
}

void BasePushNotificationSender::SendNotification(const a2a::Task& task)
{
    auto configs = configStore_->GetInfo(task.id);
    if (configs.empty()) {
        return;
    }
    for (const auto& cfg : configs) {
        if (!DispatchNotification(task, cfg)) {
            std::cerr << "Warning: Failed to send push notification for task_id=" << task.id << " to " << cfg.url
                      << "\n";
        }
    }
}

bool BasePushNotificationSender::DispatchNotification(const a2a::Task& task,
                                                      const a2a::PushNotificationConfig& push_info)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    bool ok = false;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string token_header;
    if (push_info.token) {
        token_header = std::string("X-A2A-Notification-Token: ") + *push_info.token;
        headers = curl_slist_append(headers, token_header.c_str());
    }
    nlohmann::json payload = task; // use to_json
    std::string body = payload.dump();
    curl_easy_setopt(curl, CURLOPT_URL, push_info.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    auto res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (res == CURLE_OK && status >= static_cast<int>(HttpStatusCode::OK)
        && status < static_cast<int>(HttpStatusCode::MovedPermanently)) {
        ok = true;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return ok;
}

} // namespace a2a::server
