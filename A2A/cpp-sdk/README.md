# a2a_cpp

## 简介

**A2A C++ SDK**是基于agent to agent协议的C++实现，旨在帮助开发者快速构建多智能体协作系统、任务编排系统、AI Agent平台、边缘智能设备通信等场景。[A2A协议规范](https://a2a-protocol.org/v0.3.0/specification/)是一套用于智能体之间通信的标准化协议，定义了任务、消息、时间、智能体能力描述等核心结构。SDK提供统一的数据结构、序列化能力、协议校验机制以及通信接口，让智能体之间的交互更加稳定、高效、可维护。


## 快速开始

### 安装

#### 环境要求
- C++17 或以上编译器（GCC/Clang）
- **CMake >= 3.15**（与仓库根 `CMakeLists.txt` 一致）
- Git、pthread、常规构建工具（`make` 或 `ninja`）
- 操作系统：以 **Linux** 为主进行验证

#### 安装构建依赖

以下为 **CMake 直接依赖** 或 **`third_party/third_party.cmake` 拉取/查找** 的组件及 **建议版本**（满足或高于该版本通常即可通过 `find_package` / 编译；若系统包过旧，可依赖 FetchContent 离线源码目录或联网拉取）。

| 依赖 | 角色 | 建议版本 / 备注 |
|------|------|-----------------|
| **OpenSSL**（开发包 `libssl-dev` / `openssl-devel` 等） | TLS、加解密 | **>= 1.1.1**（常见为 1.1.1 系或 3.x） |
| **libcurl**（开发包 `libcurl4-openssl-dev` / `libcurl-devel` 等） | HTTP 客户端（JSON-RPC 等） | **>= 7.68**；新发行版往往更高 |
| **libevent** | 事件循环 | 系统包 **2.1.x** 或与 FetchContent 默认 **`release-2.1.12-stable`** 等价能力 |
| **nlohmann_json** | JSON 序列化 | **>= 3.11.2**（FetchContent 默认 **`v3.11.2`**） |
| **http_parser**（nodejs/http-parser） | HTTP 报文解析 | FetchContent 默认 **`v2.9.4`**；亦可使用本地 `third_party/http_parser-src/` 离线构建 |

**说明：** 当前本仓库 **A2A C++ SDK 的 CMake 配置未使用 cpp-httplib**；若文档其它处仍出现该依赖，可忽略或与历史模板区分。

CMake 会通过 **`find_package(OpenSSL)`、`find_package(CURL)`** 使用系统 **OpenSSL**、**libcurl**；并通过 **`third_party/third_party.cmake`** 解析 **libevent**、**nlohmann_json**、**http_parser**（优先系统包，缺失时 **FetchContent** 拉源码，需网络与 Git；亦可预先放置与 `third_party` 约定一致的源码目录以离线构建）。

**Ubuntu / Debian（示例）**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config git \
  libssl-dev libcurl4-openssl-dev libevent-dev nlohmann-json3-dev
```

**Fedora / RHEL / CentOS Stream（示例）**

```bash
sudo dnf install -y gcc-c++ cmake pkg-config git openssl-devel libcurl-devel libevent-devel nlohmann-json-devel
```

**macOS（示例）**

```bash
brew install cmake pkg-config openssl libcurl libevent nlohmann-json
```

其他发行版请安装 **等价开发包**；若系统无对应包，由 `third_party/third_party.cmake` 尝试 FetchContent（需可访问 GitHub 等）。

#### 编译（含示例）

在 **`A2A/cpp-sdk`** 目录下执行（推荐一键脚本）：

```bash
cd A2A/cpp-sdk
bash scripts/build.sh -e
```

默认生成目录为 **`build/`**（可用 `bash scripts/build.sh -e -b <目录名>` 修改）。脚本等价于在构建目录执行：

```bash
cmake <sdk根目录> -DCMAKE_BUILD_TYPE=Release -DA2A_ENABLE_EXAMPLES=ON
cmake --build . -j"$(nproc)"
```

也可自行 `mkdir build && cd build` 后传入 **`-DA2A_ENABLE_EXAMPLES=ON`**；**未打开该选项时不会生成示例可执行文件**。

#### 构建产物说明

| 路径 | 内容 |
|------|------|
| **`build/src/liba2a.so`**（或平台等价动态库名） | 主库，链接示例时依赖此文件 |
| **`build/examples/`** | **`helloworld_server`**、**`helloworld_client`**、**`streaming_server`**、**`streaming_client`**（仅在 `A2A_ENABLE_EXAMPLES=ON` 时生成） |
| **`output/`** | 在 **`a2a` 目标构建完成后** 复制 **`liba2a.so`**、`include/` 头文件及 **nlohmann** 头文件，便于打包分发；**不包含示例二进制** |

运行示例前，请将 **`build/src`**（以及 `build` 根目录，若需要）加入动态库搜索路径，例如：

```bash
export LD_LIBRARY_PATH="/path/to/A2A/cpp-sdk/build/src:/path/to/A2A/cpp-sdk/build:${LD_LIBRARY_PATH:-}"
```

将 **`/path/to/A2A/cpp-sdk`** 换成本机 SDK 根目录的绝对路径。

#### 运行示例（命令行）

以下端口仅为示例，**`-p` 可改为任意本机可用端口**；客户端 **`ip` / `port` 须与服务端一致**。

**Hello World（非流式）**

```bash
/path/to/A2A/cpp-sdk/build/examples/helloworld_server -i 127.0.0.1 -p 8080
/path/to/A2A/cpp-sdk/build/examples/helloworld_client -i 127.0.0.1 -p 8080
```

客户端末尾有 **`getchar()`**，需按回车结束进程；在脚本或非交互环境可向 stdin 送入换行，例如：`printf '\n' | .../helloworld_client ...`。

**Streaming（与 `streaming_server` 配套）**

```bash
/path/to/A2A/cpp-sdk/build/examples/streaming_server -i 127.0.0.1 -p 8080
/path/to/A2A/cpp-sdk/build/examples/streaming_client -i 127.0.0.1 -p 8080
```

#### 冒烟自检（可选）

在配置好 **`LD_LIBRARY_PATH`** 后，可在 SDK 根目录执行：

```bash
bash scripts/run_example.sh
```

默认使用端口 **`8888`**（可通过环境变量 **`A2A_EXAMPLE_PORT`** 覆盖）；脚本内会对服务端做 JSON-RPC 探测，并启动两个 client（通过 **`printf '\n'`** 避免 **`getchar()`** 阻塞）。

### 样例

完整可编译示例在仓库 **`examples/`** 目录：

| 文件 | 说明 |
|------|------|
| **`helloworld_server.cpp` / `helloworld_client.cpp`** | 非流式服务端与客户端（手写 **`A2A::AgentCard`**，与服务器配置一致） |
| **`streaming_server.cpp` / `streaming_client.cpp`** | 流式编排示例与客户端（**`ClientCallContext`**、`AddEventConsumer` 等） |

下面给出与当前 SDK 一致的 **客户端最小思路**（省略 `getopt` 等，细节以 `helloworld_client.cpp` 为准）：使用 **`A2A::Client::ClientFactory::Create`**，**`ClientEvent`** 为 **`std::variant<A2A::Message, std::pair<A2A::Task, ...>>`**，按需使用 **`std::holds_alternative` / `std::visit`** 处理返回。

```cpp
#include "client/client_factory.h"
#include "types.h"

// 与 helloworld_server 中 AgentCard 字段保持一致（含 url 指向 .../jsonrpc）
A2A::AgentCard card;
card.name = "ExampleAgent";
card.url = "http://127.0.0.1:8080/jsonrpc";
card.version = "1.0.0";
card.defaultInputModes = {"text"};
card.defaultOutputModes = {"text"};
card.capabilities.streaming = false;
card.preferredTransport = A2A::JSONRPC_TRANSPORT;

A2A::Client::ClientConfig cfg;
cfg.streaming = false;
cfg.supportedTransports = {"JSONRPC"};
auto client = A2A::Client::ClientFactory::Create(card, cfg);

A2A::Message msg;
msg.role = A2A::Role::USER;
msg.messageId = "123";
// ... 组装 parts 后 client->SendMessage(msg, nullptr, handler);
```

服务端请使用 **`A2A::Server::HttpServerBuilder::Build`** 与 **`A2A::Server::AgentExecutor`**，完整流程见 **`examples/helloworld_server.cpp`**。

**说明：** `include/client/a2a_card_resolver.h` 提供 **`A2A::Client::A2ACardResolver`** 抽象接口（异步 **`GetAgentCard`**）；当前示例采用 **手写 AgentCard**，便于与示例服务器对齐。若需从 **`/.well-known/agent-card.json`** 拉取卡片，需在应用侧自行实现 **`A2ACardResolver`** 或使用项目内其它 HTTP 解析逻辑。


**A2A C++ SDK**采用模块化设计，核心模块包括：

* **SDK接口层**：定义 **`A2A::Client::Client`**、**`A2A::Server::HttpServerBuilder`**、**`A2A::Client::A2ACardResolver`**（抽象）、**`A2A::Server::AgentExecutor`** 等，便于搭建客户端与服务端。

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