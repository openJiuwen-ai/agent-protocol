# MCP C++ SDK

## 目录

- [概述](#概述)
- [文档](#文档)
- [环境要求](#环境要求)
- [快速入门](#快速入门)
- [构建与安装](#构建与安装)
- [运行示例](#运行示例)
- [运行测试](#运行测试)
- [日志](#日志)
- [License](#license)
- [Copyright](#copyright)
- [Third-Party Notices](#third-party-notices)

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

## 环境要求

- **操作系统**：Linux（主要验证平台）
- **编译器**：C++17 及以上
- **CMake**：≥ 3.15（见根目录 `CMakeLists.txt`）
- **依赖**：见 [docs/dependencies.md](docs/dependencies.md)

## 快速入门

在仓库 `MCP/cpp-sdk` 目录下，按以下顺序执行（约 5–15 分钟，视网络与编译环境而定）。

**前置条件**：已安装 C++17 编译器（如 `gcc-c++`）和 CMake ≥ 3.15。`install_deps.sh` 仅安装 OpenSSL、libcurl 开发包，不包含编译器与 CMake（见 [依赖说明](docs/dependencies.md)）。

```bash
cd MCP/cpp-sdk

# 1. 安装 OpenSSL、libcurl 开发包
bash scripts/install_deps.sh

# 2. 编译 SDK（产物：output/lib/libmcp.so）
#    第三方库（含 nlohmann_json）优先使用系统包；系统没有时由 CMake 下载到 third_party/
bash scripts/build.sh

# 3. 构建并运行全部示例（须先完成步骤 2；-t all 会自动先起 Server）
sh scripts/run_example.sh
```

**脚本调用说明**：

| 脚本 | 解释器 | 说明 |
|------|--------|------|
| `install_deps.sh` | `bash` | Bash 脚本，勿用 `sh` 调用 |
| `build.sh` | `bash` | 同上 |
| `run_example.sh` | `sh` | POSIX `sh` 脚本，可用 `sh` 或 `./`（需可执行权限） |

**nlohmann_json**：主工程 `build.sh` 与示例构建均**优先使用系统** `nlohmann_json`；系统未安装时，`build.sh` 会拉取到 `third_party/nlohmann_json-src`，示例也会回退使用该目录。若系统已有 nlohmann 开发包，则不会生成 `third_party/nlohmann_json-src`，示例仍可直接编译。

仅运行某一类示例：

**方式一：单终端一键（推荐首次验证）**

```bash
sh scripts/run_example.sh              # 或 -t all：后台起 Server 并依次跑全部 Client
```

**方式二：双终端（Server 常驻，便于反复调试 Client）**

```bash
# 终端 1：前台运行 Server（Ctrl+C 停止；不要用 nohup 包一层 scripts/run_example.sh）
sh scripts/run_example.sh -t server

# 终端 2：Server 已监听后再跑 Client
sh scripts/run_example.sh -t tool
sh scripts/run_example.sh -t sampling
```

也可用各子目录下的 `example/server_example/run_example.sh`（终端 1）与 `example/client_example/*/run_example.sh`（终端 2）。

默认 Server 端点：`http://127.0.0.1:8000/mcp`。指定端口示例：

```bash
sh scripts/run_example.sh -p 9000 -t all
```

**注意**：

- 必须先有 Server 再跑 Client。
- `-t all` / 无参数：同一脚本内后台起 Server，结束后自动停止。
- `-t server`：前台常驻，供另一终端连接；脚本退出时**不会**误杀 Server（旧版会在 EXIT 时 kill 后台进程，导致双终端流程失败）。

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

## 日志

SDK 默认将诊断日志输出到 **stdout**。stdio 传输模式下请通过 `SetLogCallback` 重定向日志，避免污染协议通道。详见 [docs/logging.md](docs/logging.md)。

## License

本项目依据 Apache-2.0 许可证授权。

## Copyright

Copyright (c) 2025 Huawei Technologies Co., Ltd. All rights reserved.

## Third-Party Notices

本项目包含或依赖第三方开源软件，其版权和许可证信息均归原作者所有，并在相应文件中予以说明。
