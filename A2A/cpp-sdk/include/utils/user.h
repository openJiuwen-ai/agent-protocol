/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_USER
#define A2A_USER

#include <string>

namespace a2a {

class User {
public:
    virtual ~User() = default;
    virtual bool IsAuthenticated() const = 0;
    virtual std::string UserName() const = 0;
};

class UnauthenticatedUser : public User {
public:
    bool IsAuthenticated() const override
    {
        return false;
    }

    std::string UserName() const override
    {
        return "";
    }
};

} // namespace a2a

#endif
