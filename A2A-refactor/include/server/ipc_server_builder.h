/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef A2A_IPC_SERVER_BUILDER
#define A2A_IPC_SERVER_BUILDER

#include <memory>

#include "server/server.h"

namespace A2A::Server {

struct IpcConfig {
    std::string elementName;
    std::string rpcCode;
    std::string cardCode;
    std::string extCardCode;
};

class IpcServerBuilder {
public:
    /**
     * @brief destructor
     */
    ~IpcServerBuilder();

    /**
     * @brief create ipc server
     *
     * @param[in] config ipc config
     * @param[in] agentCard agent card object
     * @param[in] extendedAgentCard extended agent card object
     * @param[in] taskStore store object to store task object
     * @return A shared pointer to the created server instance
     */
    static std::shared_ptr<Server> Build(const IpcConfig& config, std::shared_ptr<AgentCard> agentCard,
        std::shared_ptr<AgentCard> extendedAgentCard = nullptr, std::shared_ptr<TaskStore> taskStore = nullptr);
};

}

#endif
