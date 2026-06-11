/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <nlohmann/json.hpp>
#include <getopt.h>
#include <iostream>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>

#include "client/client_factory.h"
#include "client/http_card_resolver_builder.h"

constexpr int MAX_PORT = 65535;

void printUsage(const char* program_name)
{
    std::cout << "Usage: " << program_name << " -i <ip> -p <port> [OPTIONS]\n" <<
        "\nRequired options:\n" <<
        "  -i, --ip <ip>        Server IP address to connect to\n" <<
        "  -p, --port <port>    Server port to connect to (1-65535)\n" <<
        "\nOptional:\n" <<
        "  -h, --help           Show this help message\n" <<
        "\nExamples:\n" <<
        "  " << program_name << " -i 127.0.0.1 -p 8080\n" <<
        "  " << program_name << " --ip=192.168.1.100 --port=8090\n" <<
        "  " << program_name << " -i localhost -p 3000\n";
}

int main(int argc, char** argv)
{
    std::string ip;
    int port = 0;
    bool ip_provided = false;
    bool port_provided = false;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

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

    std::cout << "Client configuration:\n" <<
        "  Connecting to: " << ip << ":" << port << "\n\n";

    std::string base_url = std::string("http://") + ip + ":" + std::to_string(port);

    A2A::AgentCard card;
    card.name = "ExampleAgent";
    card.description = "A2A Hello World Example";
    card.version = "1.0.0";
    card.defaultInputModes = {"text"};
    card.defaultOutputModes = {"text"};
    card.capabilities.streaming = false;
    A2A::AgentInterface primaryInterface;
    primaryInterface.url = "http://" + ip + ":" + std::to_string(port) + "/jsonrpc";
    primaryInterface.protocolBinding = "JSONRPC";
    primaryInterface.protocolVersion = "1.0";
    card.supportedInterfaces = {primaryInterface};

    // Test get agent card via HTTP
    try {
        auto builder = std::make_shared<A2A::Client::HttpCardResolverBuilder>();
        auto resolver = builder->Build("http://" + ip + ":" + std::to_string(port), "/.well-known/agent-card.json", {});
        std::cout << "Test: GetAgentCard\n";
        auto card = resolver->GetAgentCard().get();
        std::cout << "Success: " << card.name << "\n";
        std::cout << "AgentCard details:\n" <<
            "  Name: " << card.name << "\n" <<
            "  Description: " << card.description << "\n" <<
            "  URL: " << (card.supportedInterfaces.empty() ? "" : card.supportedInterfaces[0].url) << "\n" <<
            "  Version: " << card.version << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed: " << e.what() << "\n";
        return -1;
    }

    A2A::Client::ClientConfig cfg;
    cfg.streaming = false;
    cfg.supportedTransports = {"JSONRPC"};

    auto client = A2A::Client::ClientFactory::Create(card, cfg);
    if (!client) {
        std::cerr << "Failed to create client" << std::endl;
        return -1;
    }

    // Build a user message with text + data parts
    A2A::Message msg;
    msg.role = A2A::Role::USER;
    msg.messageId = "123";

    A2A::Part tp;
    tp.text = "hello remote server";
    tp.mediaType = "text/plain";
    msg.parts.push_back(tp);

    A2A::Part dp;
    dp.data = nlohmann::json({{"key", "value"}, {"number", 1}}).dump();
    dp.mediaType = "application/json";
    msg.parts.push_back(dp);

    std::cout << "--> Sending to " << card.supportedInterfaces[0].url << std::endl;
    client->SendMessage(msg, nullptr, [&](const A2A::Client::ClientEvent& ev, const A2A::AgentCard& card) {
        std::visit([&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, A2A::Message>) {
                std::cout << "<-- Response: " << std::endl <<
                    "<---- Role: " << (uint32_t)e.role << std::endl <<
                    "<---- messageId: " << e.messageId << std::endl;
            } else if constexpr (std::is_same_v<T, A2A::A2AError>) {
                std::cout << "<-- Error: " << std::endl <<
                    "<---- code: " << e.code << std::endl <<
                    "<---- message: " << e.message.value_or("") << std::endl;
            } else {
                const auto& t = e.first;
                std::cout << "<-- Task: " << std::endl <<
                    "<---- contextId: " << t.contextId << std::endl <<
                    "<---- id: " << t.id << std::endl;
                std::cout << std::endl << std::endl;
            }
        }, ev);
        {
            std::unique_lock<std::mutex> lock(mtx);
            done = true;
        }
        cv.notify_one();
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return done; });
    }
    return 0;
}