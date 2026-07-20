/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_ID_GENERATOR
#define A2A_ID_GENERATOR

#include <optional>
#include <string>

namespace A2A {

struct IDGeneratorContext {
    std::optional<std::string> taskId;
    std::optional<std::string> contextId;
};

// Simple ID generator interface
struct IDGenerator {
    virtual ~IDGenerator() = default;
    virtual std::string Generate(const IDGeneratorContext& ctx) = 0;
};

// UUID-based generator (uses libuuid)
struct UUIDGenerator : public IDGenerator {
    std::string Generate(const IDGeneratorContext& ctx) override;
};

} // namespace A2A

#endif
