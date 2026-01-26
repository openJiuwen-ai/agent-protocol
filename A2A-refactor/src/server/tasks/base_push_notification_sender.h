/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_BASE_PUSH_NOTIFICATION_SENDER
#define A2A_BASE_PUSH_NOTIFICATION_SENDER

#include <memory>

#include "push_notification_config_store.h"
#include "push_notification_sender.h"

namespace A2A::Server {

class BasePushNotificationSender : public PushNotificationSender {
public:
    explicit BasePushNotificationSender(std::shared_ptr<PushNotificationConfigStore> configStore);

    void SendNotification(const A2A::Task& task) override;

    ~BasePushNotificationSender() override = default;

protected:
    bool DispatchNotification(const A2A::Task& task, const A2A::PushNotificationConfig& pushInfo);

private:
    std::shared_ptr<PushNotificationConfigStore> configStore_;
};

} // namespace A2A::Server
#endif
