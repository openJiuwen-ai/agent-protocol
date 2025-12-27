/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <nlohmann/json.hpp>

#include "client/a2a_card_resolver.h"
#include "client/client_factory.h"
#include "utils/utils_message.h"

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

    a2a::client::A2ACardResolver resolver(base_url);
    auto card = resolver.GetAgentCard();
    a2a::client::ClientConfig cfg;
    cfg.streaming = false;
    cfg.supportedTransports = {"JSONRPC"};

    a2a::client::ClientFactory factory(cfg);
    auto client = factory.Create(card);

    // Build a user message with text + data parts
    a2a::Message msg;
    msg.role = a2a::Role::USER;
    msg.messageId = "123";

    a2a::TextPart tp;
    tp.text = "hello remote server";
    msg.parts.push_back(tp);

    a2a::DataPart dp;
    dp.data = nlohmann::json{{"key", "value"}, {"number", 1}};
    msg.parts.push_back(dp);

    std::cout << "--> Sending to " << card.url << std::endl;
    client->SendMessage(msg, nullptr, [&](const a2a::client::ClientEvent& ev, const a2a::AgentCard& card) {
        if (std::holds_alternative<a2a::Message>(ev)) {
            auto m = std::get<a2a::Message>(ev);
            std::cout << "<-- Response: " << nlohmann::json(m).dump() << std::endl;
        } else {
            std::cout << "<-- Unexpected Task variant received (non-streaming config)" << std::endl;
        }
    });
    return 0;
}