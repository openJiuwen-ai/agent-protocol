/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "id_generator.h"

namespace A2A::Test {

TEST(IdGeneratorTest, UUIDGenerator_ReturnsNonEmptyId)
{
    UUIDGenerator generator;
    IDGeneratorContext ctx;
    ctx.taskId = "task-1";
    ctx.contextId = "ctx-1";

    const std::string id = generator.Generate(ctx);
    EXPECT_FALSE(id.empty());
}

TEST(IdGeneratorTest, UUIDGenerator_GeneratesUniqueIds)
{
    UUIDGenerator generator;
    IDGeneratorContext ctx;

    std::set<std::string> ids;
    for (int i = 0; i < 32; ++i) {
        ids.insert(generator.Generate(ctx));
    }
    EXPECT_EQ(ids.size(), 32u);
}

TEST(IdGeneratorTest, UUIDGenerator_IgnoresContext)
{
    UUIDGenerator generator;

    IDGeneratorContext ctxA;
    ctxA.taskId = "a";
    ctxA.contextId = "1";

    IDGeneratorContext ctxB;
    ctxB.taskId = "b";
    ctxB.contextId = "2";

    const std::string idA = generator.Generate(ctxA);
    const std::string idB = generator.Generate(ctxB);

    EXPECT_FALSE(idA.empty());
    EXPECT_FALSE(idB.empty());
    EXPECT_NE(idA, idB);
}

} // namespace A2A::Test
