# MCP C++ SDK

## 目录

- [概述](#概述)
- [文档](#文档)
- [环境要求](#环境要求)
- [快速入门](#快速入门)
- [构建与安装](#构建与安装)
- [运行示例](#运行示例)
- [运行测试](#运行测试)
- [License](#license)

## 概述

MCP C++ SDK 是 [Model Context Protocol](https://modelcontextprotocol.io/) 的 C++ 实现，提供 MCP 客户端与服务器能力，支持 **Streamable HTTP** 与 **stdio** 传输。

- 构建 MCP 客户端：连接 MCP 服务器，调用 Tool / Resource / Prompt 等
- 构建 MCP 服务器：注册 Tool / Resource / Prompt，处理协议生命周期
- 支持鉴权、Sampling、Progress、Roots 等扩展能力（详见示例与 API 文档）

## 文档

| 文档 | 说明 |
|------|------|
| [依赖说明](docs/dependencies.md) | 运行/编译依赖、版本、各发行版安装命令 |
| [测试说明](docs/testing.md) | 单元测试、集成测试、覆盖率 |
| [API 文档索引](docs/api/README.md) | Client / Server API、协议对照 |
| [易用性评估报告](docs/MCP_CPP_USABILITY_REPORT.md) | 问题分析与整改路线 |

## 环境要求

- **操作系统**：Linux（主要验证平台）
- **编译器**：C++17 及以上
- **CMake**：≥ 3.15（见根目录 `CMakeLists.txt`）
- **依赖**：见 [docs/dependencies.md](docs/dependencies.md)

## 快速入门

在仓库 `MCP/cpp-sdk` 目录下，按以下顺序执行（约 5–15 分钟，视网络与编译环境而定）：

```bash
# 1. 安装系统依赖（curl、openssl 等）
./scripts/install_deps.sh

# 2. 编译 SDK（产物：output/lib/libmcp.so）
./scripts/build.sh

# 3. 一键启动 Server 并运行全部 Client 示例（推荐）
./scripts/run_example.sh
```

仅运行某一类示例：

```bash
./scripts/run_example.sh -t server      # 后台启动 Server
./scripts/run_example.sh -t tool        # Tool 客户端示例
./scripts/run_example.sh -t sampling    # Sampling 示例
```

默认 Server 端点：`http://127.0.0.1:8000/mcp`。指定端口示例：

```bash
./scripts/run_example.sh -p 9000 -t all
```

**注意**：必须先有 Server 再跑 Client。`run_example.sh -t all` 会自动先起 Server。

## 构建与安装

统一入口为 `scripts/build.sh`（不是根目录下的 `build.sh`）。

```bash
# Release 构建（默认）
./scripts/build.sh

# Debug 构建（含调试符号，-g -O0）
./scripts/build.sh -t Debug

# 编译并启用单元测试
./scripts/build.sh -t Debug --with-tests

# 覆盖率构建 + 运行测试报告（见 docs/testing.md）
./scripts/build.sh -t Debug --coverage

# 安装到系统（需要 sudo）
./scripts/build.sh -i
```

常用 CMake 裁剪选项（经 `build.sh` 传入）：

| 选项 | 含义 |
|------|------|
| `--no-client` | 不编译 Client 组件 |
| `--no-server` | 不编译 Server 组件 |
| `--no-http` | 不编译 HTTP 传输（仅 stdio 等） |
| `--asan` | 启用 AddressSanitizer（自动使用 Debug） |
| `-s, --stdio` | 启用 stdio 相关单元测试 |

**构建产物**：

| 路径 | 说明 |
|------|------|
| `output/lib/libmcp.so` | 共享库 |
| `include/mcp/*.h` | 公共头文件（源码树，安装后位于 prefix/include） |

集成到自己工程时，链接 `libmcp.so` 并添加头文件路径 `include/mcp`（或安装后的 include 目录）。详见各示例下的 `CMakeLists.txt`。

## 运行示例

示例位于 `example/` 目录（注意是单数 `example`，非 `examples`）。各子目录有独立 `run_example.sh`，也可使用统一脚本 `scripts/run_example.sh`。

| 示例 | 路径 | 说明 |
|------|------|------|
| Server | `example/server_example/` | HTTP Server，含 Tool/Prompt/Resource/Auth 等 |
| Tool Client | `example/client_example/tool_example/` | Initialize、ListTools、CallTool |
| Prompt Client | `example/client_example/prompt_example/` | ListPrompts、GetPrompt |
| Resource Client | `example/client_example/resource_example/` | List/Read/Subscribe Resource |
| Sampling Client | `example/client_example/sampling_example/` | 服务端发起的 sampling |

- [Client 示例说明](example/client_example/README.md)
- [Server 示例说明](example/server_example/README.md)

## 运行测试

```bash
./scripts/build.sh -t Debug --with-tests
cd build && ctest --output-on-failure
```

覆盖率与 HTML 报告见 [docs/testing.md](docs/testing.md)。

## License

本项目依据 Apache-2.0 许可证授权。

## Copyright

Copyright (c) 2025 Huawei Technologies Co., Ltd. All rights reserved.

## Third-Party Notices

本项目包含或依赖第三方开源软件，其版权和许可证信息均归原作者所有，并在相应文件中予以说明。
