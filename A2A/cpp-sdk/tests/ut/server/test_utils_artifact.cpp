/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "types.h"
#include "utils_artifact.h"

namespace A2A::Server {
Artifact NewArtifact(const std::vector<Part>& parts, const std::string& name, const std::string& description);
Artifact NewTextArtifact(const std::string& name, const std::string& text, const std::string& description);
Artifact NewDataArtifact(const std::string& name, const std::string& data, const std::string& description);
}

using namespace A2A;

namespace {

class UtilsArtifactTest : public ::testing::Test {
protected:
    static Part MakeTextPart(const std::string& text)
    {
        Part p;
        p.text = text;
        p.mediaType = "text/plain";
        return p;
    }
};

TEST_F(UtilsArtifactTest, NewArtifact_WithDescription_SetsAllFields)
{
    std::vector<Part> parts{MakeTextPart("hello")};

    auto artifact = A2A::Server::NewArtifact(parts, "name1", "desc1");

    EXPECT_FALSE(artifact.artifactId.empty());
    EXPECT_TRUE(artifact.name.has_value());
    EXPECT_EQ(*artifact.name, "name1");
    ASSERT_TRUE(artifact.description.has_value());
    EXPECT_EQ(*artifact.description, "desc1");

    ASSERT_EQ(artifact.parts.size(), 1u);
    ASSERT_TRUE(artifact.parts[0].text.has_value());
    EXPECT_EQ(*artifact.parts[0].text, "hello");
}

TEST_F(UtilsArtifactTest, NewArtifact_EmptyDescription_DoesNotSetDescription)
{
    std::vector<Part> parts{MakeTextPart("hello")};

    auto artifact = A2A::Server::NewArtifact(parts, "name2", "");

    EXPECT_FALSE(artifact.artifactId.empty());
    EXPECT_TRUE(artifact.name.has_value());
    EXPECT_EQ(*artifact.name, "name2");
    EXPECT_FALSE(artifact.description.has_value());
}

TEST_F(UtilsArtifactTest, NewTextArtifact_CreatesTextPartCorrectly)
{
    auto artifact = A2A::Server::NewTextArtifact("text-artifact", "content", "desc");

    EXPECT_FALSE(artifact.artifactId.empty());
    EXPECT_TRUE(artifact.name.has_value());
    EXPECT_EQ(*artifact.name, "text-artifact");

    ASSERT_EQ(artifact.parts.size(), 1u);
    const auto& p = artifact.parts[0];

    ASSERT_TRUE(p.text.has_value());
    EXPECT_EQ(*p.text, "content");
    ASSERT_TRUE(p.mediaType.has_value());
    EXPECT_EQ(*p.mediaType, "text/plain");

    ASSERT_TRUE(artifact.description.has_value());
    EXPECT_EQ(*artifact.description, "desc");
}

TEST_F(UtilsArtifactTest, NewTextArtifact_EmptyDescription)
{
    auto artifact = A2A::Server::NewTextArtifact("text-artifact", "content", "");

    EXPECT_FALSE(artifact.artifactId.empty());
    EXPECT_FALSE(artifact.description.has_value());
}

TEST_F(UtilsArtifactTest, NewDataArtifact_CreatesBinaryPartCorrectly)
{
    auto artifact = A2A::Server::NewDataArtifact("data-artifact", "raw-bytes", "desc");

    EXPECT_FALSE(artifact.artifactId.empty());
    EXPECT_TRUE(artifact.name.has_value());
    EXPECT_EQ(*artifact.name, "data-artifact");

    ASSERT_EQ(artifact.parts.size(), 1u);
    const auto& p = artifact.parts[0];

    ASSERT_TRUE(p.data.has_value());
    EXPECT_EQ(std::get<std::string>(*p.data), "raw-bytes");
    ASSERT_TRUE(p.mediaType.has_value());
    EXPECT_EQ(*p.mediaType, "application/octet-stream");

    ASSERT_TRUE(artifact.description.has_value());
    EXPECT_EQ(*artifact.description, "desc");
}

TEST_F(UtilsArtifactTest, NewDataArtifact_EmptyDescription)
{
    auto artifact = A2A::Server::NewDataArtifact("data-artifact", "raw", "");

    EXPECT_FALSE(artifact.description.has_value());
}

} // namespace