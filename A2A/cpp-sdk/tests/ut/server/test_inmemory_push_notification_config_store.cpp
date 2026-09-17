/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "tasks/inmemory_push_notification_config_store.h"
#include "types.h"

using namespace A2A;
using namespace A2A::Server;

namespace {

class InMemoryPushNotificationConfigStoreTest : public ::testing::Test {
protected:
    InMemoryPushNotificationConfigStore store;

    static PushNotificationConfig MakeConfig(const std::string& url,
                                            const std::optional<std::string>& id = std::nullopt)
    {
        PushNotificationConfig cfg;
        cfg.url = url;
        cfg.id = id;
        return cfg;
    }
};

TEST_F(InMemoryPushNotificationConfigStoreTest, GetInfo_WhenTaskNotExist_ReturnsEmptyVector)
{
    auto result = store.GetInfo("task-1");

    EXPECT_TRUE(result.empty());
}

TEST_F(InMemoryPushNotificationConfigStoreTest, SetInfo_WhenConfigIdMissing_DefaultsToTaskId)
{
    auto cfg = MakeConfig("https://notify.example.com");

    store.SetInfo("task-1", cfg);

    auto result = store.GetInfo("task-1");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(result[0].id.has_value());
    EXPECT_EQ(*result[0].id, "task-1");
    EXPECT_EQ(result[0].url, "https://notify.example.com");
}

TEST_F(InMemoryPushNotificationConfigStoreTest, SetInfo_WhenConfigIdPresent_PreservesId)
{
    auto cfg = MakeConfig("https://notify.example.com", std::string("cfg-1"));

    store.SetInfo("task-1", cfg);

    auto result = store.GetInfo("task-1");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(result[0].id.has_value());
    EXPECT_EQ(*result[0].id, "cfg-1");
    EXPECT_EQ(result[0].url, "https://notify.example.com");
}

TEST_F(InMemoryPushNotificationConfigStoreTest, SetInfo_WhenSameIdExists_ReplacesOldConfig)
{
    store.SetInfo("task-1", MakeConfig("https://old.example.com", std::string("cfg-1")));
    store.SetInfo("task-1", MakeConfig("https://new.example.com", std::string("cfg-1")));

    auto result = store.GetInfo("task-1");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(result[0].id.has_value());
    EXPECT_EQ(*result[0].id, "cfg-1");
    EXPECT_EQ(result[0].url, "https://new.example.com");
}

TEST_F(InMemoryPushNotificationConfigStoreTest, SetInfo_WhenDifferentIdsExist_AppendsConfigs)
{
    store.SetInfo("task-1", MakeConfig("https://notify1.example.com", std::string("cfg-1")));
    store.SetInfo("task-1", MakeConfig("https://notify2.example.com", std::string("cfg-2")));

    auto result = store.GetInfo("task-1");
    ASSERT_EQ(result.size(), 2u);

    EXPECT_TRUE(result[0].id.has_value());
    EXPECT_TRUE(result[1].id.has_value());
    EXPECT_EQ(*result[0].id, "cfg-1");
    EXPECT_EQ(*result[1].id, "cfg-2");
    EXPECT_EQ(result[0].url, "https://notify1.example.com");
    EXPECT_EQ(result[1].url, "https://notify2.example.com");
}

TEST_F(InMemoryPushNotificationConfigStoreTest, GetInfo_ReturnsCopyNotReference)
{
    store.SetInfo("task-1", MakeConfig("https://notify.example.com", std::string("cfg-1")));

    auto result = store.GetInfo("task-1");
    ASSERT_EQ(result.size(), 1u);
    result[0].url = "https://changed.example.com";

    auto result2 = store.GetInfo("task-1");
    ASSERT_EQ(result2.size(), 1u);
    EXPECT_EQ(result2[0].url, "https://notify.example.com");
}

TEST_F(InMemoryPushNotificationConfigStoreTest, DeleteInfo_WhenTaskNotExist_DoesNothing)
{
    EXPECT_NO_THROW(store.DeleteInfo("task-1", std::string("cfg-1")));
    EXPECT_TRUE(store.GetInfo("task-1").empty());
}

TEST_F(InMemoryPushNotificationConfigStoreTest, DeleteInfo_WhenConfigIdMissing_UsesTaskId)
{
    store.SetInfo("task-1", MakeConfig("https://notify.example.com"));

    ASSERT_EQ(store.GetInfo("task-1").size(), 1u);

    store.DeleteInfo("task-1", std::nullopt);

    EXPECT_TRUE(store.GetInfo("task-1").empty());
}

TEST_F(InMemoryPushNotificationConfigStoreTest, DeleteInfo_WhenSpecificConfigExists_RemovesOnlyThatConfig)
{
    store.SetInfo("task-1", MakeConfig("https://notify1.example.com", std::string("cfg-1")));
    store.SetInfo("task-1", MakeConfig("https://notify2.example.com", std::string("cfg-2")));

    store.DeleteInfo("task-1", std::string("cfg-1"));

    auto result = store.GetInfo("task-1");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_TRUE(result[0].id.has_value());
    EXPECT_EQ(*result[0].id, "cfg-2");
    EXPECT_EQ(result[0].url, "https://notify2.example.com");
}

TEST_F(InMemoryPushNotificationConfigStoreTest, DeleteInfo_WhenSpecificConfigNotExist_KeepsOriginalConfigs)
{
    store.SetInfo("task-1", MakeConfig("https://notify1.example.com", std::string("cfg-1")));
    store.SetInfo("task-1", MakeConfig("https://notify2.example.com", std::string("cfg-2")));

    store.DeleteInfo("task-1", std::string("cfg-x"));

    auto result = store.GetInfo("task-1");
    ASSERT_EQ(result.size(), 2u);
}

TEST_F(InMemoryPushNotificationConfigStoreTest, DeleteInfo_WhenLastConfigRemoved_ErasesTaskEntry)
{
    store.SetInfo("task-1", MakeConfig("https://notify.example.com", std::string("cfg-1")));

    store.DeleteInfo("task-1", std::string("cfg-1"));

    EXPECT_TRUE(store.GetInfo("task-1").empty());
}

} // namespace