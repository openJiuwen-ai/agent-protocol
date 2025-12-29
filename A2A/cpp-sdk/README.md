# a2a_cpp

## 简介

**A2A C++ SDK**是基于agent to agent协议的C++实现，旨在帮助开发者快速构建多智能体协作系统、任务编排系统、AI Agent平台、边缘智能设备通信等场景。[A2A协议规范](https://a2a-protocol.org/v0.3.0/specification/)是一套用于智能体之间通信的标准化协议，定义了任务、消息、时间、智能体能力描述等核心结构。SDK提供统一的数据结构、序列化能力、协议校验机制以及通信接口，让智能体之间的交互更加稳定、高效、可维护。


## 快速开始

### 安装

#### 环境要求
- C++ 17或以上编译器
- CMake >= 3.16
- 操作系统：兼容Linux。


**源代码安装**

#### 安装构建依赖
openssl >= 1.1.1n
openssl-devel >= 1.1.1n
curl >= 8.12.0
curl-devel >= 8.12.0
cpp-httplib >= 0.18.7
nlohmann_json >= 3.11.3

##### openssl和curl
使用对应平台的包管理工具进行安装
Ubuntu/Debian
```bash
sudo apt-get install openssl-lib
sudo apt-get install openssl-devel

sudo apt-get install libcurl
sudo apt-get install libcurl-devel
```

CentOS/RHEL
```bash
sudo yum install openssl-lib
sudo yum install openssl-devel

sudo yum install libcurl
sudo yum install libcurl-devel
```

macOS
```bash
brew install openssl-lib
brew install openssl-devel

brew install libcurl
brew install libcurl-devel
```
其他平台请安装等价包

##### cpp-httplib 和 nlohmann_json
在third_party/third_party.cmake中会尝试先在本地系统查找，查找失败则会尝试从github拉取源代码

#### 编译a2a cpp

```bash
cd code
mkdir build && cd build
cmake ..
make -j $(nproc)
```
编译成功后，会在与code同级目录下生成output目录，包含lib和bin，其中lib为动态库，bin为示例二进制
使用时，讲lib下的动态库放入/usr/lib64下，然后可以执行bin下的二进制
服务端启动后会监听本机的8080端口，启动命令为：
./helloworld_server -i 127.0.0.1 -p 8080

客户端可以连接制定IP和端口的服务端，启动命令为：
./helloworld_client -i 127.0.0.1 -p 8080

成功运行会在客户端控制台输出服务端返回的数据

### 样例

让我们创建一个简单的hello world示例。详细实现可以参考example/helloworld_client.cpp

client代码：
```c++
#include <nlohmann/json.hpp>
#include <iostream>

#include "client/client_factory.h"
#include "client/a2a_card_resolver.h"

int main(int argc, char** argv)
{
    std::string base_url = "your server ip:port";

    a2a::client::A2ACardResolver resolver(base_url);
    auto card = resolver.GetAgentCard();
    a2a::client::ClientConfig cfg;
    cfg.streaming = false;
    cfg.supportedTransports = {"JSONRPC"};

    a2a::client::ClientFactory factory(cfg);
    auto client = factory.Create(card);

    a2a::Message msg;
    msg.role = a2a::Role::USER;
    msg.messageId = 123;

    a2a::TextPart tp;
    tp.text = "hello remote server";
    msg.parts.push_back(tp);

    a2a::DataPart dp;
    dp.data = nlohmann::json {
        {"key", "value"},
        {"number", 456}
    };
    msg.parts.push_back(dp);

    client->SendMessage(msg, nullptr, [&](const a2a::client::ClientEvent& ev, const a2a::AgentCard& card) {
        if (std::holds_alternative<a2a::Message>(ev)) {
            auto m = std::get<a2a::Message>(ev);
            std::cout << "<-- Response: " << nlohmann::json(m).dump(2) << std::endl;
        } else {
            std::cout << "<-- Unexpected Task variant received (non-streaming config)" << std::endl;
        }
    });

    return 0;
}

```

server代码
```c++
#include "server/request_handler.h"
#include "server/request_handler_factory.h"
#include "server/server.h"
#include "utils/types.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <memory>
#include <thread>

class MyAgentExecutor : public a2a::server::AgentExecutor {
public:
    void Execute(a2a::server::RequestContext& context, a2a::server::EventQueue& eventQueue) override
    {
        // 发送处理结果
        a2a::Message response_msg;
        response_msg.messageId = "msg-123";
        response_msg.role = a2a::Role::AGENT;

        a2a::TextPart response_part;
        response_part.text = "Processed: " + user_input;
        response_msg.parts.push_back(response_part);

        eventQueue.Enqueue(response_msg);
        eventQueue.TaskDone();
    }

    void Cancel(a2a::server::RequestContext& context, a2a::server::EventQueue& eventQueue) override
    {
    }
};

int main() {

    // 创建executor
    std::shared_ptr<a2a::server::AgentExecutor> executor = std::make_shared<MyAgentExecutor>();

    // 创建 AgentCard
    auto agentCard = std::make_shared<a2a::AgentCard>();
    agentCard->name = "ExampleAgent";
    agentCard->description = "A2A Hello World Example";
    agentCard->url = "http://localhost:8080/jsonrpc";
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
    httpConfig.ip = "127.0.0.1";
    httpConfig.port = 8080;

    int ret = server.Start(config);
    if (ret != 0) {
        return -1;
    }

    // 简单等待一段时间，然后退出
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));

    return 0;
}

```

## 架构设计

**A2A C++ SDK**采用模块化设计，核心模块包括：

* **SDK接口层**：定义Client、Server、A2ACardResolver、AgentExecutor等类，提供对外结构，帮助用户快速搭建客户端和服务端能力。

* **协议结构层**：定义Message、Task、Event、AgentCard等结构，提供字段校验、默认值处理等功能。

* **传输层**：抽象底层传输，对上层功能实现提供统一接口，便于底层不同传输类型的扩展。

## 功能特性

### **智能体能力获取**
A2A C++ SDK提供根据智能体url获取智能体能力的功能，查询智能体支持的基本信息、支持的功能、认证方式、输入输出模式、所有技能列表

### **智能体之间标准化调用**
A2A C++ SDK支持任务管理、状态同步、多模态数据、实时通信等，让用户可以快速简单的在智能体之间传递消息，进行多智能体协作，完成实时推送与流式交互


## 参与贡献

我们欢迎所有形式的贡献，包括但不限于:
- 提交问题和功能建议
- 改进文档
- 提交代码
- 分享使用经验

## 开源许可证

本项目依据Apache-2.0许可证授权。