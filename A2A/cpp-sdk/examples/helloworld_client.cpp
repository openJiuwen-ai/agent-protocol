/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <nlohmann/json.hpp>

#include "client/a2a_card_resolver.h"
#include "client/client_factory.h"

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
              << "  " << program_name << " -i 127.0.0.1 -p 8080\n"
              << "  " << program_name << " --ip=192.168.1.100 --port=8090\n"
              << "  " << program_name << " -i localhost -p 3000\n";
}

int main(int argc, char** argv)
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

    std::string base_url = std::string("http://") + ip + ":" + std::to_string(port);

    A2A::AgentCard card;
    card.name = "ExampleAgent";
    card.description = "A2A Hello World Example";
    card.url = "http://" + ip + ":" + std::to_string(port) + "/jsonrpc";
    card.version = "1.0.0";
    card.defaultInputModes = {"text"};
    card.defaultOutputModes = {"text"};
    card.capabilities.streaming = false;
    card.preferredTransport = A2A::JSONRPC_TRANSPORT;

    A2A::Client::ClientConfig cfg;
    cfg.streaming = false;
    cfg.supportedTransports = {"JSONRPC"};

    auto client = A2A::Client::ClientFactory::Create(card, cfg);

    // Build a user message with text + data parts
    A2A::Message msg;
    msg.role = A2A::Role::USER;
    msg.messageId = "123";

    A2A::TextPart tp;
    tp.text = "hello remote server";
    msg.parts.push_back(tp);

    A2A::DataPart dp;
    dp.data = nlohmann::json{{"key", "value"}, {"number", 1}};
    msg.parts.push_back(dp);

    std::cout << "--> Sending to " << card.url << std::endl;
    client->SendMessage(msg, nullptr, [&](const A2A::Client::ClientEvent& ev, const A2A::AgentCard& card) {
        if (std::holds_alternative<A2A::Message>(ev)) {
            auto m = std::get<A2A::Message>(ev);
            std::cout << "<-- Response: " << std::endl
                      << "<---- kind: " << m.kind << std::endl
                      << "<---- Role: " << (uint32_t)m.role << std::endl
                      << "<---- messageId: " << m.messageId << std::endl;
        } else {
            std::cout << "<-- Unexpected Task variant received (non-streaming config)" << std::endl;
        }
    });

    ::getchar();
    return 0;
}
