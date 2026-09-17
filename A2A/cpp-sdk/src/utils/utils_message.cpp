/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "utils_message.h"
#include "uuid.h"

namespace A2A {

Message NewAgentTextMessage(const std::string& text, const std::optional<std::string>& contextId,
                            const std::optional<std::string>& taskId)
{
    Part p;
    p.text = text;
    p.mediaType = "text/plain";
    Message m;
    m.role = Role::AGENT;
    m.parts = {p};
    m.messageId = A2A::GenerateUuid();
    m.taskId = taskId;
    m.contextId = contextId;
    return m;
}

Message NewAgentPartsMessage(const std::vector<Part>& parts, const std::optional<std::string>& contextId,
    const std::optional<std::string>& taskId)
{
    Message m;
    m.role = Role::AGENT;
    m.parts = parts;
    m.messageId = A2A::GenerateUuid();
    m.taskId = taskId;
    m.contextId = contextId;
    return m;
}

std::vector<std::string> GetTextParts(const std::vector<Part>& parts)
{
    std::vector<std::string> out;
    for (const auto& p : parts) {
        if (p.text.has_value()) {
            out.push_back(p.text.value());
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