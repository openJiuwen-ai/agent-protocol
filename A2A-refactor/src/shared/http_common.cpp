/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "http_common.h"

#include <sstream>
#include <stdexcept>

namespace A2A::Http {

void TrimInPlace(std::string& value)
{
    if (value.empty()) {
        return;
    }

    const auto firstNotSpace = value.find_first_not_of(" \t");
    if (firstNotSpace == std::string::npos) {
        value.clear();
        return;
    }

    value.erase(0, firstNotSpace);

    const auto lastNotSpace = value.find_last_not_of(" \t");
    if (lastNotSpace != std::string::npos && lastNotSpace + 1 < value.size()) {
        value.erase(lastNotSpace + 1);
    }
}

bool ParseHeadersAndBody(const std::string& buffer, std::size_t headerEnd,
                         std::unordered_map<std::string, std::string>& headers, std::string& body,
                         std::size_t& consumedBytes)
{
    consumedBytes = 0;

    if (headerEnd == std::string::npos || headerEnd + HTTP_HEADER_BODY_SEPARATOR_LENGTH > buffer.size()) {
        return false;
    }

    const std::size_t bodyStart = headerEnd + HTTP_HEADER_BODY_SEPARATOR_LENGTH;

    // Parse headers from the header part.
    std::string headerPart = buffer.substr(0, headerEnd);
    std::istringstream headerStream(headerPart);

    std::string headerLine;
    while (std::getline(headerStream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        if (headerLine.empty()) {
            continue;
        }
        std::size_t colonPosition = headerLine.find(':');
        if (colonPosition == std::string::npos) {
            continue;
        }

        std::string key = headerLine.substr(0, colonPosition);
        std::string value = headerLine.substr(colonPosition + 1);

        TrimInPlace(key);
        TrimInPlace(value);

        headers[key] = value;
    }

    std::size_t contentLength = 0;
    auto contentLengthIterator = headers.find(CONTENT_LENGTH_HEADER);
    if (contentLengthIterator != headers.end()) {
        try {
            contentLength = static_cast<std::size_t>(std::stoul(contentLengthIterator->second));
        } catch (...) {
            contentLength = 0;
            return false;
        }
    }

    if (buffer.size() - bodyStart < contentLength) {
        return false;
    }

    body = buffer.substr(bodyStart, contentLength);
    consumedBytes = bodyStart + contentLength;
    return true;
}

std::string GetContentType(const HttpResponse& response)
{
    auto contentTypeIter = response.headers.find(CONTENT_TYPE_HEADER);
    if (contentTypeIter == response.headers.end()) {
        return "";
    }
    return contentTypeIter->second;
}

bool ParseSseLine(const std::string& line, ServerSentEvent& sseEvent)
{
    // check if end of event
    if (line.empty()) {
        if (sseEvent.event.empty() && sseEvent.id.empty() && sseEvent.data.empty() && sseEvent.retry == 0) {
            return false;
        }
        return true;
    }

    // parse line to sseEvent
    if (line.find("event:") == 0) {
        sseEvent.event = line.substr(SSE_EVENT_PREFIX_LEN);
        // Trim leading whitespace
        size_t firstNonSpace = sseEvent.event.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos) {
            sseEvent.event = sseEvent.event.substr(firstNonSpace);
        }
    } else if (line.find("id:") == 0) {
        sseEvent.id = line.substr(SSE_ID_PREFIX_LEN);
        // Trim leading whitespace
        size_t firstNonSpace = sseEvent.id.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos) {
            sseEvent.id = sseEvent.id.substr(firstNonSpace);
        }
    } else if (line.find("data:") == 0) {
        sseEvent.data = line.substr(SSE_DATA_PREFIX_LEN);
        // Trim leading whitespace
        size_t firstNonSpace = sseEvent.data.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos) {
            sseEvent.data = sseEvent.data.substr(firstNonSpace);
        }
    }

    return false;
}

} // namespace A2A::Http
