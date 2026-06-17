# A2A C++ SDK 依赖说明

## 依赖分类

| 类别 | 依赖 | 获取方式 |
|------|------|----------|
| **系统（需手动/脚本安装）** | OpenSSL、libcurl、libevent、nlohmann_json | `scripts/install_deps.sh` 或系统包管理器 |
| **CMake 自动拉取** | http_parser、Google Test 等 | 构建时 `third_party/third_party.cmake`（系统包优先，否则 FetchContent） |
| **测试专用** | Google Test、gcovr | 启用 `--with-tests` / `run_ut.sh` 时按需安装 |

## 版本要求

| 依赖 | 最低版本 | 用途 |
|------|----------|------|
| C++ 编译器 | C++17 | 语言标准 |
| CMake | 3.16 | 构建系统 |
| OpenSSL | 1.1.1 | TLS、加密 |
| libcurl | 7.x+ | HTTP Client 传输 |
| nlohmann_json | 3.11.2 | JSON 序列化 |
| libevent | 2.1.12 | 事件循环（Server I/O） |
| http_parser | 2.9.4 | HTTP 解析 |

## 系统包安装

### 一键检查与安装（推荐）

```bash
cd A2A/cpp-sdk
./scripts/install_deps.sh
```

脚本会检测 Ubuntu/Debian、RHEL/CentOS/Fedora/EulerOS、Arch 等，并安装 **libcurl**、**OpenSSL**、**libevent**、**nlohmann_json** 等开发包。

### Ubuntu / Debian（手动）

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake \
  libssl-dev \
  libcurl4-openssl-dev \
  libevent-dev \
  nlohmann-json3-dev
```

### CentOS / RHEL / Fedora / EulerOS（手动）

```bash
sudo yum install -y \
  gcc-c++ cmake \
  openssl-devel \
  libcurl-devel \
  libevent-devel
```

### Arch Linux（手动）

```bash
sudo pacman -S --needed base-devel cmake openssl curl libevent nlohmann-json
```

## 验证依赖

```bash
# OpenSSL
openssl version

# libcurl
curl-config --version

# CMake
cmake --version
```

## 说明

- 其余第三方库在首次 `cmake` 配置时由 `third_party/third_party.cmake` 处理，一般无需单独安装。
- 若需离线构建，可预先安装上述系统包，并配置 CMake 使用系统提供的 json/libevent 等（见 `A2A_USE_SYSTEM_*` 选项）。
- 从其他机器同步工程时，建议清理 `third_party/*-subbuild` 后再配置，避免 CMakeCache 路径不一致。
