/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <curl/curl.h>

#include <sstream>
#include <stdexcept>

#include "http_client_transport.h"
#include "utils/errors.h"

namespace {
size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

struct StreamCtx {
    std::string buffer;
    a2a::transport::ClientTransportEventHandler onEvent;
};

// Very simple SSE parser: collects lines until blank line, then treats accumulated 'data:' lines as a JSON string.
size_t SseWriteCb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* ctx = static_cast<StreamCtx*>(userdata);
    const size_t n = size * nmemb;
    ctx->buffer.append(ptr, n);
    size_t pos = 0;
    while (true) {
        auto nl = ctx->buffer.find('\n', pos);
        if (nl == std::string::npos) {
            break;
        }

        std::string line = ctx->buffer.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        static thread_local std::string eventData;
        if (line.rfind("data:", 0) == 0) {
            auto payload = line.substr(5);
            if (!payload.empty() && payload[0] == ' ') {
                payload.erase(0, 1);
            }

            if (!eventData.empty()) {
                eventData.push_back('\n');
            }
            eventData += payload;
        } else if (line.empty()) {
            if (!eventData.empty()) {
                ctx->onEvent(eventData);
                eventData.clear();
            }
        }
    }
    // keep only the remainder
    ctx->buffer.erase(0, pos);
    return n;
}

} // namespace

namespace a2a::transport {

HttpClientTransport::HttpClientTransport(std::string url) : url_(std::move(url))
{
}

HttpClientTransport& HttpClientTransport::SetHeader(std::string key, std::string value)
{
    headers_[std::move(key)] = std::move(value);
    return *this;
}

HttpClientTransport& HttpClientTransport::SetBearerToken(std::string token)
{
    bearerToken_ = std::move(token);
    return *this;
}

HttpClientTransport& HttpClientTransport::SetTimeoutMs(long connectMs, long readMs)
{
    connectTimeoutMs_ = connectMs;
    readTimeoutMs_ = readMs;
    return *this;
}

std::string HttpClientTransport::SendData(const std::string& data) const
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    std::string out;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // Static headers
    for (const auto& kv : headers_) {
        std::string h = kv.first + ": " + kv.second;
        headers = curl_slist_append(headers, h.c_str());
    }

    if (bearerToken_) {
        std::string h = "Authorization: Bearer " + *bearerToken_;
        headers = curl_slist_append(headers, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    // Timeouts
    if (connectTimeoutMs_ > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs_);
    }

    if (readTimeoutMs_ > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, readTimeoutMs_);
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc == CURLE_OPERATION_TIMEDOUT) {
        throw a2a::A2AClientTimeoutError("Client Request timed out");
    }
    if (rc != CURLE_OK) {
        std::ostringstream oss;
        oss << "Network communication error: " << curl_easy_strerror(rc);
        throw a2a::A2AClientHTTPError(static_cast<int>(HttpStatusCode::ServiceUnavailable),
            oss.str());
    }
    if (status >= static_cast<int>(HttpStatusCode::BadRequest)) {
        std::ostringstream oss;
        oss << "HTTP status " << status;
        throw a2a::A2AClientHTTPError(static_cast<int>(status), oss.str());
    }
    return out;
}

void HttpClientTransport::SendDataStreaming(const std::string& data, const ClientTransportEventHandler& onEvent) const
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    struct curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");
    // Disable Expect: 100-continue to avoid proxies interfering with SSE
    hdr = curl_slist_append(hdr, "Expect:");
    for (const auto& kv : headers_) {
        std::string h = kv.first + ": " + kv.second;
        hdr = curl_slist_append(hdr, h.c_str());
    }

    if (bearerToken_) {
        std::string h = "Authorization: Bearer " + *bearerToken_;
        hdr = curl_slist_append(hdr, h.c_str());
    }
    // Expect SSE
    hdr = curl_slist_append(hdr, "Accept: text/event-stream");

    StreamCtx ctx{"", onEvent};

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SseWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    // For SSE, length is unknown; ignore any Content-Length mismatches to avoid CURLE_PARTIAL_FILE on early close
    curl_easy_setopt(curl, CURLOPT_IGNORE_CONTENT_LENGTH, 1L);
    // Ensure HTTP/1.1 for chunked SSE
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    if (connectTimeoutMs_ > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connectTimeoutMs_);
    }

    if (readTimeoutMs_ > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, readTimeoutMs_);
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);

    if (rc == CURLE_OPERATION_TIMEDOUT) {
        throw A2AClientTimeoutError("Client Request timed out");
    }

    // Treat PARTIAL_FILE as normal end-of-stream for SSE
    if (rc != CURLE_OK && rc != CURLE_PARTIAL_FILE) {
        throw A2AClientHTTPError(static_cast<int>(HttpStatusCode::ServiceUnavailable),
            curl_easy_strerror(rc));
    }

    if (status >= static_cast<int>(HttpStatusCode::BadRequest)) {
        throw A2AClientHTTPError(static_cast<int>(status), "HTTP status error");
    }
}

} // namespace a2a::transport
