# MCP C++ SDK 依赖说明

## 依赖分类

| 类别 | 依赖 | 获取方式 |
|------|------|----------|
| **系统（需预先安装）** | C++ 编译器、CMake | 系统包管理器（`install_deps.sh` 不安装） |
| **系统（脚本可安装）** | OpenSSL、libcurl | `bash scripts/install_deps.sh` 或系统包管理器 |
| **CMake 自动拉取** | nlohmann_json、json-schema-validator、libevent、http_parser 等 | `bash scripts/build.sh` 时由 `third_party/third_party.cmake` 处理（系统包优先，否则 FetchContent 到 `third_party/`） |
| **测试专用** | Google Test、gcovr、junit2html | 启用 `--with-tests` / `run_ut.sh` 时按需安装 |

## 版本要求

| 依赖 | 最低版本 | 用途 |
|------|----------|------|
| C++ 编译器 | C++17 | 语言标准 |
| CMake | 3.15 | 构建系统 |
| OpenSSL | 1.1.1 | TLS、加密 |
| libcurl | 8.12 | HTTP Client 传输 |
| nlohmann_json | 3.11.2 | JSON 序列化 |
| nlohmann-json-schema-validator | 2.3.0 | Schema 校验 |
| libevent | 2.1.12 | 事件循环 |
| http_parser | 2.9.4 | HTTP 解析 |

## 推荐构建顺序

在 `MCP/cpp-sdk` 目录下：

```bash
# 1. 安装 OpenSSL、libcurl 开发包（需已安装 gcc/g++、cmake）
bash scripts/install_deps.sh

# 2. 编译 SDK（产物：output/lib/libmcp.so）
bash scripts/build.sh

# 3. 构建并运行示例（须先完成步骤 2）
sh scripts/run_example.sh
```

`install_deps.sh` 与 `build.sh` 为 Bash 脚本，请使用 `bash` 调用；`run_example.sh` 为 POSIX `sh` 脚本。

## 系统包安装

### 一键检查与安装（OpenSSL / libcurl）

```bash
cd MCP/cpp-sdk
bash scripts/install_deps.sh
```

脚本会检测 Ubuntu/Debian、RHEL/CentOS/Fedora/EulerOS、Arch 等，并安装 **libcurl** 与 **OpenSSL** 开发包。

**不包含**：`gcc-c++`、`cmake`、`nlohmann_json` 等，需单独安装或由 `build.sh` 的 CMake 自动处理。

### Ubuntu / Debian（手动，含编译工具链）

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake \
  libssl-dev \
  libcurl4-openssl-dev
```

可选：安装系统 `nlohmann_json`，避免从网络拉取：

```bash
sudo apt-get install -y nlohmann-json3-dev
```

### CentOS / RHEL / Fedora / EulerOS（手动）

```bash
sudo yum install -y \
  gcc-c++ cmake \
  openssl-devel \
  libcurl-devel
```

EulerOS / openEuler 上 `nlohmann_json` 包名可能为 `nlohmann-json-devel` 等，视发行版而定。

### Arch Linux（手动）

```bash
sudo pacman -S --needed base-devel cmake openssl curl
```

## nlohmann_json 解析规则

主工程与示例对 `nlohmann_json` 均采用**系统优先、third_party 回退**策略：

| 阶段 | 行为 |
|------|------|
| `bash scripts/build.sh` | `third_party/third_party.cmake` 先 `find_package` / 查找系统包；找不到时 FetchContent 到 `third_party/nlohmann_json-src` |
| 示例构建（`run_example.sh`） | `example/cmake/ExampleCommon.cmake` 先查找系统头文件或 CMake 包；找不到时使用 `third_party/nlohmann_json-src/include` |

说明：

- 系统已安装 nlohmann 开发包时，**不会**生成 `third_party/nlohmann_json-src`，主工程与示例均使用系统库，属正常情况。
- 系统未安装时，须先成功执行 `build.sh`，确保 `third_party/nlohmann_json-src` 已拉取，再运行 `run_example.sh`。
- 强制从源码拉取（忽略系统包）：`cmake -DMCP_USE_SYSTEM_NLOHMANN_JSON=OFF ...`（经 `build.sh` 配置时传入）。

## 验证依赖

```bash
# 编译器
g++ --version

# CMake
cmake --version

# OpenSSL
openssl version

# libcurl
curl-config --version

# nlohmann_json（可选，系统已装时）
ls /usr/include/nlohmann/json.hpp 2>/dev/null || \
  ls third_party/nlohmann_json-src/include/nlohmann/json.hpp
```

## 说明

- 除 OpenSSL、libcurl 外，其余第三方库在首次 `cmake` 配置时由 `third_party/third_party.cmake` 处理，一般无需单独安装。
- 若需离线构建，可预先安装系统包（含 nlohmann_json、libevent 等），并配置 CMake 使用系统提供的库（见 `MCP_USE_SYSTEM_*` 选项）。
- 构建产物：`output/lib/libmcp.so`、公共头文件 `include/mcp/`；部分第三方源码位于 `third_party/`（视系统环境而定）。
