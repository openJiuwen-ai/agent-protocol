/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "base_push_notification_sender.h"
#include "default_request_handler.h"
#include "events/in_memory_queue_manager.h"
#include "request_handler_factory_impl.h"
#include "server/queue_manager.h"

namespace a2a::server {

RequestHandlerFactoryImpl::RequestHandlerFactoryImpl()
{
}

std::shared_ptr<RequestHandler> RequestHandlerFactoryImpl::Create(std::shared_ptr<AgentExecutor> executor,
                                                                  std::shared_ptr<AgentCard> agentCard,
                                                                  std::shared_ptr<QueueManager> mgr) const
{
    return std::make_shared<DefaultRequestHandler>(executor, agentCard,
                                                   mgr ? mgr : std::make_shared<InMemoryQueueManager>());
}

} // namespace a2a::server