/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */
#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace Mcp::HttpClientTestSupport {
// Script event ordering without a live network or timing assumptions.
inline std::vector<std::function<void()>> events;
inline bool stopped = false;
inline bool timerFired = false;
}

namespace Mcp {
class EventSystem {
public:
    bool Init()
    {
        HttpClientTestSupport::stopped = false;
        HttpClientTestSupport::timerFired = false;
        return true;
    }
    void Stop() { HttpClientTestSupport::stopped = true; }
    void Start(bool)
    {
        for (const auto& event : HttpClientTestSupport::events) {
            if (HttpClientTestSupport::stopped) {
                break;
            }
            event();
        }
        if (!HttpClientTestSupport::stopped && timer_) {
            HttpClientTestSupport::timerFired = true;
            timer_(-1, 0, nullptr);
        }
    }
    int AddTimer(int, std::function<void(int, short, void*)> callback, void*, bool)
    {
        timer_ = std::move(callback);
        return 1;
    }
    void RemoveEvent(int) {}

private:
    std::function<void(int, short, void*)> timer_;
};
}
