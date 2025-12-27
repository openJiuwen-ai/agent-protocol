/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_UTIL_MESSAGE
#define A2A_UTIL_MESSAGE

#include <string>
#include <vector>

#include "utils/types.h"

namespace a2a {

Message NewAgentTextMessage(const std::string& text, const std::optional<std::string>& contextId = std::nullopt,
                            const std::optional<std::string>& taskId = std::nullopt);

Message NewAgentPartsMessage(const std::vector<Part>& parts, const std::optional<std::string>& contextId = std::nullopt,
                             const std::optional<std::string>& taskId = std::nullopt);

std::vector<std::string> GetTextParts(const std::vector<Part>& parts);

std::vector<nlohmann::json> GetDataParts(const std::vector<Part>& parts);

std::vector<std::variant<FileWithBytes, FileWithUri>> GetFileParts(const std::vector<Part>& parts);

std::string GetMessageText(const Message& message, const std::string& delimiter = "\n");

} // namespace a2a

#endif