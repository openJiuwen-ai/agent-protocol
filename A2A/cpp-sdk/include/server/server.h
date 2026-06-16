/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_SERVER
#define A2A_SERVER

namespace A2A::Server {

class Server {
public:
    /**
    * @brief destructor
    */
    virtual ~Server() = default;

    /**
    * @brief start server and listen
    */
    virtual int Start() = 0;

    /**
    * @brief stop server
    */
    virtual void Stop() = 0;
};

} // namespace A2A::Server

#endif