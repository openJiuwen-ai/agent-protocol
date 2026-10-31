/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "shared/http_common.h"

#include <algorithm>
#include <regex>
#include <charconv>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace Mcp::Http {

namespace {
constexpr std::size_t MAX_STREAMABLE_HTTP_URL_LENGTH = 2048;
constexpr int PORT_MIN = 1;
constexpr int PORT_MAX = 65535;
// Capture groups for: ^(https?)://([^/\s?#:]+)(?::(\d+))?(/[^\s#]*)?$
constexpr std::size_t URL_REGEX_GROUP_HOST = 2;
constexpr std::size_t URL_REGEX_GROUP_PORT = 3;
} // namespace

bool IsValidStreamableHttpEndpoint(const std::string& endpoint, std::string* errorMsg)
{
    auto fail = [errorMsg](const char* message) {
        if (errorMsg != nullptr) {
            *errorMsg = message;
        }
        return false;
    };

    if (endpoint.empty()) {
        return fail("endpoint is empty");
    }
    if (endpoint.find(' ') != std::string::npos) {
        return fail("endpoint contains spaces");
    }
    if (endpoint.length() > MAX_STREAMABLE_HTTP_URL_LENGTH) {
        return fail("url length more than max url length");
    }

    // Scheme and host are required; port and path/query are optional.
    // Host excludes ':' so an optional port is not absorbed into the host.
    // Reject '#' (fragment); query '?' is allowed in the path segment.
    static const std::regex urlRegex(R"(^(https?)://([^/\s?#:]+)(?::(\d+))?(/[^\s#]*)?$)");
    std::smatch matches;
    if (!std::regex_match(endpoint, matches, urlRegex)) {
        return fail("url is not valid");
    }

    if (matches[URL_REGEX_GROUP_HOST].str().empty()) {
        return fail("host is required");
    }

    if (matches[URL_REGEX_GROUP_PORT].matched) {
        try {
            const int port = std::stoi(matches[URL_REGEX_GROUP_PORT].str());
            if (port < PORT_MIN || port > PORT_MAX) {
                return fail("port out of range");
            }
        } catch (const std::exception&) {
            return fail("invalid port");
        }
    }

    return true;
}

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
                         std::size_t& consumedBytes, HttpParseStatus* status)
{
    consumedBytes = 0;
    if (status != nullptr) {
        *status = HttpParseStatus::INCOMPLETE;
    }
    auto invalid = [status]() {
        if (status != nullptr) {
            *status = HttpParseStatus::INVALID;
        }
        return false;
    };

    if (headerEnd == std::string::npos || headerEnd > buffer.size() ||
        buffer.size() - headerEnd < HTTP_HEADER_BODY_SEPARATOR_LENGTH) {
        return false;
    }

    const std::size_t bodyStart = headerEnd + HTTP_HEADER_BODY_SEPARATOR_LENGTH;

    // Parse headers from the header part.
    std::string headerPart = buffer.substr(0, headerEnd);
    std::istringstream headerStream(headerPart);

    std::size_t contentLength = 0;
    bool hasContentLength = false;
    bool hasTransferEncoding = false;
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

        std::string normalizedKey = key;
        std::transform(normalizedKey.begin(), normalizedKey.end(), normalizedKey.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (normalizedKey == "content-length") {
            // Reject duplicates before the header map can overwrite them.
            if (hasContentLength) {
                return invalid();
            }
            hasContentLength = true;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), contentLength);
            if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
                return invalid();
            }
        } else if (normalizedKey == "transfer-encoding") {
            hasTransferEncoding = true;
        }
        headers[normalizedKey] = value;
    }

    if (hasContentLength && hasTransferEncoding) {
        return invalid();
    }

    if (buffer.size() - bodyStart < contentLength) {
        return false;
    }

    body = buffer.substr(bodyStart, contentLength);
    consumedBytes = bodyStart + contentLength;
    if (status != nullptr) {
        *status = HttpParseStatus::COMPLETE;
    }
    return true;
}

std::string getContentType(const HttpResponse& response)
{
    auto contentTypeIter = response.headers.find(CONTENT_TYPE_HEADER);
    if (contentTypeIter == response.headers.end()) {
        return "";
    }
    return contentTypeIter->second;
}

bool parseSseLine(const std::string& line, ServerSentEvent& sseEvent)
{
    // check if end of event
    if (line.empty()) {
        // Each data field contributes a trailing LF; remove only the final one.
        if (!sseEvent.data.empty() && sseEvent.data.back() == '\n') {
            sseEvent.data.pop_back();
        }
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
    } else if (line == "data" || line.find("data:") == 0) {
        std::string dataValue = line == "data" ? "" : line.substr(SSE_DATA_PREFIX_LEN);
        // SSE removes at most one ASCII space after the colon, preserving payload whitespace.
        if (!dataValue.empty() && dataValue.front() == ' ') {
            dataValue.erase(0, 1);
        }
        sseEvent.data += dataValue;
        sseEvent.data.push_back('\n');
    }

    return false;
}

} // namespace Mcp::Http
