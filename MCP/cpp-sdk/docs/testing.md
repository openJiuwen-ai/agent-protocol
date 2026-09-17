# MCP C++ SDK 测试说明

## 概述

测试基于 **Google Test**，通过 **CTest** 发现与运行。测试源码位于 `tests/ut/`，默认**不编译**（需显式开启 `MCP_ENABLE_TESTS`）。

## 快速运行

```bash
cd MCP/cpp-sdk

# 1. Debug + 启用测试
./scripts/build.sh -t Debug --with-tests

# 2. 运行全部测试
cd build
ctest --output-on-failure -V
```

## 构建选项

通过 `scripts/build.sh` 传入：

| 选项 | CMake 变量 | 说明 |
|------|------------|------|
| `-u, --with-tests` | `MCP_ENABLE_TESTS=ON` | 编译单元测试 |
| `-c, --coverage` | `MCP_ENABLE_COVERAGE=ON` | 启用覆盖率（隐含 `--with-tests`） |
| `-s, --stdio` | `MCP_ENABLE_STDIO=ON` | 启用 stdio 相关 UT |
| `--no-client` | `MCP_BUILD_CLIENT=OFF` | 跳过 Client 模块测试 |
| `--no-server` | `MCP_BUILD_SERVER=OFF` | 跳过 Server 模块测试 |
| `--no-http` | `MCP_WITH_HTTP=OFF` | 跳过 HTTP 相关测试 |

当 Client + Server + HTTP 均启用时，会自动打开 **ST 集成测试**（`tests/ut/st/`，`MCP_ENABLE_ST_TESTS`）。

`stdio/` 目录下的测试需额外传入 `-s, --stdio`（`MCP_ENABLE_STDIO=ON`），且依赖 `MCP_BUILD_SERVER`。

## 覆盖率与报告

覆盖率需要 **三步**：带覆盖率标志编译 → 运行测试（生成 `.gcda`）→ 用 **gcovr** 生成 HTML。仅 `ctest` 不会产生可浏览的覆盖率报告。

**完整示例**（在 `MCP/cpp-sdk` 下）：

```bash
bash scripts/build.sh -t Debug --coverage
bash scripts/run_ut.sh
mkdir -p test_output/coverage
gcovr -r . --object-directory build \
  --filter 'src/' --filter 'include/' \
  --exclude 'tests/' --exclude 'third_party/' \
  --html --html-details -o test_output/coverage/index.html
# 浏览器打开 test_output/coverage/index.html
```

### 1. 带覆盖率编译

```bash
cd MCP/cpp-sdk
bash scripts/build.sh -t Debug --coverage
```

`--coverage` 隐含 `--with-tests`，会为 GCC/Clang 加上 `-fprofile-arcs -ftest-coverage`（见根目录 `CMakeLists.txt`）。

### 2. 运行测试（写入覆盖率数据）

任选其一：

```bash
# 方式 A：统一脚本（推荐，顺带生成测试结果 HTML）
bash scripts/run_ut.sh

# 方式 B：仅跑 CTest（集成测试建议串行）
cd build
ctest --output-on-failure -j1
```

测试执行后，`build/` 下会生成与目标对应的 `.gcda` 文件（需先完成步骤 1 的编译）。

### 3. 生成覆盖率 HTML 报告

`run_ut.sh` 会尝试安装 **gcovr**，但**不会**自动生成覆盖率 HTML，需在测试跑完后手动执行：

```bash
cd MCP/cpp-sdk
mkdir -p test_output/coverage

gcovr -r . \
  --object-directory build \
  --filter 'src/' \
  --filter 'include/' \
  --exclude 'tests/' \
  --exclude 'third_party/' \
  --html --html-details \
  -o test_output/coverage/index.html
```

若未安装 gcovr：

```bash
pip3 install --user gcovr
# 或：pip install --user gcovr
```

### 4. 查看报告

| 产物 | 路径 | 说明 |
|------|------|------|
| 测试结果 HTML | `test_output/ut/ut_result.html` | 由 `run_ut.sh` 生成（需 ctest ≥ 3.21 与 junit2html） |
| 覆盖率汇总 | `test_output/coverage/index.html` | 上一步 `gcovr` 生成，浏览器打开 |
| 覆盖率分文件详情 | `test_output/coverage/*.html` | `--html-details` 生成的各源文件页面 |

在机器上可用绝对路径打开，例如：

```text
file:///path/to/MCP/cpp-sdk/test_output/coverage/index.html
```

自定义输出目录（测试与覆盖率共用前缀）：

```bash
bash scripts/run_ut.sh -o my_report
mkdir -p my_report/coverage
gcovr -r . --object-directory build \
  --filter 'src/' --filter 'include/' \
  --exclude 'tests/' --exclude 'third_party/' \
  --html --html-details -o my_report/coverage/index.html
```

### 依赖说明

| 工具 | 用途 |
|------|------|
| `gcov` / `lcov`（随 GCC 工具链） | 编译与链接覆盖率插桩、生成 `.gcno`/`.gcda` |
| `gcovr` | 将 `.gcda` 聚合为 HTML |
| `junit2html` | 将 CTest JUnit XML 转为 `ut_result.html`（`run_ut.sh` 按需 pip 安装） |

## 测试模块结构

```
tests/ut/
├── client/          # 客户端、HTTP 传输、TLS（需 Client + Server + HTTP）
├── server/          # 服务端、Manager、HTTP Server（需 Server + HTTP）
├── shared/          # jsonrpc、base_session
├── auth/            # 鉴权
├── http/            # HTTP 客户端/服务端底层（需 Client + HTTP）
├── transport/       # stdio 传输层（需 Server）
├── stdio/           # stdio 传输扩展 UT（需 -s/--stdio 且 Server）
├── net/             # 网络层
├── event/           # 事件系统
├── log/             # 日志模块
└── st/              # 端到端集成（可选，需 Client + Server + HTTP）
```

说明：`transport/` 与 `stdio/` 均覆盖 stdio 相关能力；默认 `--with-tests` 会编译 `transport/`，`stdio/` 仅在传入 `-s, --stdio` 时加入构建。

## 常见问题

### 构建目录不存在

```
Error: Build directory not found
```

先执行：`./scripts/build.sh -t Debug --with-tests`

### 无测试用例

确认构建时使用了 `--with-tests`，且未使用 `--no-client` 等裁剪掉被测模块。

### 集成测试端口冲突

部分测试使用固定端口（如 8001）。并行运行或端口占用时可能失败；`mcp_client_tests` 中的集成用例建议串行执行：

```bash
cd build
ctest --output-on-failure -j1
# 或单独跑某一模块：
ctest -R mcp_client_tests --output-on-failure -j1
```

## 与示例的关系

- **单元测试**：验证 SDK 内部模块与协议逻辑。
- **示例冒烟**：`scripts/run_example.sh` 验证可执行示例链路，不属于 CTest。

两者互补，发布前建议均跑通。
