/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "utils_message.h"
#include "uuid.h"

namespace A2A {

Message NewAgentTextMessage(const std::string& text, const std::optional<std::string>& context_id,
                            const std::optional<std::string>& task_id)
{
    TextPart t{.kind = "text", .metadata = std::nullopt, .text = text};
    Part p = t;
    Message m;
    m.role = Role::AGENT;
    m.parts = {p};
    m.messageId = A2A::GenerateUuid();
    m.taskId = task_id;
    m.contextId = context_id;
    return m;
}

Message NewAgentPartsMessage(const std::vector<Part>& parts, const std::optional<std::string>& context_id,
                             const std::optional<std::string>& task_id)
{
    Message m;
    m.role = Role::AGENT;
    m.parts = parts;
    m.messageId = A2A::GenerateUuid();
    m.taskId = task_id;
    m.contextId = context_id;
    return m;
}

std::vector<std::string> GetTextParts(const std::vector<Part>& parts)
{
    std::vector<std::string> out;
    for (const auto& p : parts) {
        if (std::holds_alternative<TextPart>(p)) {
            out.push_back(std::get<TextPart>(p).text);
        }
    }
    return out;
}

std::vector<nlohmann::json> GetDataParts(const std::vector<Part>& parts)
{
    std::vector<nlohmann::json> out;
    for (const auto& p : parts) {
        if (std::holds_alternative<DataPart>(p)) {
            out.push_back(std::get<DataPart>(p).data);
        }
    }
    return out;
}

std::vector<std::variant<FileWithBytes, FileWithUri>> GetFileParts(const std::vector<Part>& parts)
{
    std::vector<std::variant<FileWithBytes, FileWithUri>> out;
    for (const auto& p : parts) {
        if (std::holds_alternative<FilePart>(p)) {
            out.push_back(std::get<FilePart>(p).file);
        }
    }
    return out;
}

std::string GetMessageText(const Message& message, const std::string& delimiter)
{
    std::string joined;
    const auto texts = GetTextParts(message.parts);
    for (size_t i = 0; i < texts.size(); ++i) {
        if (i != 0) {
            joined += delimiter;
        }
        joined += texts[i];
    }
    return joined;
}

} // namespace A2A
