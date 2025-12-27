/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_INMEMORY_TASK_STORE
#define A2A_INMEMORY_TASK_STORE

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "task_store.h"

namespace a2a::server {

class InMemoryTaskStore : public TaskStore {
public:
    void Save(const a2a::Task& task, const a2a::server::ServerCallContext* context = nullptr) override;

    std::optional<a2a::Task> Get(const std::string& taskId,
                                 const a2a::server::ServerCallContext* context = nullptr) override;

    void Delete(const std::string& taskId, const a2a::server::ServerCallContext* context = nullptr) override;

private:
    std::mutex m_;
    std::unordered_map<std::string, a2a::Task> tasks_;
};

} // namespace a2a::server

#endif
