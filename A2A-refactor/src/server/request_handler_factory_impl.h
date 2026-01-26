/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_REQUEST_HANDLER_FACTORY_IMPL
#define A2A_REQUEST_HANDLER_FACTORY_IMPL

#include <memory>

#include "server/request_handler_factory.h"
#include "utils/types.h"

namespace A2A::Server {

class RequestHandlerFactoryImpl {
public:
    explicit RequestHandlerFactoryImpl();

    ~RequestHandlerFactoryImpl() = default;

    std::shared_ptr<RequestHandler> Create(std::shared_ptr<AgentExecutor> executor,
                                           std::shared_ptr<AgentCard> agentCard,
                                           std::shared_ptr<QueueManager> mgr) const;
};

} // namespace A2A::Server

#endif