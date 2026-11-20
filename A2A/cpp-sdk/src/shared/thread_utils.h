/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_THREAD_UTILS_H
#define A2A_THREAD_UTILS_H

#include <string>

#include "platform_compat.h"

namespace A2A {

/**
 * @brief Set the name of the current thread.
 * @param name The thread name (max 15 characters + null terminator)
 */
inline void SetCurrentThreadName(const std::string& name)
{
    Platform::SetCurrentThreadName(name);
}

} // namespace A2A

#endif // A2A_THREAD_UTILS_H
