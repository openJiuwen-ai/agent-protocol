/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_INMEMORY_TASK_STORE
#define A2A_INMEMORY_TASK_STORE

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "server/task_store.h"

namespace A2A::Server {

class InMemoryTaskStore : public TaskStore {
public:
    void Save(const A2A::Task& task, const A2A::Server::ServerCallContext* context = nullptr) override;

    ~InMemoryTaskStore() override = default;

    std::optional<A2A::Task> Get(const std::string& taskId,
                                 const A2A::Server::ServerCallContext* context = nullptr) override;

    void Delete(const std::string& taskId, const A2A::Server::ServerCallContext* context = nullptr) override;

private:
    std::mutex m_;
    std::unordered_map<std::string, A2A::Task> tasks_;
};

} // namespace A2A::Server

#endif
