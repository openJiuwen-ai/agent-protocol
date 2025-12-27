/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "default_request_handler.h"
#include "request_handler_factory_impl.h"
#include "server/request_handler_factory.h"

namespace a2a::server {

RequestHandlerFactory::RequestHandlerFactory() : impl_(std::make_unique<RequestHandlerFactoryImpl>())
{
}

RequestHandlerFactory::~RequestHandlerFactory() = default;

std::shared_ptr<RequestHandler> RequestHandlerFactory::Create(std::shared_ptr<AgentExecutor> executor,
                                                              std::shared_ptr<AgentCard> agentCard,
                                                              std::shared_ptr<QueueManager> mgr) const
{
    return impl_->Create(executor, agentCard, mgr);
}

} // namespace a2a::server
