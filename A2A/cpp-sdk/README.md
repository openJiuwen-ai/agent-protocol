# a2a_cpp

## 简介

**A2A C++ SDK**是基于agent to agent协议的C++实现，旨在帮助开发者快速构建多智能体协作系统、任务编排系统、AI Agent平台、边缘智能设备通信等场景。
[A2A协议规范](https://a2a-protocol.org/v0.3.0/specification/)是一套用于智能体之间通信的标准化协议，定义了任务、消息、时间、智能体能力描述等核心结构。
SDK提供统一的数据结构、序列化能力、协议校验机制以及通信接口，让智能体之间的交互更加稳定、高效、可维护。


## 编译宏说明

A2A C++ SDK 在构建过程中会使用若干编译宏（`-D`），用于控制构建行为、启用安全加固以及适配不同平台环境。以下为当前使用的主要编译宏说明。

| 宏名称 | 来源 | 作用 | 说明 |
|------|------|------|------|
| `NDEBUG` | Release / MinSizeRel 构建类型 | 关闭断言 | 标识当前为非调试构建，`assert` 宏将被禁用 |
| `_FORTIFY_SOURCE=2` | Release / RelWithDebInfo 构建类型 | 安全加固 | 启用 libc 边界检查增强，提升运行时安全性 |
| `__MUSL__` | 平台工具链 | musl libc 标识 | 由 musl 平台工具链自动定义，用于区分 glibc 与 musl 环境 |

## 快速开始

### 安装

#### 环境要求
- C++ 17或以上编译器
- CMake >= 3.16
- 操作系统：兼容Linux


**源代码安装**

#### 安装构建依赖

在 Debian/Ubuntu、RHEL 系等 Linux 上可一键安装系统包（推荐，可加快首次编译）：

```bash
bash ./scripts/install_deps.sh
```

也可手动安装以下依赖：
- curl >= 8.8.0
- curl-devel >= 8.8.0
- nlohmann_json >= 3.11.2
- libevent >= 2.1.12
- http_parser >= 2.9.4

##### curl
使用对应平台的包管理工具进行安装
Ubuntu/Debian
```bash
sudo apt-get install libcurl
sudo apt-get install libcurl-devel
```

CentOS/RHEL
```bash
sudo yum install libcurl
sudo yum install libcurl-devel
```

其他 Linux 发行版请安装等价包

##### libevent 和 nlohmann_json
在 `third_party/third_party.cmake` 中会尝试先在本地系统查找，查找失败则会从 GitHub 拉取源代码。

##### http_parser
与 blue 一致：仅使用 `third_party/http_parser-src`（不存在时由 FetchContent 下载 v2.9.4），不从系统 `find_library` 链接。

#### 编译a2a cpp

```bash
bash ./scripts/build.sh -e
```

编译成功后，会在项目根目录生成 `output/`，包含 `include`、`lib` 和 `bin`（头文件、动态库与示例二进制）。

运行示例冒烟测试（需先完成 `-e` 编译）：

```bash
bash ./scripts/run_example.sh
```

手动运行示例时，将 `lib` 加入动态库搜索路径，例如：

```bash
export LD_LIBRARY_PATH="$(pwd)/output/lib:${LD_LIBRARY_PATH:-}"
```

服务端启动后会监听本机指定端口，例如：

```bash
./output/bin/helloworld_server -i 127.0.0.1 -p 8080
./output/bin/helloworld_client -i 127.0.0.1 -p 8080
```

成功运行会在客户端控制台输出服务端返回的数据

### 样例

让我们创建一个简单的hello world示例。详细实现可以参考example/helloworld_client.cpp

client代码：
```c++
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>

#include "client/http_card_resolver_builder.h"
#include "client/a2a_card_resolver.h"
#include "client/client_factory.h"

int main(int argc, char** argv)
{
    std::string baseUrl = "127.0.0.1:8080";
    std::string cardpath = "/.well-known/agent-card.json";
    std::shared_ptr<A2A::Client::A2ACardResolver> resolver =
        A2A::Client::HttpCardResolverBuilder::Build(baseUrl, cardpath);
    auto card = resolver->GetAgentCard();

    A2A::Client::ClientConfig cfg;
    cfg.streaming = false;
    cfg.supportedTransports = {"JSONRPC"};
    auto client = A2A::Client::ClientFactory::Create(card.get(), cfg);

    A2A::Message msg;
    msg.role = A2A::Role::USER;
    msg.messageId = 123;

    A2A::Part tp;
    tp.text = "hello remote server";
    tp.mediaType = "text/plain";
    msg.parts.push_back(tp);

    A2A::Part dp;
    dp.data = nlohmann::json {
        {"key", "value"},
        {"number", 456}
    };
    dp.mediaType = "application/json";
    msg.parts.push_back(dp);

    client->SendMessage(msg, nullptr, [&](const A2A::Client::ClientEvent& ev, const A2A::AgentCard& card) {
        if (std::holds_alternative<A2A::Message>(ev)) {
            auto m = std::get<A2A::Message>(ev);
            std::cout << "receive response, message id: " << m.messageId << std::endl;
        } else {
            std::cout << "Unexpected Task variant received (non-streaming config)" << std::endl;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    return 0;
}

```

server代码
```c++
#include <csignal>
#include <iostream>
#include <thread>

#include "server/agent_executor.h"
#include "server/http_server_builder.h"
#include "server/server.h"
#include "types.h"

class MyAgentExecutor : public A2A::Server::AgentExecutor {
public:
    void Execute(const A2A::Server::RequestContext& context,
        std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        std::cout << "AgentExecutor::Execute() called\n";

        // 发送初始状态
        taskUpdater->StartWork();

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
                if (part.text.has_value()) {
                    user_input = part.text.value();
                    break;
                }
            }
        }

        A2A::Part text_part;
        text_part.text = "Processed: " + user_input;
        text_part.mediaType = "text/plain";
        response_msg.parts.push_back(text_part);

        // 发送完成状态
        taskUpdater->Complete(response_msg);
    }

    void Cancel(const A2A::Server::RequestContext& context,
        std::shared_ptr<A2A::Server::TaskUpdater> taskUpdater) override
    {
        std::cout << "AgentExecutor::Cancel() called\n";
        taskUpdater->Cancel();
    }
};

int main() {
    // 1. 创建executor
    auto executor = std::make_shared<MyAgentExecutor>();

    // 2. 创建 AgentCard
    auto agentCard = std::make_shared<A2A::AgentCard>();
    agentCard->name = "ExampleAgent";
    agentCard->description = "A2A Hello World Example";
    agentCard->version = "1.0.0";
    agentCard->defaultInputModes = {"text"};
    agentCard->defaultOutputModes = {"text"};
    agentCard->capabilities.streaming = false;
    agentCard->supportedInterfaces = {
        {
            "http://127.0.0.1:8080/jsonrpc",
            "JSONRPC",
            "1.0",
            std::nullopt
        }
    };

    std::cout << "--Built agent card \n";

    // 3. 创建HTTP服务器配置
    A2A::Server::HttpConfig httpConfig;
    httpConfig.ip = "127.0.0.1";
    httpConfig.port = 8080;

    // 4. 使用HTTP服务器构建器创建服务器
    auto server = A2A::Server::HttpServerBuilder::Build(
        httpConfig,
        agentCard,
        nullptr,
        executor,
        nullptr
    );

    std::cout << "\nStarting A2A Hello World server..." << std::endl;
    int ret = server->Start();
    if (ret != 0) {
        std::cerr << "Server failed to start" << std::endl;
        return -1;
    }

    // 简单等待一段时间，然后退出
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));

    // 停止服务器
    server->Stop();

    std::cout << "Server stopped.\n";
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