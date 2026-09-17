/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef A2A_SERVER
#define A2A_SERVER

namespace A2A::Server {

/**
 * @brief Top-level A2A server interface.
 * @note 服务端生命周期：Start() → Stop()。
 */
class Server {
public:
    /** @brief Virtual destructor. */
    virtual ~Server() = default;

    /**
     * @brief Start the server and begin listening for requests.
     * @return 0 on success, non-zero on failure.
     * @throws std::runtime_error if the transport fails to bind or start.
     */
    virtual int Start() = 0;

    /**
     * @brief Stop the server and release listening resources.
     */
    virtual void Stop() = 0;
};

} // namespace A2A::Server

#endif
