/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 */

#include <gtest/gtest.h>

#define private public
#define protected public
#include "server/http_server_manager.h"
#undef private
#undef protected

using namespace A2A::Server;

namespace {

class HttpServerManagerCoverageTest : public ::testing::Test {
protected:
    static HttpServerManagerConfig MakeConfig(size_t ioThreadNum = 1)
    {
        HttpServerManagerConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = 0;
        cfg.ioThreadNum = ioThreadNum;
        return cfg;
    }
};

TEST_F(HttpServerManagerCoverageTest, ConstructorInitializesFields)
{
    HttpServerManager mgr(MakeConfig(2));

    EXPECT_EQ(mgr.config_.host, "127.0.0.1");
    EXPECT_EQ(mgr.config_.port, 0);
    EXPECT_EQ(mgr.config_.ioThreadNum, 2u);
    EXPECT_FALSE(mgr.running_.load());
    EXPECT_TRUE(mgr.servers_.empty());
}

TEST_F(HttpServerManagerCoverageTest, StartWhenAlreadyRunningReturnsImmediately)
{
    HttpServerManager mgr(MakeConfig(2));
    mgr.running_.store(true);

    mgr.Start();

    EXPECT_TRUE(mgr.running_.load());
    EXPECT_TRUE(mgr.servers_.empty());
}

TEST_F(HttpServerManagerCoverageTest, StartWithZeroIoThreadReturnsWithoutCreatingServers)
{
    HttpServerManager mgr(MakeConfig(0));

    mgr.Start();

    EXPECT_FALSE(mgr.running_.load());
    EXPECT_TRUE(mgr.servers_.empty());
}

TEST_F(HttpServerManagerCoverageTest, StopWhenNotRunningReturnsImmediately)
{
    HttpServerManager mgr(MakeConfig(1));

    mgr.Stop();

    EXPECT_FALSE(mgr.running_.load());
    EXPECT_TRUE(mgr.servers_.empty());
}

TEST_F(HttpServerManagerCoverageTest, StopWhenRunningAndServersExistClearsAllServers)
{
    HttpServerManager mgr(MakeConfig(2));
    mgr.running_.store(true);

    mgr.servers_.push_back(nullptr);
    mgr.servers_.push_back(nullptr);
    ASSERT_EQ(mgr.servers_.size(), 2u);

    mgr.Stop();

    EXPECT_FALSE(mgr.running_.load());
    EXPECT_TRUE(mgr.servers_.empty());
}

TEST_F(HttpServerManagerCoverageTest, StopTwiceIsSafe)
{
    HttpServerManager mgr(MakeConfig(1));
    mgr.running_.store(true);
    mgr.servers_.push_back(nullptr);

    mgr.Stop();
    mgr.Stop();

    EXPECT_FALSE(mgr.running_.load());
    EXPECT_TRUE(mgr.servers_.empty());
}

TEST_F(HttpServerManagerCoverageTest, DestructorCallsStopSafely)
{
    auto mgr = std::make_unique<HttpServerManager>(MakeConfig(1));
    mgr->running_.store(true);
    mgr->servers_.push_back(nullptr);
    mgr->servers_.push_back(nullptr);

    mgr.reset();

    SUCCEED();
}

} // namespace