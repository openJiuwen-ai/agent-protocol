/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <iomanip>

#include "uuid.h"
#include "id_generator.h"

namespace A2A {

std::string UUIDGenerator::Generate(const IDGeneratorContext& ctx)
{
    return GenerateUuid();
}

} // namespace A2A
