/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TEST_FIXTURES_MOCK_AGENT_EXECUTOR_H
#define A2A_TEST_FIXTURES_MOCK_AGENT_EXECUTOR_H

#include <memory>

#include "server/agent_executor.h"

namespace A2A::Test {

class DummyAgentExecutor final : public Server::AgentExecutor {
public:
    void Execute(std::shared_ptr<Server::RequestContext>, std::shared_ptr<Server::TaskUpdater>) override
    {
    }

    void Cancel(std::shared_ptr<Server::RequestContext>, std::shared_ptr<Server::TaskUpdater>) override
    {
    }
};

} // namespace A2A::Test

#endif
