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

/** @brief Opaque implementation of RequestContext (PIMPL). */
class RequestContextImpl;

/**
 * @brief Per-request context passed to AgentExecutor.
 * @note 封装 message/send 请求的 message、task、扩展等信息。
 */
class RequestContext {
public:
    /**
     * @brief Construct from request parameters.
     * @param[in] param Construction parameters.
     */
    explicit RequestContext(const RequestContextParam& param);

    /** @brief Destructor. */
    ~RequestContext();

    /**
     * @brief Extract text content from the user message parts.
     * @param[in] delimiter String used when joining multiple text parts.
     * @return Concatenated text from all user message parts.
     */
    std::string GetUserInput(const std::string& delimiter = "\n") const;

    /**
     * @brief Attach a related task to this context.
     * @param[in] task Task to attach.
     */
    void AttachRelatedTask(const A2A::Task& task);

    /**
     * @brief Get the inbound Message from the request, if present.
     * @return Pointer to the message, or nullptr.
     */
    const A2A::Message* GetMessage() const;

    /**
     * @brief Get tasks related to the current request.
     * @return Reference to the related-task list.
     */
    const std::vector<A2A::Task>& GetRelatedTasks() const;

    /**
     * @brief Get the task currently being processed.
     * @return Shared pointer to the current task, or nullptr.
     */
    std::shared_ptr<Task> GetCurrentTask() const;

    /**
     * @brief Get the task ID associated with this request.
     * @return Task ID, or nullopt.
     */
    const std::optional<std::string>& GetTaskId() const;

    /**
     * @brief Get the conversation context ID.
     * @return Context ID, or nullopt.
     */
    const std::optional<std::string>& GetContextId() const;

    /**
     * @brief Get the MessageSendConfiguration from the request.
     * @return Configuration, or nullptr.
     */
    std::shared_ptr<MessageSendConfiguration> GetConfiguration() const;

    /**
     * @brief Get the server call context for this request.
     * @return Server call context, or nullptr.
     */
    std::shared_ptr<ServerCallContext> GetCallContext() const;

    /**
     * @brief Get request-level metadata JSON.
     * @return Metadata string (may be empty).
     */
    std::string GetMetadata() const;

    /**
     * @brief Record an activated protocol extension URI.
     * @param[in] uri Extension URI.
     */
    void AddActivatedExtension(const std::string& uri);

    /**
     * @brief Get extensions requested by the client.
     * @return Set of requested extension URIs.
     */
    std::unordered_set<std::string> GetRequestedExtensions() const;

private:
    std::unique_ptr<RequestContextImpl> impl_;
};

} // namespace A2A::Server
#endif
