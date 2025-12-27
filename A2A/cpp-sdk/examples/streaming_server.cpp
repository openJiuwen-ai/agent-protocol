/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cstdlib>
#include <getopt.h>
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>

#include "server/request_handler.h"
#include "server/request_handler_factory.h"
#include "server/server.h"
#include "utils/types.h"
#include "utils/utils_message.h"
#include "utils/uuid.h"

using json = nlohmann::json;

constexpr int MAX_PORT = 65535;

// --------------------- Streaming Orchestrator Executor ---------------------
class OrchestratorAgentExecutor : public a2a::server::AgentExecutor {
public:
    void Execute(a2a::server::RequestContext& context, a2a::server::EventQueue& eventQueue) override
    {
        std::cout << "OrchestratorAgentExecutor::Execute() called\n";

        // 获取请求消息
        const a2a::Message* req = context.Message();
        if (!req) {
            // 错误处理：发 error 事件
            a2a::TaskStatusUpdateEvent err;
            err.status.state = a2a::TaskState::FAILED;
            err.status.message = makeErrorPart("No message in request");
            eventQueue.Enqueue(err);
            eventQueue.TaskDone();
            return;
        }

        // 1. 提取参数
        std::string destination = "Unknown";
        std::string date = "anytime";
        for (const auto& part : req->parts) {
            if (auto* data_part = std::get_if<a2a::DataPart>(&part)) {
                const auto& data = data_part->data;
                destination = data.value("destination", destination);
                date = data.value("date", date);
            }
        }

        // 2. 生成任务 ID（用你平台的 UUID，或 fallback）
        std::string task_id = "task-1";
        std::string ctx_id = a2a::generateUuid();

        // 3. 创建任务
        a2a::Task task;
        task.id = task_id;
        task.contextId = ctx_id;
        task.kind = "task";
        task.status.state = a2a::TaskState::WORKING;
        eventQueue.Enqueue(task);

        // 4. 创建flight info
        a2a::TaskStatusUpdateEvent flight_status;
        flight_status.contextId = ctx_id;
        flight_status.final = false;
        flight_status.status.state = a2a::TaskState::WORKING;
        flight_status.status.message = makeCombinedPart("flight-progress", {{"stage", "calling-flight"}});
        flight_status.taskId = task_id;
        eventQueue.Enqueue(flight_status);

        // 5. 模拟FlightAgent的回复
        json flight_data{
            {"action", "flights/info"}, {"airline", "Air France"}, {"destination", destination}, {"price", "750 USD"}};
        a2a::TaskArtifactUpdateEvent flight_artifact;
        flight_artifact.contextId = ctx_id;
        flight_artifact.taskId = task_id;
        flight_artifact.artifact.artifactId = a2a::generateUuid();
        flight_artifact.artifact.parts.push_back(a2a::TextPart{"text", std::nullopt, flight_data.dump()});
        eventQueue.Enqueue(flight_artifact);

        // 6. 创建weather info
        a2a::TaskStatusUpdateEvent weather_status;
        weather_status.contextId = ctx_id;
        weather_status.taskId = task_id;
        weather_status.final = false;
        weather_status.status.state = a2a::TaskState::WORKING;
        weather_status.status.message = makeCombinedPart("weather-progress", {{"stage", "calling-weather"}});
        eventQueue.Enqueue(weather_status);

        // 7. 模拟WeatherAgent的回复
        json weather_data{
            {"action", "weather/forecast"}, {"city", destination}, {"temperature", "-2°C"}, {"conditions", "Snowy"}};
        a2a::TaskArtifactUpdateEvent weather_artifact;
        weather_artifact.contextId = ctx_id;
        weather_artifact.taskId = task_id;
        weather_artifact.artifact.artifactId = a2a::generateUuid();
        weather_artifact.artifact.parts.push_back(a2a::TextPart{"text", std::nullopt, weather_data.dump()});
        eventQueue.Enqueue(weather_artifact);

        // 8. 最终状态更新
        a2a::TaskStatusUpdateEvent final_status;
        final_status.contextId = ctx_id;
        final_status.taskId = task_id;
        final_status.final = true;
        final_status.status.state = a2a::TaskState::COMPLETED;
        final_status.status.message = makeCombinedPart("done", {{"stage", "aggregate"}});
        eventQueue.Enqueue(final_status);

        // 9. 发出最终任务，附带合并后的消息
        task.status.state = a2a::TaskState::COMPLETED;
        task.status.message = makeCombinedMessage(*req);
        eventQueue.Enqueue(task);

        // 10. 标记任务结束
        eventQueue.TaskDone();
    }

    void Cancel(a2a::server::RequestContext& context, a2a::server::EventQueue& eventQueue) override
    {
        std::cout << "OrchestratorAgentExecutor::Cancel() called\n";
        a2a::TaskStatusUpdateEvent cancel_event;
        cancel_event.status.state = a2a::TaskState::CANCELED;
        eventQueue.Enqueue(cancel_event);
        eventQueue.TaskDone();
    }

private:
    std::optional<a2a::Message> makeCombinedPart(const std::string& tag, const json& payload)
    {
        a2a::DataPart dp;
        dp.data = {{"tag", tag}, {"payload", payload}};
        return a2a::NewAgentPartsMessage({dp});
    }

    std::optional<a2a::Message> makeErrorPart(const std::string& msg)
    {
        a2a::DataPart dp;
        dp.data = {{"error", msg}};
        return a2a::NewAgentPartsMessage({dp});
    }

    std::optional<a2a::Message> makeCombinedMessage(const a2a::Message& input)
    {
        std::string destination = "Unknown";
        std::string date = "anytime";
        for (const auto& part : input.parts) {
            if (auto* data_part = std::get_if<a2a::DataPart>(&part)) {
                const auto& d = data_part->data;
                destination = d.value("destination", destination);
                date = d.value("date", date);
            }
        }

        json combined{
            {"action", "tripPlan"}, {"destination", destination}, {"date", date}, {"summary", "Trip planned."}};

        a2a::DataPart dp;
        dp.data = combined;
        return a2a::NewAgentPartsMessage({dp});
    }
};

// 全局退出标志
static std::atomic<bool> running{true};

void signal_handler(int sig)
{
    std::cout << "\nReceived signal " << sig << ". Shutting down...\n";
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
    auto agentCard = std::make_shared<a2a::AgentCard>();
    agentCard->name = "OrchestratorAgent";
    agentCard->description = "A2A Orchestrator Example";
    agentCard->url = "http://" + ip + ":" + std::to_string(port) + "/stream";
    agentCard->version = "1.0.0";
    agentCard->defaultInputModes = {"text"};
    agentCard->defaultOutputModes = {"text"};
    agentCard->capabilities.streaming = true;

    std::cout << "--Built agent card \n";

    a2a::server::RequestHandlerFactory fac;
    auto orch_handler = fac.Create(orch_executor, agentCard, nullptr);

    // === Orchestrator Server ===
    a2a::server::Server server(a2a::server::SERVER_TRANSPORT_TYPE_HTTP, orch_handler, agentCard);

    a2a::server::ServerConfig config;
    config.type = a2a::server::SERVER_TRANSPORT_TYPE_HTTP;
    auto& httpConfig = std::get<a2a::server::HttpConfig>(config.config);
    httpConfig.ip = ip;
    httpConfig.port = port;

    std::cout << "\nStarting A2A Orchestrator server..." << std::endl;
    int ret = server.Start(config);
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

    std::cout << "Server stopped.\n";
    return 0;
}