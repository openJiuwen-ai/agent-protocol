/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_INMEMORY_TASK_STORE
#define A2A_INMEMORY_TASK_STORE

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "server_call_context.h"
#include "server/task_store.h"

namespace A2A::Server {

class InMemoryTaskStore : public TaskStore {
public:
    void Save(const A2A::Task& task, std::shared_ptr<ServerCallContext> context) override;

    ~InMemoryTaskStore() override = default;

    std::optional<A2A::Task> Get(const std::string& taskId, std::shared_ptr<ServerCallContext> context) override;

    void Delete(const std::string& taskId, std::shared_ptr<ServerCallContext> context) override;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<A2A::Task>> tasks_;
};

} // namespace A2A::Server

#endif
