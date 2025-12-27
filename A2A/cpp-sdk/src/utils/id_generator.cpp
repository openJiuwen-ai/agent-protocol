/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <iomanip>

#include "utils/id_generator.h"
#include "utils/uuid.h"

namespace a2a {

std::string UUIDGenerator::Generate(const IDGeneratorContext& ctx)
{
    return generateUuid();
}

} // namespace a2a
