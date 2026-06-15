/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <sstream>
#include <stdexcept>

#include "http_common.h"

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
    std::string contentLengthStr = GetHeaderValue(headers, CONTENT_LENGTH_HEADER);
    if (!contentLengthStr.empty()) {
        try {
            contentLength = static_cast<std::size_t>(std::stoul(contentLengthStr));
        } catch (...) {
            contentLength = 0;
        }
    }

    if (buffer.size() - bodyStart < contentLength) {
        return false;
    }

    body = buffer.substr(bodyStart, contentLength);
    consumedBytes = bodyStart + contentLength;
    return true;
}

std::string GetHeaderValue(const HttpResponse& response, const std::string& headerName)
{
    return GetHeaderValue(response.headers, headerName);
}

std::string GetHeaderValue(const std::unordered_map<std::string, std::string>& headers, const std::string& headerName)
{
    auto iter = FindHeaderCaseInsensitive(headers, headerName);
    if (iter == headers.end()) {
        return "";
    }
    return iter->second;
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
        std::string dataValue = line.substr(SSE_DATA_PREFIX_LEN);
        // Trim leading whitespace
        size_t firstNonSpace = dataValue.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos) {
            dataValue = dataValue.substr(firstNonSpace);
        }
        // 追加到现有数据（SSE规范要求用换行符连接多行数据）
        if (sseEvent.data.empty()) {
            sseEvent.data = dataValue;
        } else {
            sseEvent.data += "\n" + dataValue;
        }
    }

    return false;
}

std::unordered_map<std::string, std::string>::const_iterator FindHeaderCaseInsensitive(
    const std::unordered_map<std::string, std::string>& headers, const std::string& headerName)
{
    // 先尝试精确匹配
    auto it = headers.find(headerName);
    if (it != headers.end()) {
        return it;
    }

    // 如果没找到，尝试小写版本
    std::string lowerHeaderName = headerName;
    std::transform(lowerHeaderName.begin(), lowerHeaderName.end(), lowerHeaderName.begin(), ::tolower);

    for (auto iter = headers.begin(); iter != headers.end(); ++iter) {
        std::string keyLower = iter->first;
        std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
        if (keyLower == lowerHeaderName) {
            return iter;
        }
    }

    return headers.end();
}


} // namespace A2A::Http