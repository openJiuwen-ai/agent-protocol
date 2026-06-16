# A2A C++ SDK

## 目录

- [概述](#概述)
- [文档](#文档)
- [环境要求](#环境要求)
- [快速入门](#快速入门)
- [构建与安装](#构建与安装)
- [运行示例](#运行示例)
- [运行测试](#运行测试)
- [参与贡献](#参与贡献)
- [License](#license)

## 概述

A2A C++ SDK 是 [Agent-to-Agent 协议 v1.0](https://a2a-protocol.org/v1.0.0/specification/) 的 C++ 实现，提供 A2A 客户端与服务器能力，支持基于 **HTTP + JSON-RPC** 的智能体间通信。

- 构建 A2A 客户端：发现 Agent Card、发送消息、查询/取消任务、接收流式事件
- 构建 A2A 服务器：注册 `AgentExecutor`、管理任务生命周期、暴露 JSON-RPC 端点
- 支持多模态 `Part`、推送通知配置、请求拦截器、自定义 `TaskStore` 等扩展能力（详见示例与 API 文档）

## 文档

| 文档 | 说明 |
|------|------|
| [API 参考](docs/API.md) | 公开头文件、类型、Client / Server API 说明 |
| [日志说明](docs/LOGGING.md) | SDK 诊断日志、`A2A_LOG` / `SetLogCallback` 用法 |
| [测试说明](docs/TESTING.md) | 单元测试布局、构建与运行、覆盖率 |

## 环境要求

- **操作系统**：Linux（glibc 或 musl，主要验证平台）
- **编译器**：C++17 及以上（GCC 9+ / Clang 10+）
- **CMake**：≥ 3.16（见根目录 `CMakeLists.txt`）
- **依赖**：OpenSSL、libcurl、libevent、nlohmann_json；`http_parser` 由 `third_party/` 提供（v2.9.4）

可使用 `scripts/install_deps.sh` 一键安装系统包；`libevent` / `nlohmann_json` 缺失时会由 `third_party/third_party.cmake` 自动从 GitHub 拉取。

## 快速入门

在仓库 `A2A/cpp-sdk` 目录下，按以下顺序执行（约 5–15 分钟，视网络与编译环境而定）：

```bash
# 1. 安装系统依赖（curl、openssl、libevent 等；部分发行版需 sudo）
sudo ./scripts/install_deps.sh

# 2. 编译 SDK 与示例（产物：output/lib/liba2a.so、output/bin/*）
./scripts/build.sh -e

# 3. 一键冒烟测试全部示例（推荐）
./scripts/run_example.sh
```

默认示例端口：`8888`。JSON-RPC 端点：`http://127.0.0.1:8888/jsonrpc`。指定端口示例：

```bash
A2A_EXAMPLE_PORT=9000 ./scripts/run_example.sh
```

**注意**：`run_example.sh` 会依次启动 `helloworld` 与 `streaming` 示例的 Server/Client 并做 HTTP/JSON-RPC 检查，需先完成 `-e` 编译。

## 构建与安装

统一入口为 `scripts/build.sh`（不是仓库根目录下的 `build.sh`）。

```bash
# Release 构建（默认，仅核心库）
./scripts/build.sh

# Release 构建 + 示例
./scripts/build.sh -e

# Debug 构建（含调试符号）
./scripts/build.sh -t Debug

# 编译并启用单元测试
./scripts/build.sh -u -t Debug

# 覆盖率构建 + 运行测试报告（见 docs/TESTING.md）
./scripts/build.sh -c

# AddressSanitizer 调试构建
./scripts/build.sh -u -t Debug --asan
```

常用 CMake 裁剪选项（经 `build.sh` 传入）：

| 选项 | 含义 |
|------|------|
| `-e, --with-examples` | 编译 `examples/` 下示例程序 |
| `-u, --with-tests` | 编译单元测试 |
| `-c, --coverage` | 启用覆盖率（隐含 `-u`，Debug） |
| `--no-client` | 不编译 Client 组件 |
| `--no-server` | 不编译 Server 组件 |
| `--asan` | 启用 AddressSanitizer（自动使用 Debug） |
| `-t Debug\|Release` | 构建类型，默认 Release |
| `-b, --build-dir <dir>` | 构建目录，默认 `build` |

**构建产物**：

| 路径 | 说明 |
|------|------|
| `output/lib/liba2a.so` | 共享库 |
| `output/include/` | 公共头文件（`types.h`、`client/`、`server/` 等） |
| `output/bin/` | 示例二进制（需 `-e`） |

集成到自己工程时，链接 `liba2a.so` 并添加头文件路径 `output/include`（或源码树 `include/`）。运行时需要将 `output/lib` 加入 `LD_LIBRARY_PATH`。详见 `examples/CMakeLists.txt`。

## 运行示例

示例位于 `examples/` 目录。可使用统一脚本 `scripts/run_example.sh` 做冒烟测试，也可手动分别启动 Server 与 Client：

```bash
export LD_LIBRARY_PATH="$(pwd)/output/lib:${LD_LIBRARY_PATH:-}"

./output/bin/helloworld_server -i 127.0.0.1 -p 8080
./output/bin/helloworld_client -i 127.0.0.1 -p 8080
```

| 示例 | 源文件 | 说明 |
|------|--------|------|
| Hello World Server | `examples/helloworld_server.cpp` | HTTP Server，非流式消息处理 |
| Hello World Client | `examples/helloworld_client.cpp` | 获取 Agent Card、发送消息 |
| Streaming Server | `examples/streaming_server.cpp` | 流式任务状态与产物推送 |
| Streaming Client | `examples/streaming_client.cpp` | `message/stream` 流式接收 |

API 与协议细节见 [docs/API.md](docs/API.md)。

## 运行测试

```bash
# 构建 + 运行 ctest（含覆盖率与 ASAN，默认）
./scripts/run_ut.sh

# 跳过覆盖率，更快
./scripts/run_ut.sh --no-coverage

# 跳过 ASAN
./scripts/run_ut.sh --no-coverage --no-asan
# 或
A2A_SKIP_ASAN=1 ./scripts/run_ut.sh --no-coverage
```

也可手动构建后运行：

```bash
./scripts/build.sh -u -t Debug
cd build && ctest --output-on-failure
```

覆盖率与报告说明见 [docs/TESTING.md](docs/TESTING.md)。

## 参与贡献

欢迎提交 Issue、文档改进与代码贡献。提交 PR 前请运行：

```bash
./scripts/run_ut.sh --no-coverage
```

确保单元测试通过。

## License

本项目依据 Apache-2.0 许可证授权。

## Copyright

Copyright (c) 2025-2026 Huawei Technologies Co., Ltd. All rights reserved.

## Third-Party Notices

本项目包含或依赖第三方开源软件，其版权和许可证信息均归原作者所有，并在相应文件中予以说明。
