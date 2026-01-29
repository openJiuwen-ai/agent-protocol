/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_REQUEST_CONTEXT
#define A2A_REQUEST_CONTEXT

#include <unordered_set>
#include <optional>
#include <string>
#include <vector>
#include <memory>

#include "server_call_context.h"
#include "types.h"

namespace A2A::Server {

class RequestContextImpl;

struct RequestContextParam {
    std::optional<A2A::MessageSendParams> request = std::nullopt;
    std::optional<std::string> taskId = std::nullopt;
    std::optional<std::string> contextId = std::nullopt;
    std::optional<A2A::Task> task = std::nullopt;
    std::vector<A2A::Task> relatedTasks;
    std::shared_ptr<A2A::Server::ServerCallContext> callContext;
};

class RequestContext {
public:
    /**
     * @brief constructor
     *
     * @param[in] param params used to construct RequestContext
     */
    RequestContext(const RequestContextParam& param);

    /**
     * @brief extracts text content from the users's message parts
     *
     * @param[in] delimiter the string to use when joining multiple text parts
     * @return a single string containing akk text content from the user message
     */
    std::string GetUserInput(const std::string& delimiter = "\n") const;

    /**
     * @brief attach a related task to the context
     *
     * @param[in] task the 'Task' object to attach
     */
    void AttachRelatedTask(const A2A::Task& task);

    /**
     * @brief get the 'Message' object from the request, if available
     *
     * @return the 'Message' object from the request, if available
     */
    const A2A::Message* GetMessage() const;

    /**
     * @brief get a list of other tasks related to the current request
     *
     * @return a list of other tasks related to the current request
     */
    const std::vector<A2A::Task>& GetRelatedTasks() const;

    /**
     * @brief get the current 'Task' being processed
     *
     * @return the current 'Task' being processed
     */
    const std::optional<A2A::Task>& GetCurrentTask() const;

    /**
     * @brief set the current 'Task'
     *
     * @param[in] t the current 'Task' object
     */
    void SetCurrentTask(const A2A::Task& t);

    /**
     * @brief get the ID of the task associated with this task
     *
     * @return the ID of the task associated with this task
     */
    const std::optional<std::string>& GetTaskId() const;

    /**
     * @brief get the ID of the context associated with this task
     *
     * @return the ID of the context associated with this task
     */
    const std::optional<std::string>& GetContextId() const;

    /**
     * @brief get the 'MessageSendConfiguration' from the request, if available
     *
     * @return the 'MessageSendConfiguration' from the request
     */
    std::shared_ptr<MessageSendConfiguration> GetConfiguration() const;

    /**
     * @brief get server call context from the request, if available
     *
     * @return server call context from the request
     */
    std::shared_ptr<ServerCallContext> GetCallContext() const;

    /**
     * @brief get metadate associated whit the request, if available
     *
     * @return metadate associated whit the request
     */
    nlohmann::json GetMetadata() const;

    /**
     * @brief add an extension to the set of activated extensions for this request
     *
     * @param[in] uri uri of extension
     */
    void AddActivatedExtension(const std::string& uri);

    /**
     * @brief get the extensions that the client requested to activate
     *
     * @return a list of the extensions that the client requested to activate
     */
    std::unordered_set<std::string> GetRequestedExtensions() const;

private:
    std::unique_ptr<RequestContextImpl> impl_;
};

} // namespace A2A::Server
#endif
