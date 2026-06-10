/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <variant>

#include "client/a2a_card_resolver.h"
#include "client/client_factory.h"
#include "types.h"

using json = nlohmann::json;

constexpr int MAX_PORT = 65535;

void printUsage(const char* program_name)
{
    std::cout << "Usage: " << program_name << " -i <ip> -p <port> [OPTIONS]\n"
              << "\nRequired options:\n"
              << "  -i, --ip <ip>        Server IP address to connect to\n"
              << "  -p, --port <port>    Server port to connect to (1-65535)\n"
              << "\nOptional:\n"
              << "  -h, --help           Show this help message\n"
              << "\nExamples:\n"
              << "  " << program_name << " -i 127.0.0.1 -p 8083\n"
              << "  " << program_name << " --ip=192.168.1.100 --port=9000\n"
              << "  " << program_name << " -i localhost -p 3000\n";
}

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

    std::cout << "Client configuration:\n"
              << "  Connecting to: " << ip << ":" << port << "\n\n";

    std::cout << "--- Streaming Client Demo ---" << std::endl;
    const std::string sessionId = "demo-session";

    // Client config
    std::cout << "[1] Configuring and creating client factory..." << std::endl;

    A2A::AgentCard card;
    card.name = "OrchestratorAgent";
    card.description = "A2A Orchestrator Example";
    card.url = "http://" + ip + ":" + std::to_string(port) + "/jsonrpc";
    card.version = "1.0.0";
    card.defaultInputModes = {"text"};
    card.defaultOutputModes = {"text"};
    card.capabilities.streaming = true;
    card.preferredTransport = A2A::JSONRPC_TRANSPORT;

    A2A::Client::ClientConfig cfg;
    cfg.streaming = true;
    cfg.supportedTransports = {"JSONRPC"};
    auto client = A2A::Client::ClientFactory::Create(card, cfg);

    std::cout << "Client created for agent: " << card.name << std::endl;

    // Consumer to log events with card context
    std::cout << "[2] Adding a global event consumer..." << std::endl;
    client->AddEventConsumer([](const A2A::Client::ClientEvent& ev, const A2A::AgentCard& card) {
        std::cout << "[consumer] Event for " << card.name << ":" << std::endl;
        std::visit(
            [&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, A2A::Message>) {
                    std::cout << "<-- Response: " << std::endl
                              << "<---- kind: " << e.kind << std::endl
                              << "<---- Role: " << (uint32_t)e.role << std::endl
                              << "<---- messageId: " << e.messageId << std::endl;
                } else {
                    const auto& t = e.first;
                    const auto& upd = e.second;
                    std::cout << "<-- Task: " << std::endl
                              << "<---- kind: " << t.kind << std::endl
                              << "<---- contextId: " << t.contextId << std::endl
                              << "<---- id: " << t.id << std::endl;
                    if (!std::holds_alternative<std::monostate>(upd)) {
                        if (std::holds_alternative<A2A::TaskStatusUpdateEvent>(upd)) {
                            auto u = std::get<A2A::TaskStatusUpdateEvent>(upd);
                            std::cout << "<-- Status: " << std::endl
                                      << "<---- kind: " << u.kind << std::endl
                                      << "<---- status: " << static_cast<uint32_t>(u.status.state) << std::endl
                                      << "<---- taskId: " << u.taskId << std::endl;
                        } else {
                            auto u = std::get<A2A::TaskArtifactUpdateEvent>(upd);
                            std::cout << "<-- Status: " << std::endl
                                      << "<---- kind: " << u.kind << std::endl
                                      << "<---- contextId: " << u.contextId << std::endl
                                      << "<---- taskId: " << u.taskId << std::endl;
                        }
                    }
                }
                std::cout << std::endl << std::endl;
            },
            ev);
    });

    // Build request message from user
    std::cout << "[3] Building request message..." << std::endl;
    A2A::Message m;
    m.role = A2A::Role::USER;
    m.messageId = "77777";

    A2A::Part p;
    p.data = json({{"action", "planTrip"}, {"destination", "Paris"}, {"date", "2025-12-25"}}).dump();
    p.mediaType = "application/json";

    m.parts.push_back(p);

    // Per-call context with sessionId for credential lookup
    std::cout << "[4] Preparing call context with sessionId..." << std::endl;
    A2A::ClientCallContext ctx;
    ctx.state["sessionId"] = sessionId;

    std::cout << "\n--> [5] Sending streaming request to Orchestrator..." << std::endl;
    client->SendMessage(m, &ctx, [](const A2A::Client::ClientEvent& ev, const A2A::AgentCard& card) {
        std::cout << "[callback] Received event:" << std::endl;
        std::visit(
            [&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, A2A::Message>) {
                    std::cout << "<-- Response: " << std::endl
                              << "<---- kind: " << e.kind << std::endl
                              << "<---- Role: " << (uint32_t)e.role << std::endl
                              << "<---- messageId: " << e.messageId << std::endl;
                } else {
                    const auto& t = e.first;
                    const auto& upd = e.second;
                    std::cout << "<-- Task: " << std::endl
                              << "<---- kind: " << t.kind << std::endl
                              << "<---- contextId: " << t.contextId << std::endl
                              << "<---- id: " << t.id << std::endl;
                    if (!std::holds_alternative<std::monostate>(upd)) {
                        if (std::holds_alternative<A2A::TaskStatusUpdateEvent>(upd)) {
                            auto u = std::get<A2A::TaskStatusUpdateEvent>(upd);
                            std::cout << "<-- Status: " << std::endl
                                      << "<---- kind: " << u.kind << std::endl
                                      << "<---- status: " << static_cast<uint32_t>(u.status.state) << std::endl
                                      << "<---- taskId: " << u.taskId << std::endl;
                        } else {
                            auto u = std::get<A2A::TaskArtifactUpdateEvent>(upd);
                            std::cout << "<-- Status: " << std::endl
                                      << "<---- kind: " << u.kind << std::endl
                                      << "<---- contextId: " << u.contextId << std::endl
                                      << "<---- taskId: " << u.taskId << std::endl;
                        }
                    }
                    std::cout << std::endl << std::endl;
                }
            },
            ev);
    });

    ::getchar();
    std::cout << "\nDone." << std::endl;
    return 0;
}
