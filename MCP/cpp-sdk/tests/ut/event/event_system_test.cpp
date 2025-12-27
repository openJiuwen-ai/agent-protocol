/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gtest/gtest.h>

#include "event/event_system.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <memory>
#include <thread>

using namespace std::chrono_literals;

TEST(EventSystemTest, InitAndGetEventBase) {
	Mcp::EventSystem es;
	ASSERT_TRUE(es.Init());
	ASSERT_NE(es.GetEventBase(), nullptr);
}

TEST(EventSystemTest, AddEventBeforeInit) {
	Mcp::EventSystem es;
	std::atomic<int> called{0};
	auto cb = [&called](int, short, void*) { called.fetch_add(1); };

	// addTimer requires init first -> should fail when not initialized
	int id = es.AddTimer(10, cb, nullptr, false);
	EXPECT_EQ(id, -1);

	// addEvent also fails when not initialized
	id = es.AddEvent(-1, Mcp::EventType::TIMEOUT, cb, nullptr, 10);
	EXPECT_EQ(id, -1);
}

TEST(EventSystemTest, AddTimerOneShot) {
	Mcp::EventSystem es;
	ASSERT_TRUE(es.Init());

	std::mutex m;
	std::condition_variable cv;
	std::atomic<int> calls{0};

	auto cb = [&](int, short, void*) {
		calls.fetch_add(1);
		cv.notify_one();
	};

	int id = es.AddTimer(50, cb, nullptr, false);
	ASSERT_GT(id, 0);

	es.Start(true); // run in background

	{
		std::unique_lock<std::mutex> lk(m);
		EXPECT_TRUE(cv.wait_for(lk, 500ms, [&]() { return calls.load() >= 1; }));
	}

	// allow small grace period to ensure it does not fire again
	std::this_thread::sleep_for(200ms);
	EXPECT_EQ(calls.load(), 1);

	EXPECT_FALSE(es.RemoveEvent(id));

	es.Stop();
}

TEST(EventSystemTest, AddTimerRepeatAndRemove) {
	Mcp::EventSystem es;
	ASSERT_TRUE(es.Init());

	std::mutex m;
	std::condition_variable cv;
	std::atomic<int> calls{0};

	auto cb = [&](int, short, void*) {
		int c = calls.fetch_add(1) + 1;
		if (c >= 3) cv.notify_one();
	};

	int id = es.AddTimer(30, cb, nullptr, true);
	ASSERT_GT(id, 0);

	es.Start(true);

	{
		std::unique_lock<std::mutex> lk(m);
		EXPECT_TRUE(cv.wait_for(lk, 1000ms, [&]() { return calls.load() >= 3; }));
	}

	EXPECT_TRUE(es.RemoveEvent(id));

	int after = calls.load();
	std::this_thread::sleep_for(200ms);

	EXPECT_EQ(after, calls.load());

	es.Stop();
}

TEST(EventSystemTest, InvalidArgs) {
	Mcp::EventSystem es;
	ASSERT_TRUE(es.Init());

	std::atomic<int> called{0};
	auto cb = [&](int, short, void*) { called.fetch_add(1); };

	int id = es.AddEvent(-1, static_cast<Mcp::EventType>(0), cb, nullptr, 10);
	EXPECT_EQ(id, -1);

	id = es.AddEvent(-1, Mcp::EventType::TIMEOUT, nullptr, nullptr, 10);
	EXPECT_EQ(id, -1);

	id = es.AddTimer(0, cb, nullptr, false);
	EXPECT_EQ(id, -1);

	EXPECT_FALSE(es.RemoveEvent(999999));
}

TEST(EventSystemIOTest, ReadEventTriggeredFromSocketpair) {
    int sv[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    Mcp::EventSystem es(true);
    ASSERT_TRUE(es.Init());
    std::atomic<bool> called{false};
    int eventId = es.AddEvent(sv[0],
        Mcp::EventType::READ,
        [&called](int fd, short events, void*){
            called.store(true);
        },
        nullptr);
    ASSERT_NE(-1, eventId);

    es.Start(true); // run in background

    const char payload[] = "x";
    ssize_t n = write(sv[1], payload, sizeof(payload));
    ASSERT_GT(n, 0);

    for (int i = 0; i < 100 && !called.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(called.load());

    es.RemoveEvent(eventId);
    es.Stop();
    close(sv[0]);
    close(sv[1]);
}

TEST(EventSystemTest, StartStopIdempotent) {
	Mcp::EventSystem es(true);
	ASSERT_TRUE(es.Init());

	es.Start(true);
	es.Start(true);

	es.Stop();
	es.Stop();
}

TEST(EventSystemTest, EventFdCreateNotifyReadClose) {
	Mcp::EventSystem es(true);
	ASSERT_TRUE(es.Init());

	std::mutex m;
	std::condition_variable cv;
	std::shared_ptr<std::vector<uint64_t>> values = std::make_shared<std::vector<uint64_t>>();
	std::atomic<int> calls{0};

	auto cb = [&es, values, &m, &cv, &calls](int fd, short events, void* /*arg*/) {
		uint64_t v = 0;
		if (es.ReadEventFd(fd, v)) {
			{
				std::lock_guard<std::mutex> lk(m);
				values->push_back(v);
			}
			calls.fetch_add(1);
			cv.notify_one();
		}
	};

	int notifyEventId = es.CreateNotifyEventId(cb, nullptr, /*persist=*/true);
	ASSERT_GT(notifyEventId, 0);

	es.Start(true);

	ASSERT_TRUE(es.NotifyEventId(notifyEventId, 3));
	std::this_thread::sleep_for(20ms);
	ASSERT_TRUE(es.NotifyEventId(notifyEventId, 1));

	{
		std::unique_lock<std::mutex> lk(m);
		EXPECT_TRUE(cv.wait_for(lk, 1000ms, [&]() { return calls.load() >= 2; }));
	}

	{
		std::lock_guard<std::mutex> lk(m);
		ASSERT_GE(values->size(), 2u);
		EXPECT_EQ(values->at(0), 3u);
		EXPECT_EQ(values->at(1), 1u);
	}

	// cleanup
	EXPECT_TRUE(es.CloseNotifyEventId(notifyEventId));
	es.Stop();
}