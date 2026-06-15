# MCP C++ SDK 易用性评估报告

**评估对象**：`MCP/cpp-sdk`  
**评估日期**：2026-06-10  
**评估方式**：静态代码审阅、文档审阅、测试体系梳理（只读调研，未执行完整测试运行）

**整改范围约束（2026-06-10）**：本期整改**不涉及**错误处理模型与接口易用性的代码/API 变更。第 4.3、4.4 节保留为评估记录；落地工作聚焦环境搭建、依赖说明、README/文档、示例整理、编译发布与测试规范。

---

## 目录

- [1. 执行摘要](#1-执行摘要)
- [2. 评估范围与方法](#2-评估范围与方法)
- [3. 总体判断](#3-总体判断)
- [4. 分维度问题分析](#4-分维度问题分析)
  - [4.1 测试与 UT 规范性](#41-测试与-ut-规范性)
  - [4.2 示例与 API 文档](#42-示例与-api-文档)
  - [4.3 错误码与异常类型](#43-错误码与异常类型)
  - [4.4 接口易用性与接口注释](#44-接口易用性与接口注释)
  - [4.5 README 规范性](#45-readme-规范性)
- [5. P0 细化问题](#5-p0-细化问题)
- [6. 整改路线与建议](#6-整改路线与建议)
- [7. 建议文档体系](#7-建议文档体系)
- [8. 验收标准](#8-验收标准)
- [9. 证据与参考路径](#9-证据与参考路径)
- [10. 结论](#10-结论)

---

## 1. 执行摘要

MCP C++ SDK 在功能面上已具备较完整的 client/server、HTTP/stdio 传输、Auth、Sampling、Progress、Roots 等能力，配套 GTest 单元测试（约 600+ 用例）、示例代码和构建脚本。当前主要短板不在“能不能用”，而在**首次接入成本高、语义不统一、文档不成体系**。

| 维度 | 风险等级 | 一句话概括 |
|------|----------|------------|
| 测试/UT | 中 | GTest 覆盖较多，但 UT/ST 边界不清，工程化与规范性不足 |
| 示例 | 中 | 功能覆盖尚可，但缺少最小 quickstart，示例偏大、路径硬编码 |
| API 文档 | 高 | 仅有头文件注释，缺少独立 API 参考与生命周期说明 |
| 错误处理 | 高 | 多种错误表达方式并存，调用方难以建立统一心智模型 |
| 接口易用性 | 高 | JSON string 参数、future 样板多、Optional 参数晦涩 |
| README | 中 | 有基础结构，但存在与代码不一致的命令与路径 |

**整改优先级**：中高。**本期优先**：README/搭建路径、依赖与编译文档、示例与测试规范。**暂缓**：错误处理统一、接口/API 易用性改造（见上文范围约束）。

---

## 2. 评估范围与方法

### 2.1 范围

- Public API：`include/mcp/*.h`（6 个头文件）
- 实现与行为：`src/client/`、`src/server/`、`src/shared/` 等
- 示例：`example/`（server、client tool/prompt/resource/sampling）
- 测试：`tests/ut/`、ST 集成测试、遗留测试目录
- 文档：`README.md`、`example/client_example/README.md`、发布说明草稿
- 构建入口：`CMakeLists.txt`、`scripts/build.sh`、`scripts/run_example.sh`、`scripts/run_ut.sh`

### 2.2 方法

- 审阅 public headers 的接口设计、注释与异常声明
- 对照实现验证文档与 API 是否一致
- 梳理测试目录结构、CTest 目标、命名与断言风格
- 对照 README、示例 README、脚本能力，识别文档缺口与事实性错误

---

## 3. 总体判断

SDK 已具备生产向的基础能力：工厂 + 抽象接口 + 丰富 DTO，JSON-RPC 错误通过 `MCPError` 映射到 `future.get()` 的设计方向合理，服务端 sync/async handler 抽象也有表现力。

主要问题集中在三类：

1. **入口不顺**：README 命令、依赖包名、产物路径与脚本/CMake 不一致；示例 CMake 硬编码 `output/lib/libmcp.so`。
2. **语义分裂**：`bool` / `std::runtime_error` / `std::invalid_argument` / `MCPError` / `CallToolResult::isError` 并存；Client 抛异常 vs Server `Run()` 返回 `bool`。
3. **文档不成体系**：无 `docs/` 索引、无独立 API Reference、release notes 草稿反而信息最全。

新用户容易在依赖安装、构建路径、JSON 参数格式、异步 future 样板、错误分类、回调注册时机、示例选择上卡住。

---

## 4. 分维度问题分析

### 4.1 测试与 UT 规范性

#### 现状

- **框架**：Google Test（系统包或 FetchContent v1.16.0）
- **组织**：`tests/ut/<module>/`，与 `src/` 模块基本对应
- **规模**：约 31 个 gtest 源文件、604 个 `TEST`/`TEST_F` 用例
- **发现方式**：11 个模块可执行文件 + `gtest_discover_tests` → CTest
- **覆盖较好**：`jsonrpc`、Manager 层（tool/resource/prompt）、transport、auth、部分 session 能力
- **集成测试**：`client_test.cpp` 中 `McpIntegrationTest`（真实 HTTP server）；ST 仅 1 例（`SetLoggingLevel`）

#### 主要问题

| 问题 | 说明 |
|------|------|
| 测试默认关闭 | `MCP_ENABLE_TESTS=OFF`，README 无 UT 章节，无 CI 门禁 |
| 命名不统一 | `*_test.cpp` 与 `test_*.cpp` 混用；同名不同物（如两处 `http_client_test.cpp`） |
| `aux_source_directory` | 隐式收集源文件，易混入非测试代码 |
| CTest 覆盖不完整 | `ut/http/` 手写 `main()`；`http_server_test.cpp` 等游离于 CTest；`tests/http_client_service/` 独立维护 |
| 断言风格不一致 | 混用 `EXPECT_THROW`、`catch (...)` + `SUCCEED()`、`FAIL() << e.what()` |
| GTest 链接不一致 | 部分用 `GTest::gtest_main`，部分用 `gtest`/`gtest_main` |
| 模块覆盖空洞 | 无 `mcp_exception`、`sampling_validation` 专项 UT；消息队列、thread utils 无测试 |
| 集成测试脆弱 | 固定端口（8001/9000）、`sleep` 等待、重复 `McpTestServer` 实现，并行/CI 易失败 |
| UT/ST 边界不清 | ST 目录在 `tests/ut/st/`，命名易混淆 |

#### 整改方向

1. README 补充测试章节；CI 执行 `build.sh -t Debug --with-tests --stdio` + `ctest --output-on-failure`
2. 统一命名规范（推荐 `<module>_<feature>_test.cpp`）；合并/删除游离测试
3. CMake 显式列出测试源文件；全部纳入 `gtest_discover_tests`
4. 抽取公共 `McpTestServer` fixture；ST 扩展为端到端场景矩阵
5. 集成测试使用动态端口；用 readiness 轮询替代 `sleep`
6. 补 `mcp_exception`、`sampling_validation` 专项 UT；设定覆盖率门禁

#### 如何运行测试（当前）

```bash
cd MCP/cpp-sdk

# 编译并启用测试
./scripts/build.sh -t Debug --with-tests

# 完整：覆盖率 + stdio UT + ST
./scripts/build.sh -t Debug --coverage --stdio

cd build
ctest -V

# 或生成覆盖率报告
../scripts/run_ut.sh
```

---

### 4.2 示例与 API 文档

#### 现状

| 示例 | 覆盖能力 |
|------|----------|
| `server_example` | Tool/Prompt/Resource、Auth、Sampling、Progress、Completion、stateless |
| `tool_example` | Initialize、Ping、ListTools、CallTool、Progress、Complete |
| `prompt_example` | ListPrompts、GetPrompt |
| `resource_example` | ListResources、Subscribe/Unsubscribe、ReadResource、Templates |
| `sampling_example` | 服务端发起的 `sampling/createMessage` |

另有 `scripts/run_example.sh` 支持一键冒烟（server + 全部 client 示例）。

#### 主要问题

| 问题 | 说明 |
|------|------|
| 缺少最小 quickstart | `server_example.cpp` 体量过大（800+ 行），不适合作为入门示例 |
| 示例 README 过时 | `client_example/README.md` 路径写 `output/libmcp.so`、`output/mcp/` 头文件，与实际不符 |
| 缺少 sampling 说明 | client README 目录树未包含 `sampling_example` |
| 无 server README | stateless、端口、`--auth` 等无独立说明 |
| 无 stdio 示例 | README 声称支持 stdio，示例目录无对应样例 |
| Auth 不可见 | Auth 能力主要在 UT 和 server 参数中，无面向用户的 Auth 文档 |
| 无集成指南 | 示例 CMake 硬编码 `.so` 路径，未说明 `cmake --install` 或 `find_library` |
| 无独立 API 文档 | API = 头文件内联注释；无 Doxygen/Sphinx 站点、无协议能力矩阵 |

#### 整改方向

1. 新增 50–80 行最小 server/client 示例（仅 Initialize → 注册/调用 → 关闭）
2. 示例分层：`quickstart/`、`feature/`（Auth/Sampling/Progress）、`advanced/`
3. 更新 `client_example/README.md`；新增 `server_example/README.md`
4. 建立 MCP 方法名 ↔ C++ API ↔ 示例文件 ↔ 测试用例对照表
5. 引入 Doxygen 生成 API Reference；补充概念文档（握手、会话、future 错误处理）

---

### 4.3 错误码与异常类型

> **本期状态：仅评估记录，不整改。** 以下问题已知，后续单独立项处理。

#### 现状

**公开错误类型**（`include/mcp/mcp_error.h`）：

```cpp
enum class JsonRpcErrorCode : int {
    PARSE_ERROR = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS = -32602,
    INTERNAL_ERROR = -32603,
    SERVER_ERROR = -32000
};

class MCPError : public std::runtime_error { ... };
```

**错误传播路径**：

- Client：JSON-RPC error → `ErrorResult` → `promise::set_exception(MCPError)` → `future.get()` 抛出
- Server：handler 异常 → `SendErrorResponse`；工具业务错误 → `CallToolResult.isError = true`
- 本地/SDK 错误：`std::runtime_error`、`std::invalid_argument`；Server `Run()` 返回 `false`

#### 主要问题

| 问题 | 说明 |
|------|------|
| 双轨错误，无统一基类 | 协议错误 `MCPError` vs SDK 编程错误 `runtime_error`；`catch (runtime_error&)` 会误吞 `MCPError` |
| `SERVER_ERROR` 枚举过窄 | 仅 -32000，MCP 自定义 -32000…-32099 无枚举成员 |
| 服务端错误码映射随意 | 工具不存在、schema 校验失败均映射为 `INVALID_PARAMS` |
| 业务错误 vs 协议错误混用 | `CallTool` 既可能 `throw MCPError`，也可能返回 `isError=true` 的 Result |
| `CallTool` timeout 未接线 | API 暴露 `int timeout`，会话层 `requestTimeout` 未实际实现 |
| Sampling 拒绝 | 注释写 JSON-RPC code -1，非标准码，无对应枚举 |
| 文档缺失 | 无集中错误处理专章；README 未说明捕获顺序与恢复策略 |

#### 建议错误语义矩阵（文档化）

| 场景 | 表现方式 | 调用方处理 |
|------|----------|------------|
| 本地参数错误（空 name、非法 config） | `std::invalid_argument` / `std::runtime_error` | 修复调用代码 |
| 未初始化调用 | `std::runtime_error("client is not initialized.")` | 先 `Initialize()` |
| 远端 JSON-RPC 错误 | `MCPError`（`future.get()`） | `catch (MCPError&)`，读 `code()` / `message()` |
| 工具业务失败 | `CallToolResult.isError == true` | 检查 Result，不依赖异常 |
| 传输/内部错误 | `MCPError(INTERNAL_ERROR)` 或 transport 回调 | 重试或降级 |
| Server 启动失败 | `Run()` 返回 `false` | 查日志，无异常信息 |

#### 整改方向

1. 文档明确捕获顺序：`catch (MCPError&)` 先于 `catch (runtime_error&)`
2. 实现或移除 `CallTool` 的 `timeout` 参数
3. 扩展 `JsonRpcErrorCode` 或提供 `isServerError(code)` 辅助函数
4. 统一 Server 启动错误表达（抛 `McpStartupError` 或 `expected<void, Error>`）
5. 新增 `docs/errors.md` 专章

---

### 4.4 接口易用性与接口注释

> **本期状态：仅评估记录，不整改。** 以下问题已知，后续单独立项处理。

#### Public API 概况

```
McpClientFactory / McpServerFactory
        ↓
McpClient / McpServer（纯虚接口）
        ↓
ClientSession / McpServerImplement + Transport
```

- Client 返回 `shared_ptr<McpClient>`，Server 返回 `unique_ptr<McpServer>`（所有权风格不一致）
- 几乎全部 RPC 返回 `std::future<std::shared_ptr<T>>`
- 部分回调须在 `Initialize()` 前注册（sampling、roots），其余未统一说明

#### 易用性问题

| 类别 | 问题 |
|------|------|
| 异步负担 | 示例需手写 `wait_for` + `get()` + 多层 `try/catch`，样板代码多 |
| 生命周期 | 无 `IsInitialized()`；`CloseGracefully()` 后能否再 `Initialize()` 未文档化 |
| 回调陷阱 | `SetLoggingCallback` / `SetElicit*` 在 Initialize 前可能空指针崩溃；与 `SetListRootsCallback` 行为不一致 |
| Optional 参数晦涩 | `AddToolOptionalParams` 使用 `optional<reference_wrapper<const T>>`，需 `std::cref()`，易悬空 |
| JSON 参数不友好 | `CallTool`/`GetPrompt` 的 `arguments` 为 JSON string；非法 JSON 可能在 `get_future()` 前就抛异常 |
| 命名不一致 | `ListResourcesTemplates`（多了 s）；`RemoveResource` 注释写 "by name" 但参数是 `uri` |
| 内部头泄漏 | CMake 将 `src/` 设为 PUBLIC include，用户可 `#include "client/client_session.h"` |
| 编译期能力 | `MCP_WITH_HTTP` 关闭时 `CreateStreamableHttpClient` 运行时才抛错 |

#### 注释质量

| 文件 | 评价 |
|------|------|
| `mcp_client.h` | 较好：主要 RPC 有 `@brief`/`@param`/`@throw` |
| `mcp_server.h` | 不均衡：核心方法有文档，`McpServerSession`、`RegisterSetLoggingLevelHandler` 等缺失 |
| `mcp_type.h` | 大量 struct/enum 无字段说明 |
| `mcp_error.h` | 精简清晰 |
| `mcp_auth.h` | 接口级注释尚可 |
| `mcp_log.h` | 几乎无 API 文档 |

#### 整改方向

1. **P0**：修复 Initialize 前回调空指针；实现或移除 `CallTool timeout`；`src/` include 改 PRIVATE
2. **P1**：`AddToolOptionalParams` 改为值类型 `optional<string>`；提供 `JsonValue` 重载
3. **P1**：补充 `IsInitialized()`；提供 `*Sync()` 便捷 API（内部 `future.get()`）
4. **P2**：统一工厂返回值语义；补齐 `mcp_type.h` 字段注释与 Doxygen
5. **P2**：`#ifdef MCP_WITH_HTTP` 编译期 stub，避免运行时才知 HTTP 未启用

---

### 4.5 README 规范性

#### 现状结构

`MCP/cpp-sdk/README.md` 包含：概述、构建依赖、编译、快速启动（Client/Server）、License。

对比 A2A SDK README（依赖表、多发行版命令、产物表、代码片段更完整），MCP README 偏简略。

#### 主要问题

| 问题 | README 写法 | 实际情况 |
|------|-------------|----------|
| 构建脚本路径 | `./build.sh` | 应为 `scripts/build.sh` |
| 依赖包名 | `openssl-lib`、`openssl-devel`（Ubuntu） | 非真实 apt 包名，应为 `libssl-dev`、`libcurl4-openssl-dev` 等 |
| CMake 版本 | ≥ 3.16 | `CMakeLists.txt` 为 3.15 |
| 产物路径 | `output/libmcp.so` | 实际为 `output/lib/libmcp.so` |
| 快速启动顺序 | 先 Client 后 Server | 应先起 Server；推荐 `scripts/run_example.sh` |
| 安装说明 | 无 | CMake 有 `install` + `build.sh -i` |
| CMake 选项 | 无 | `MCP_BUILD_CLIENT/SERVER`、`MCP_WITH_HTTP`、测试/覆盖率/ASAN 仅在 `build.sh --help` |
| 进阶指导 | 无 | 无 stdio、Auth、TLS、stateless、错误处理专题 |
| 测试说明 | 无 | 脚本能力已具备，README 未反映 |

#### 建议 README 结构

```markdown
# MCP C++ SDK

## 简介
## 特性一览
## 环境要求与依赖安装（各发行版正确包名）
## 构建与安装（scripts/build.sh 全参数、产物路径、cmake --install）
## 快速入门（30 分钟跑通 server + client）
## 示例索引（链接 example/ 与各 README）
## API 与文档（链接 docs/）
## 测试（UT/ST/coverage 运行方式）
## 常见问题与排障
## License
```

#### 可立即整改项

1. 修正脚本路径、包名、产物路径
2. 补充从零构建并跑通的一条完整命令序列
3. 链接 `docs/errors.md`、`docs/testing.md`（待创建）
4. 将 `MCP_RELEASE_NOTES_temp.md` 转正并在 README 引用

---

## 5. P0 细化问题

### 5.1 本期 P0（文档与工程化，不含 API/错误处理改动）

| 问题 | 现状 | 整改方向 |
|------|------|----------|
| README 入口错误 | 脚本路径、包名、产物路径、Client/Server 顺序与代码不一致 | 重写构建与快速入门章节，对齐 `scripts/` |
| 示例构建路径 | `MCP_ENABLE_EXAMPLES` 指向 `examples/`，实际为 `example/` | 修正 CMake 或文档，统一命名 |
| 测试收敛 | `ut/http/` 手写 main、`tests/http_client_service/` 游离于 CTest | 迁入 GTest/CTest 或转为 example |
| 测试文档缺失 | README 无 UT/ST/coverage 说明 | 新增 `docs/testing.md` 并在 README 链接 |
| 依赖说明分裂 | README 包名错误，与 `install_deps.sh` 不一致 | 新增 `docs/dependencies.md`，区分运行/编译依赖 |

### 5.2 暂缓（错误处理 / 接口易用性相关，本期不改）

| 问题 | 说明 |
|------|------|
| 回调生命周期 | `SetLoggingCallback` / `SetElicit*` Initialize 前空指针风险 |
| 超时语义 | `CallTool` timeout 参数未接线 |
| Public 边界 | `src/` 作为 PUBLIC include 泄漏内部头 |
| 错误模型统一 | `MCPError` / `runtime_error` / `isError` 分裂 |
| Optional 参数、JSON string API、Sync 便捷接口等 | 接口易用性改造 |

---

## 6. 整改路线与建议

### 6.1 优先级路线图（本期范围）

| 优先级 | 主题 | 建议 |
|--------|------|------|
| **P0** | 入口一致性 | README 与 `scripts/`、CMake 对齐，保证复制命令可跑通 |
| **P0** | 环境搭建 | 固定「依赖 → 编译 → 运行」顺序；推荐 `scripts/run_example.sh` |
| **P0** | 依赖文档 | `docs/dependencies.md`：运行/编译依赖、版本、各发行版包名 |
| **P1** | 编译发布文档 | 在 README 暴露 `build.sh` 参数（Debug/ASAN/裁剪组件/安装） |
| **P1** | 示例与文档 | 最小 quickstart 示例；更新 client/server 示例 README |
| **P1** | 测试规范 | `docs/testing.md`；CTest 收敛；CI 跑 `ctest` |
| **P2** | 文档体系 | getting-started、examples 索引、排障 FAQ |
| **P2** | 打包分发 | 安装文档、`mcpConfig.cmake`（长期） |

**暂缓（下期）**：错误语义统一、`docs/errors.md` 落地、API/回调/timeout/include 边界、JSON 重载、Optional 简化、Doxygen 全量补齐。

### 6.2 可立即落地（Quick Wins，本期）

1. 修正 README 中脚本路径、依赖包名和产物路径
2. 补充一条标准路径：`install_deps.sh` → `build.sh` → `run_example.sh`
3. 整理测试运行文档：UT/ST/stdio/coverage/ASAN 对应脚本与 CMake 开关
4. 更新 `client_example/README.md`，补充 `sampling_example`，新增 `server_example/README.md`
5. 新增最小 server/client quickstart 示例（不改动现有 API 用法）

---

## 7. 建议文档体系

```
MCP/cpp-sdk/
├── README.md                    # 简介、安装、快速入门、导航
└── docs/
    ├── MCP_CPP_USABILITY_REPORT.md   # 本报告
    ├── getting-started.md       # 30 分钟跑通指南
    ├── build.md                 # 依赖表、CMake 选项、安装/卸载
    ├── api/
    │   ├── client.md            # 客户端生命周期、调用模式、回调
    │   └── server.md            # 服务端注册、sync/async handler
    ├── errors.md                # 错误码、异常、业务错误映射
    ├── testing.md               # UT/ST/coverage/ASAN 规范
    ├── examples.md              # 示例矩阵、端口、鉴权配置
    └── guides/
        ├── mcp-http-server.md
        ├── mcp-stdio.md
        ├── mcp-auth.md
        └── integrate-via-cmake.md
```

---

## 8. 验收标准

### 8.1 新用户体验

从干净 Linux 环境开始，按 README 完成：

1. 安装依赖（`scripts/install_deps.sh` 或文档中的 apt/yum 命令）
2. 构建 SDK（`scripts/build.sh`）
3. 启动 Server（`scripts/run_example.sh -t server` 或手动）
4. 运行 Client（`scripts/run_example.sh -t tool`）

**目标**：30 分钟内完成，看到预期日志输出，无需查阅源码或 issue。

### 8.2 开发者体验（本期）

- 能从 README/docs 完成依赖安装、Debug 构建、示例与测试运行
- 能从文档查到 CMake/`build.sh` 主要参数含义

**暂缓验收（下期）**：API 参数格式、回调时序、超时语义、错误捕获顺序、handler 契约等，待错误处理与接口易用性专项整改后再纳入验收。

### 8.3 测试与 CI

- `build.sh -t Debug --with-tests --stdio` + `ctest` 在 CI 中稳定通过
- 覆盖率报告可生成，并设定最低门禁（建议 line ≥ 70% 起步）
- 无固定端口冲突、无依赖 `sleep` 的脆弱集成测试

---

## 9. 证据与参考路径

| 类别 | 路径 |
|------|------|
| SDK 主文档 | `MCP/cpp-sdk/README.md` |
| Public API | `MCP/cpp-sdk/include/mcp/*.h` |
| 错误定义 | `MCP/cpp-sdk/include/mcp/mcp_error.h` |
| 客户端接口 | `MCP/cpp-sdk/include/mcp/mcp_client.h` |
| 服务端接口 | `MCP/cpp-sdk/include/mcp/mcp_server.h` |
| 类型与配置 | `MCP/cpp-sdk/include/mcp/mcp_type.h` |
| 示例 | `MCP/cpp-sdk/example/` |
| 客户端示例说明 | `MCP/cpp-sdk/example/client_example/README.md` |
| 单元测试 | `MCP/cpp-sdk/tests/ut/` |
| 构建脚本 | `MCP/cpp-sdk/scripts/build.sh` |
| 示例冒烟 | `MCP/cpp-sdk/scripts/run_example.sh` |
| 测试与覆盖率 | `MCP/cpp-sdk/scripts/run_ut.sh` |
| 发布说明草稿 | `MCP_RELEASE_NOTES_temp.md` |
| CMake 入口 | `MCP/cpp-sdk/CMakeLists.txt` |

---

## 10. 结论

MCP C++ SDK **功能基础扎实**，测试与脚本能力已具备雏形，但**易用性与文档规范性明显落后于实现进度**。问题本质不是缺功能，而是：

- 入口文档与代码不一致，导致“复制 README 跑不通”
- 错误与异常模型分裂，调用方难以建立统一处理策略
- 示例偏大、API 无独立参考、进阶能力（stdio/Auth/TLS）缺少面向用户的出口
- 测试工程化（默认开启、CI、命名规范、游离文件清理）尚未到位

**建议实施顺序（本期）**：

1. **README 与搭建路径**：纠错、固定顺序、暴露 `scripts/` 能力
2. **依赖与编译文档**：`dependencies.md`、`build.sh` 参数表、Debug/测试说明
3. **示例与测试规范**：quickstart、示例 README、`testing.md`、CTest 收敛

**下期再议**：错误处理统一、接口易用性（回调、timeout、JSON/Optional API、Doxygen 全量等）。

按本期顺序推进，可在**不改 public API** 的前提下，显著降低环境搭建与首次跑通成本。

---

*本报告由静态审阅生成，未替代实际构建与测试执行验证。建议在整改后补充一次完整的 `run_example.sh` 与 `run_ut.sh` 冒烟记录。*
