/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_REQUEST_HANDLER_FACTORY_IMPL
#define A2A_REQUEST_HANDLER_FACTORY_IMPL

#include <memory>

#include "server/request_handler_factory.h"
#include "utils/types.h"

namespace a2a::server {

class RequestHandlerFactoryImpl {
public:
    explicit RequestHandlerFactoryImpl();

    std::shared_ptr<RequestHandler> Create(std::shared_ptr<AgentExecutor> executor,
                                           std::shared_ptr<AgentCard> agentCard,
                                           std::shared_ptr<QueueManager> mgr) const;
};

} // namespace a2a::server

#endif