/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_REQUEST_HANDLER_FACTORY
#define A2A_REQUEST_HANDLER_FACTORY

#include <memory>

#include "server/agent_executor.h"
#include "server/queue_manager.h"
#include "server/request_handler.h"
#include "utils/types.h"

namespace a2a::server {

class RequestHandlerFactoryImpl;

class RequestHandlerFactory {
public:
    /**
     * @brief constructor
     */
    RequestHandlerFactory();

    /**
     * @brief destructor
     */
    ~RequestHandlerFactory();

    /**
     * @brief create request handler
     *
     * @param[in] executor agent executor
     * @param[in] agentCard agent card
     * @param[in] mgr queue manager
     * @return shared pointer of client been created
     */
    std::shared_ptr<RequestHandler> Create(std::shared_ptr<AgentExecutor> executor,
                                           std::shared_ptr<AgentCard> agentCard,
                                           std::shared_ptr<QueueManager> mgr = nullptr) const;

private:
    std::unique_ptr<RequestHandlerFactoryImpl> impl_;
};

} // namespace a2a::server

#endif
