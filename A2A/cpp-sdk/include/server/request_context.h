/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_REQUEST_CONTEXT
#define A2A_REQUEST_CONTEXT

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "server/server_call_context.h"
#include "utils/id_generator.h"
#include "utils/types.h"
#include "utils/utils_message.h"

namespace a2a::server {

class RequestContextImpl;

// Request Context equivalent to Python's RequestContext
class RequestContext {
public:
    /**
     * @brief constructor
     *
     * @param[in] service credential service
     */
    RequestContext(const std::optional<a2a::MessageSendParams>& request = std::nullopt,
                   const std::optional<std::string>& taskId = std::nullopt,
                   const std::optional<std::string>& contextId = std::nullopt,
                   const std::optional<a2a::Task>& task = std::nullopt, const std::vector<a2a::Task>& relatedTasks = {},
                   const a2a::server::ServerCallContext* callContext = nullptr,
                   std::shared_ptr<IDGenerator> taskIdGenerator = nullptr,
                   std::shared_ptr<IDGenerator> contextIdGenerator = nullptr);

    // Helpers
    std::string GetUserInput(const std::string& delimiter = "\n") const;

    void AttachRelatedTask(const a2a::Task& task);

    // Accessors
    const a2a::Message* Message() const;

    const std::vector<a2a::Task>& RelatedTasks() const;

    const std::optional<a2a::Task>& CurrentTask() const;

    void SetCurrentTask(const a2a::Task& t);

    const std::optional<std::string>& TaskId() const;

    const std::optional<std::string>& ContextId() const;

    const std::optional<nlohmann::json> Configuration() const;

    const a2a::server::ServerCallContext* CallContext() const;

    nlohmann::json Metadata() const;

    void AddActivatedExtension(const std::string& uri);

    std::unordered_set<std::string> RequestedExtensions() const;

private:
    std::unique_ptr<RequestContextImpl> impl_;
};

} // namespace a2a::server
#endif
