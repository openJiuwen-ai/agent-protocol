/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <thread>

#include "server/request_handler.h"
#include "server/request_handler_factory.h"
#include "server/server.h"
#include "utils/types.h"

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
class MyAgentExecutor : public a2a::server::AgentExecutor {
public:
    void Execute(a2a::server::RequestContext& context, a2a::server::EventQueue& eventQueue) override
    {
        std::cout << "AgentExecutor::Execute() called\n";

        // 发送初始状态
        a2a::TaskStatusUpdateEvent status_event;
        status_event.status.state = a2a::TaskState::WORKING;
        eventQueue.Enqueue(status_event);

        // 模拟业务处理
        std::this_thread::sleep_for(std::chrono::milliseconds(SIMULATED_SERVER_LATENCY));

        // 发送处理结果
        a2a::Message response_msg;
        response_msg.role = a2a::Role::AGENT;

        // 从请求中提取用户输入
        std::string user_input = "Hello World!";
        if (context.Message()) {
            const auto& msg = context.Message();
            response_msg.messageId = msg->messageId;
            response_msg.contextId = msg->contextId.value_or("");
            for (const auto& part : msg->parts) {
                if (auto* text_part = std::get_if<a2a::TextPart>(&part)) {
                    user_input = text_part->text;
                    break;
                }
            }
        }

        a2a::TextPart response_part;
        response_part.text = "Processed: " + user_input;
        response_msg.parts.push_back(response_part);

        eventQueue.Enqueue(response_msg);

        // 发送完成状态
        status_event.status.state = a2a::TaskState::COMPLETED;
        eventQueue.Enqueue(status_event);

        eventQueue.TaskDone();
    }

    void Cancel(a2a::server::RequestContext& context, a2a::server::EventQueue& eventQueue) override
    {
        std::cout << "AgentExecutor::Cancel() called\n";
        a2a::TaskStatusUpdateEvent status_event;
        status_event.status.state = a2a::TaskState::CANCELED;
        eventQueue.Enqueue(status_event);
        eventQueue.TaskDone();
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

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 创建executor
    std::shared_ptr<a2a::server::AgentExecutor> executor = std::make_shared<MyAgentExecutor>();

    // 创建 AgentCard
    auto agentCard = std::make_shared<a2a::AgentCard>();
    agentCard->name = "ExampleAgent";
    agentCard->description = "A2A Hello World Example";
    agentCard->url = "http://" + ip + ":" + std::to_string(port) + "/jsonrpc";
    agentCard->version = "1.0.0";
    agentCard->defaultInputModes = {"text"};
    agentCard->defaultOutputModes = {"text"};
    agentCard->capabilities.streaming = false;

    // 创建handler
    a2a::server::RequestHandlerFactory fac;
    auto handler = fac.Create(executor, agentCard, nullptr);

    a2a::server::Server server(a2a::server::SERVER_TRANSPORT_TYPE_HTTP, handler, agentCard);

    // 启动服务器
    a2a::server::ServerConfig config;
    config.type = a2a::server::SERVER_TRANSPORT_TYPE_HTTP;
    auto& httpConfig = std::get<a2a::server::HttpConfig>(config.config);
    httpConfig.ip = ip;
    httpConfig.port = port;

    std::cout << "\nStarting A2A Hello World server..." << std::endl;
    int ret = server.Start(config);
    if (ret != 0) {
        std::cerr << "Server failed to start\n";
        return 1;
    }

    std::cout << "Hello World server running on " << ip << ":" << port << "\n";

    // 阻塞主线程
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(MAIN_LOOP_SLEEP_INTERVAL));
    }

    std::cout << "Server stopped.\n";
    return 0;
}