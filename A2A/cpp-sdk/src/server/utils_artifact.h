/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_UTILS_ARTIFACT
#define A2A_UTILS_ARTIFACT

#include <string>
#include <vector>

#include "utils/types.h"

namespace a2a {

Artifact NewArtifact(const std::vector<Part>& parts, const std::string& name, const std::string& description = "");

Artifact NewTextArtifact(const std::string& name, const std::string& text, const std::string& description = "");

Artifact NewDataArtifact(const std::string& name, const nlohmann::json& data, const std::string& description = "");

} // namespace a2a

#endif
