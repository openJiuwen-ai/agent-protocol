/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_UTILS_ARTIFACT
#define A2A_UTILS_ARTIFACT

#include <string>
#include <vector>

#include "types.h"

namespace A2A {

Artifact NewArtifact(const std::vector<Part>& parts, const std::string& name, const std::string& description = "");

Artifact NewTextArtifact(const std::string& name, const std::string& text, const std::string& description = "");

Artifact NewDataArtifact(const std::string& name, const nlohmann::json& data, const std::string& description = "");

} // namespace A2A

#endif
