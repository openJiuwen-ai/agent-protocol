/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_BASE_PUSH_NOTIFICATION_SENDER
#define A2A_BASE_PUSH_NOTIFICATION_SENDER

#include <memory>

#include "push_notification_config_store.h"
#include "push_notification_sender.h"

namespace a2a::server {

class BasePushNotificationSender : public PushNotificationSender {
public:
    explicit BasePushNotificationSender(std::shared_ptr<PushNotificationConfigStore> configStore);

    void SendNotification(const a2a::Task& task) override;

protected:
    bool DispatchNotification(const a2a::Task& task, const a2a::PushNotificationConfig& pushInfo);

private:
    std::shared_ptr<PushNotificationConfigStore> configStore_;
};

} // namespace a2a::server
#endif
