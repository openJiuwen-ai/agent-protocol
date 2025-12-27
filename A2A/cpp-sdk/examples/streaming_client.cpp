/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

#include "client/a2a_card_resolver.h"
#include "client/auth_interceptor.h"
#include "client/client_factory.h"
#include "utils/credentials.h"
#include "utils/types.h"

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
    // Prepare auth store and interceptor
    std::cout << "[1] Preparing auth store and interceptor..." << std::endl;
    auto credStore = std::make_shared<a2a::InMemoryContextCredentialStore>();
    const std::string sessionId = "demo-session";
    credStore->SetCredentials(sessionId, "bearerAuth", "demo-token-123");
    a2a::client::AuthInterceptor auth(credStore);

    // Client config
    std::cout << "[2] Configuring and creating client factory..." << std::endl;
    a2a::client::ClientConfig cfg;
    cfg.streaming = true;
    cfg.supportedTransports = {"JSONRPC"};
    a2a::client::ClientFactory factory(cfg);

    std::string base_url = std::string("http://") + ip + ":" + std::to_string(port) + "/";
    a2a::client::A2ACardResolver resolver(base_url);
    auto card = resolver.GetAgentCard();
    auto client = factory.Create(card, {}, {&auth});
    std::cout << "Client created for agent: " << card.name << std::endl;

    // Consumer to log events with card context
    std::cout << "[3] Adding a global event consumer..." << std::endl;
    client->AddEventConsumer([](const a2a::client::ClientEvent& ev, const a2a::AgentCard& card) {
        std::cout << "[consumer] Event for " << card.name << ":" << std::endl;
        std::visit(
            [&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, a2a::Message>) {
                    std::cout << "  Message: " << json(e).dump() << std::endl;
                } else {
                    const auto& t = e.first;
                    const auto& upd = e.second;
                    std::cout << "  Task: " << json(t).dump() << std::endl;
                    if (!std::holds_alternative<std::monostate>(upd)) {
                        if (std::holds_alternative<a2a::TaskStatusUpdateEvent>(upd)) {
                            std::cout << "  Status: " << json(std::get<a2a::TaskStatusUpdateEvent>(upd)).dump()
                                      << std::endl;
                        } else {
                            std::cout << "  Artifact: " << json(std::get<a2a::TaskArtifactUpdateEvent>(upd)).dump()
                                      << std::endl;
                        }
                    }
                }
                std::cout << std::endl << std::endl;
            },
                ev);
    });

    // Build request message from user
    std::cout << "[4] Building request message..." << std::endl;
    a2a::Message m;
    m.role = a2a::Role::USER;
    m.messageId = "77777";

    a2a::DataPart p;
    p.data = json{{"action", "planTrip"}, {"destination", "Paris"}, {"date", "2025-12-25"}};

    m.parts.push_back(p);
    std::cout << "Message: " << json(m).dump() << std::endl;

    // Per-call context with sessionId for credential lookup
    std::cout << "[5] Preparing call context with sessionId..." << std::endl;
    a2a::ClientCallContext ctx;
    ctx.state["sessionId"] = sessionId;

    std::cout << "\n--> [6] Sending streaming request to Orchestrator..." << std::endl;
    client->SendMessage(m, &ctx, [](const a2a::client::ClientEvent& ev, const a2a::AgentCard& card) {
        std::cout << "[callback] Received event:" << std::endl;
        std::visit([&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, a2a::Message>) {
                    std::cout << "  Final Message: " << json(e).dump() << std::endl;
                } else {
                    const auto& t = e.first;
                    const auto& upd = e.second;
                    std::cout << "  Task event: " << json(t).dump() << std::endl;
                    if (!std::holds_alternative<std::monostate>(upd)) {
                        if (std::holds_alternative<a2a::TaskStatusUpdateEvent>(upd)) {
                            std::cout << "  -> status: " << json(std::get<a2a::TaskStatusUpdateEvent>(upd)).dump()
                                      << std::endl;
                        } else {
                            std::cout << "  -> artifact: " << json(std::get<a2a::TaskArtifactUpdateEvent>(upd)).dump()
                                      << std::endl;
                        }
                    }
                    std::cout << std::endl << std::endl;
                }
            },
            ev);
    });

    std::cout << "\nDone." << std::endl;
    return 0;
}
