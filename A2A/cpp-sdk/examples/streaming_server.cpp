/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <atomic>
#include <csignal>
#include <nlohmann/json.hpp>
#include <server/agent_executor.h>
#include <server/task_updater.h>
#include <server/http_server_builder.h>

#include "server/server.h"
#include "types.h"
#include "utils/utils_message.h"

using json = nlohmann::json;

constexpr int MAX_PORT = 65535;

// --------------------- Streaming Orchestrator Executor ---------------------
class OrchestratorAgentExecutor : public A2A::Server::AgentExecutor {
public:
    void Execute(const A2A::Server::RequestContext& context,
        std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        std::cout << "OrchestratorAgentExecutor::Execute() called\n";

        // 获取请求消息
        const A2A::Message* req = context.GetMessage();
        if (!req) {
            // 错误处理：发 error 事件
            const auto errMsg = makeErrorPart("No message in request");
            taskUpdater->Failed(errMsg);
            return;
        }

        // 1. 提取参数
        std::string destination = "Unknown";
        std::string date = "anytime";
        for (const auto& part : req->parts) {
            if (part.data.has_value()) {
                const auto& d = part.data.value();
                nlohmann::json j = nlohmann::json::parse(d);
                destination = j.value("destination", destination);
                date = j.value("date", date);
            }
        }

        // 2. 生成任务 ID（用你平台的 UUID，或 fallback）--在RequestHandler的实现中执行
        // 3. 创建任务 --在RequestHandler的实现中执行
        // 4. 创建flight info
        const auto flight_status_msg = makeCombinedPart("flight-progress", {{"stage", "calling-flight"}});
        taskUpdater->StartWork(flight_status_msg);

        // 5. 模拟FlightAgent的回复
        const json flight_data{
            {"action", "flights/info"}, {"airline", "Air France"}, {"destination", destination}, {"price", "750 USD"}};
        std::vector<A2A::Part> flight_artifact_parts;
        A2A::Part flight_data_part;
        flight_data_part.text = flight_data.dump();
        flight_data_part.mediaType = "text/plain";
        flight_artifact_parts.emplace_back(flight_data_part);

        A2A::Server::TaskArtifactParam flight_artifact_param;
        flight_artifact_param.parts = flight_artifact_parts;
        taskUpdater->AddArtifact(flight_artifact_param);

        // 6. 创建weather info
        const auto weather_status_msg = makeCombinedPart("weather-progress", {{"stage", "calling-weather"}});
        taskUpdater->StartWork(weather_status_msg);

        // 7. 模拟WeatherAgent的回复
        json weather_data{
            {"action", "weather/forecast"}, {"city", destination}, {"temperature", "-2°C"}, {"conditions", "Snowy"}};
        std::vector<A2A::Part> weather_artifact_parts;
        A2A::Part weather_data_part;
        weather_data_part.text = weather_data.dump();
        weather_data_part.mediaType = "text/plain";
        weather_artifact_parts.emplace_back(weather_data_part);

        A2A::Server::TaskArtifactParam weather_artifact_param;
        weather_artifact_param.parts = weather_artifact_parts;
        taskUpdater->AddArtifact(weather_artifact_param);

        // 8. 最终状态更新
        const auto finish_message = makeCombinedPart("done", {{"stage", "aggregate"}});
        taskUpdater->StartWork(finish_message);

        // 9. 发出最终任务，附带合并后的消息
        const auto merged_req = makeCombinedMessage(*req);
        taskUpdater->Complete(merged_req);
    }

    void Cancel(const A2A::Server::RequestContext& context,
        std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        std::cout << "OrchestratorAgentExecutor::Cancel() called\n";
        taskUpdater->Cancel();
    }

private:
    std::optional<A2A::Message> makeCombinedPart(const std::string& tag, const json& payload)
    {
        A2A::Part dp;
        dp.data = nlohmann::json({{"tag", tag}, {"payload", payload}}).dump();
        dp.mediaType = "application/json";
        return A2A::NewAgentPartsMessage({dp});
    }

    std::optional<A2A::Message> makeErrorPart(const std::string& msg)
    {
        A2A::Part dp;
        dp.data = nlohmann::json({{"error", msg}}).dump();
        dp.mediaType = "application/json";
        return A2A::NewAgentPartsMessage({dp});
    }

    std::optional<A2A::Message> makeCombinedMessage(const A2A::Message& input)
    {
        std::string destination = "Unknown";
        std::string date = "anytime";
        for (const auto& part : input.parts) {
            if (part.data.has_value()) {
                const auto& d = part.data.value();
                nlohmann::json j = nlohmann::json::parse(d);
                destination = j.value("destination", destination);
                date = j.value("date", date);
            }
        }

        json combined{
            {"action", "tripPlan"}, {"destination", destination}, {"date", date}, {"summary", "Trip planned."}};

        A2A::Part dp;
        dp.data = combined.dump();
        dp.mediaType = "application/json";
        return A2A::NewAgentPartsMessage({dp});
    }
};

// 全局退出标志
static std::atomic<bool> g_running{true};

void signal_handler(int sig)
{
    std::cout << "\nReceived signal " << sig << ". Shutting down...\n";
    g_running = false;
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
              << "  " << program_name << " -i 127.0.0.1 -p 8083\n"
              << "  " << program_name << " --ip=0.0.0.0 --port=9000\n"
              << "  " << program_name << " -i 192.168.1.100 -p 3000\n";
}

// ========================
// Main
// ========================
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
    auto orch_executor = std::make_shared<OrchestratorAgentExecutor>();

    // 3. 创建 AgentCard
    auto agentCard = std::make_shared<A2A::AgentCard>();
    agentCard->name = "OrchestratorAgent";
    agentCard->description = "A2A Orchestrator Example";
    agentCard->url = "http://" + ip + ":" + std::to_string(port) + "/jsonrpc";
    agentCard->version = "1.0.0";
    agentCard->defaultInputModes = {"text"};
    agentCard->defaultOutputModes = {"text"};
    agentCard->capabilities.streaming = true;
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
        orch_executor,  // agentExecutor
        nullptr   // taskStore
    );

    std::cout << "\nStarting A2A Orchestrator server..." << std::endl;
    int ret = server->Start();
    if (ret != 0) {
        std::cerr << "Server failed to start\n";
        return 1;
    }

    std::cout << "Orchestrator server running on " << ip << ":" << port << "\n";
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