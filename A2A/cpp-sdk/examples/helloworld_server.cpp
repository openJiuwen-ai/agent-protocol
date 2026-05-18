/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <atomic>
#include <csignal>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <thread>
#include <server/agent_executor.h>
#include <server/http_server_builder.h>

#include "server/server.h"
#include "types.h"

constexpr auto SIMULATED_SERVER_LATENCY = 300;
constexpr auto MAIN_LOOP_SLEEP_INTERVAL = 100;
constexpr int MAX_PORT = 65535;

// 全局退出标志
static std::atomic<bool> running{true};

void signal_handler(int)
{
    running = false;
}

void printUsage(const char* program_name)
{
    std::cout << "Usage: " << program_name << " -i <ip> -p <port> [OPTIONS]\n"
              << "\nRequired options:\n"
              << "  -i, --ip <ip>        Server IP address to bind to\n"
              << "  -p, --port <port>    Server port to listen on (1-65535)\n"
              << "\nOptional:\n"
              << "  -h, --help           Show this help message\n"
              << "\nExamples:\n"
              << "  " << program_name << " -i 127.0.0.1 -p 8080\n"
              << "  " << program_name << " --ip=0.0.0.0 --port=9000\n"
              << "  " << program_name << " -i 192.168.1.100 -p 3000\n";
}

// 简化 AgentExecutor
class MyAgentExecutor : public A2A::Server::AgentExecutor {
public:
    void Execute(const A2A::Server::RequestContext& context, std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        std::cout << "AgentExecutor::Execute() called\n";

        // 发送初始状态
        taskUpdater->StartWork();

        // 模拟业务处理
        std::this_thread::sleep_for(std::chrono::milliseconds(SIMULATED_SERVER_LATENCY));

        // 发送处理结果
        A2A::Message response_msg;
        response_msg.role = A2A::Role::AGENT;

        // 从请求中提取用户输入
        std::string user_input = "Hello World!";
        if (context.GetMessage()) {
            const auto& msg = context.GetMessage();
            response_msg.messageId = msg->messageId;
            response_msg.contextId = msg->contextId.value_or("");
            for (const auto& part : msg->parts) {
                if (auto* text_part = std::get_if<A2A::TextPart>(&part)) {
                    user_input = text_part->text;
                    break;
                }
            }
        }

        A2A::TextPart response_part;
        response_part.text = "Processed: " + user_input;
        response_msg.parts.push_back(response_part);

        // 发送完成状态
        taskUpdater->Complete(response_msg);
    }

    void Cancel(const A2A::Server::RequestContext& context, std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        std::cout << "AgentExecutor::Cancel() called\n";
        taskUpdater->Cancel();
    }
};

int main(int argc, char* argv[])
{
    std::string ip;
    int port = 0;
    bool ip_provided = false;
    bool port_provided = false;

    static struct option long_options[] = {{"ip", required_argument, 0, 'i'},
                                           {"port", required_argument, 0, 'p'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "i:p:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'i':
                ip = optarg;
                ip_provided = true;
                break;
            case 'p':
                try {
                    port = std::stoi(optarg);
                    port_provided = true;
                    if (port < 1 || port > MAX_PORT) {
                        std::cerr << "Error: Port must be between 1 and 65535\n\n";
                        printUsage(argv[0]);
                        return 1;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error: Invalid port number '" << optarg << "'\n\n";
                    printUsage(argv[0]);
                    return 1;
                }
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            case '?':
                printUsage(argv[0]);
                return 1;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    if (!ip_provided || !port_provided) {
        std::cerr << "Error: Both -i/--ip and -p/--port are required parameters\n\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "Server configuration:\n"
              << "  IP: " << ip << "\n"
              << "  Port: " << port << "\n\n";

    // 1. 阻塞信号
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

    // 2. 创建executor
    auto executor = std::make_shared<MyAgentExecutor>();

    // 3. 创建 AgentCard
    auto agentCard = std::make_shared<A2A::AgentCard>();
    agentCard->name = "ExampleAgent";
    agentCard->description = "A2A Hello World Example";
    agentCard->url = "http://" + ip + ":" + std::to_string(port) + "/jsonrpc";
    agentCard->version = "1.0.0";
    agentCard->defaultInputModes = {"text"};
    agentCard->defaultOutputModes = {"text"};
    agentCard->capabilities.streaming = false;
    agentCard->preferredTransport = A2A::JSONRPC_TRANSPORT;

    std::cout << "--Built agent card \n";

    // 4. 创建HTTP服务器配置
    A2A::Server::HttpConfig httpConfig;
    httpConfig.ip = ip;
    httpConfig.port = port;

    // 5. 使用HTTP服务器构建器创建服务器
    auto server = A2A::Server::HttpServerBuilder::Build(
        httpConfig,
        agentCard,
        nullptr,  // extendedAgentCard
        executor,  // agentExecutor
        nullptr   // taskStore
    );

    std::cout << "\nStarting A2A Hello World server..." << std::endl;
    int ret = server->Start();
    if (ret != 0) {
        std::cerr << "Server failed to start\n";
        return 1;
    }

    std::cout << "Hello World server running on " << ip << ":" << port << "\n";
    std::cout << "Press Ctrl+C to stop\n";

    // 用 sigwait 同步等待信号
    int sig = 0;
    int result = sigwait(&sigset, &sig); // ← 阻塞直到收到 SIGINT/SIGTERM
    if (result == 0) {
        std::cout << "\nReceived signal " << sig << ". Shutting down...\n";
    }

    // 停止服务器
    server->Stop();

    std::cout << "Server stopped.\n";
    return 0;
}