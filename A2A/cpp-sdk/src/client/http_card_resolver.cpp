/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <future>
#include <string>

#include "uuid.h"
#include "a2a_log.h"
#include "jsonrpc.h"
#include "http_card_resolver.h"

namespace A2A::Client {

namespace {
constexpr int K_BAD_ALLOC = -32110;
constexpr int K_TRANSPORT_EXCEPTION = -32102;
constexpr int K_INVALID_FORMAT = -32106;
} // namespace

HttpCardResolver::HttpCardResolver(
    std::string baseUrl,
    const std::optional<std::string>& relativeCardPath,
    std::map<std::string, std::string> httpKwargs)
    : baseUrl_(std::move(baseUrl)), relativeCardPath_(relativeCardPath), httpKwargs_(std::move(httpKwargs))
{
    std::string path = relativeCardPath_.value_or("");

    // combine urls
    std::string fullPath = baseUrl_;
    // remove ending '/' from baseUrl
    if (!fullPath.empty() && fullPath.back() == '/') {
        fullPath.pop_back();
    }
    // ensure path starts with '/'(avoiding '//')
    if (!path.empty()) {
        if (path.front() != '/') {
            fullPath += '/';
        }
        fullPath += path;
    }
    // create JSONRPC transport
    transport_ = std::make_shared<JsonRpcTransport>(fullPath, AgentCard{}, ClientConfig{},
        std::vector<std::shared_ptr<ClientCallInterceptor>>{});
    // register callback
    transport_->SetTransportCallback([this](const std::string& id, const TransportEvent& ev) {
        this->OnTransportEvent(id, ev);
    });
}

std::future<AgentCard> HttpCardResolver::GetAgentCard([[maybe_unused]] const std::optional<std::string>&
    relativeCardPath) const
{
    std::string requestId = GenerateUuid();
    std::shared_ptr<std::promise<AgentCard>> promise;
    try {
        promise = std::make_shared<std::promise<AgentCard>>();
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            pendingPromises_[requestId] = promise;
        }

        // send request
        transport_->GetCard(requestId, nullptr, 0);
    } catch (const std::bad_alloc& e) {
        A2A_LOG(A2A_LOG_LEVEL_ERROR, std::string("exception occured: ") + e.what());
        std::promise<AgentCard> fallbackPromise;
        fallbackPromise.set_exception(CreateExceptionPtr(K_BAD_ALLOC, e.what()));
        std::lock_guard<std::mutex> lock(callbackMutex_);
        pendingPromises_.erase(requestId);
        return fallbackPromise.get_future();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        pendingPromises_.erase(requestId);
        promise->set_exception(CreateExceptionPtr(K_TRANSPORT_EXCEPTION, e.what()));
    }

    return promise->get_future();
}

std::future<std::vector<AgentCard>> HttpCardResolver::GetAllAgentCards() const
{
    auto outerCardFuture = GetAgentCard(std::nullopt);
    return std::async(std::launch::async,
        [cardFuture = std::move(outerCardFuture)]() mutable -> std::vector<AgentCard> {
            AgentCard card = cardFuture.get();
            return {std::move(card)};
        });
}

void HttpCardResolver::OnTransportEvent(const std::string& requestId, const TransportEvent& event)
{
    std::shared_ptr<std::promise<AgentCard>> promise;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        auto it = pendingPromises_.find(requestId);
        if (it != pendingPromises_.end()) {
            promise = std::move(it->second);
            pendingPromises_.erase(it);
        }
    }

    if (!promise) {
        return;
    }

    // check event type
    if (const TransportError* err = std::get_if<TransportError>(&event)) {
        promise->set_exception(CreateExceptionPtr(err->errorCode, err->errInfo));
    } else if (const AgentCard* card = std::get_if<AgentCard>(&event)) {
        promise->set_value(*card);
    } else {
        // wrong type (i.e. Message, Task)
        promise->set_exception(CreateExceptionPtr(K_INVALID_FORMAT, "invalid response format for GetCard"));
    }
}

std::exception_ptr HttpCardResolver::CreateExceptionPtr(int code, const std::string& msg) const
{
    A2AError error;
    error.code = code;
    error.message = msg;
    return std::make_exception_ptr(std::runtime_error(nlohmann::json(error).dump()));
}

} // namespace A2A::Client