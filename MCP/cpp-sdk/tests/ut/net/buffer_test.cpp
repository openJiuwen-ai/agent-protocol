/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cstring>
#include <unistd.h>

#include <gtest/gtest.h>

#include "net/tcp_socket.h"

using namespace Mcp::Net;

TEST(BufferTest, AppendRetrieveCycle) {
    Buffer buf(8);
    const std::string prefix = "abcd";
    buf.Append(prefix);
    EXPECT_EQ(buf.ReadableBytes(), prefix.size());
    EXPECT_EQ(std::string(buf.Peek(), buf.ReadableBytes()), prefix);

    buf.Retrieve(2);
    EXPECT_EQ(buf.ReadableBytes(), prefix.size() - 2);

    const std::string suffix = "efgh";
    buf.Append(suffix);
    EXPECT_EQ(buf.ReadableBytes(), prefix.size() - 2 + suffix.size());
    std::string expected = "cd";
    expected += suffix;
    EXPECT_EQ(std::string(buf.Peek(), buf.ReadableBytes()), expected);

    buf.RetrieveAll();
    EXPECT_EQ(buf.ReadableBytes(), 0);
}

TEST(BufferTest, ReadFdPopsFromPipe) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    const char payload[] = "net-buffer";
    ASSERT_EQ(::write(fds[1], payload, sizeof(payload) - 1), sizeof(payload) - 1);

    Buffer buf(4);
    ssize_t n = buf.ReadFd(fds[0]);
    EXPECT_EQ(n, static_cast<ssize_t>(sizeof(payload) - 1));
    EXPECT_EQ(std::string(buf.Peek(), buf.ReadableBytes()), std::string(payload, sizeof(payload) - 1));

    buf.RetrieveAll();
    EXPECT_EQ(buf.ReadableBytes(), 0);

    ::close(fds[1]);
    ::close(fds[0]);
}
