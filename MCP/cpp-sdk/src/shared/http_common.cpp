/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "shared/http_common.h"

#include <sstream>
#include <stdexcept>

namespace Mcp::Http {

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
        }
    }

    if (buffer.size() - bodyStart < contentLength) {
        return false;
    }

    body = buffer.substr(bodyStart, contentLength);
    consumedBytes = bodyStart + contentLength;
    return true;
}

} // namespace Mcp::Http
