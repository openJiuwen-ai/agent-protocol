/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_TEST_FIXTURES_MOCK_TASK_STORE_H
#define A2A_TEST_FIXTURES_MOCK_TASK_STORE_H

#include <memory>
#include <string>

#include "server/task_store.h"

namespace A2A::Test {

class DummyTaskStore final : public Server::TaskStore {
public:
    void Save(const Task&, std::shared_ptr<Server::ServerCallContext>) override
    {
    }

    std::shared_ptr<Task> Get(const std::string&, std::shared_ptr<Server::ServerCallContext>) override
    {
        return nullptr;
    }

    void Delete(const std::string&, std::shared_ptr<Server::ServerCallContext>) override
    {
    }
};

} // namespace A2A::Test

#endif
