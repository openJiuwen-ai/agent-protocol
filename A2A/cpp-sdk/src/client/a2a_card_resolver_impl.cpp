/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <curl/curl.h>

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "a2a_card_resolver_impl.h"
#include "utils/errors.h"

namespace {
size_t CardResolverWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}
} // namespace

namespace a2a::client {

A2ACardResolverImpl::A2ACardResolverImpl(std::string baseUrl, std::string agentCardPath, std::string localPath)
    : baseUrl_(std::move(baseUrl)), agentCardPath_(std::move(agentCardPath)), localPath_(std::move(localPath))
{
}

A2ACardResolverImpl::~A2ACardResolverImpl() = default;

AgentCard A2ACardResolverImpl::GetAgentCard(const std::optional<std::string>& relativeCardPath, long connectTimeoutMs,
                                            long readTimeoutMs) const
{
    std::string path = relativeCardPath.value_or(agentCardPath_);
    std::string url = baseUrl_;

    if (!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    if (!path.empty() && path.front() != '/') {
        path = "/" + path;
    }

    url += path;

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    std::string out;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CardResolverWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    if (connectTimeoutMs > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs);
    }

    if (readTimeoutMs > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, readTimeoutMs);
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc == CURLE_OPERATION_TIMEDOUT) {
        throw A2AClientTimeoutError("Client Request timed out");
    }

    if (rc != CURLE_OK) {
        throw A2AClientHTTPError(static_cast<int>(HttpStatusCode::ServiceUnavailable), curl_easy_strerror(rc));
    }

    if (status >= static_cast<int>(HttpStatusCode::BadRequest)) {
        throw A2AClientHTTPError(static_cast<int>(status), "HTTP status error");
    }

    try {
        auto j = nlohmann::json::parse(out);
        return j.get<AgentCard>();
    } catch (const nlohmann::json::parse_error& e) {
        throw A2AClientJSONError(e.what());
    }
}

std::vector<AgentCard> A2ACardResolverImpl::GetAllAgentCards(const std::optional<std::string>& localPath) const
{
    // NOT implemented
    return std::vector<AgentCard>();
}

} // namespace a2a::client
