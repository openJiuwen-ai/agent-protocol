/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "uuid.h"
#include "utils_artifact.h"

namespace A2A::Server {

Artifact NewArtifact(const std::vector<Part>& parts, const std::string& name, const std::string& description)
{
    Artifact a;
    a.artifactId = GenerateUuid();
    a.parts = parts;
    a.name = name;
    if (!description.empty()) {
        a.description = description;
    }
    return a;
}

Artifact NewTextArtifact(const std::string& name, const std::string& text, const std::string& description)
{
    TextPart t{.kind = "text", .metadata = std::nullopt, .text = text};
    Part p = t;
    return NewArtifact({p}, name, description);
}

Artifact NewDataArtifact(const std::string& name, const nlohmann::json& data, const std::string& description)
{
    DataPart d{.kind = "data", .metadata = std::nullopt, .data = data};
    Part p = d;
    return NewArtifact({p}, name, description);
}

} // namespace A2A::Server
