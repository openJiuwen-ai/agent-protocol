# MCP C++ SDK


<!-- omit in toc -->
## 目录

- [MCP C++ SDK](#mcp-c-sdk)
    - [概述](#概述)
    - [构建指南](#构建指南)
        - [构建依赖](#构建依赖)
        - [编译](#编译)
    - [快速启动](#快速启动)
        - [运行 MCP Client](#运行-mcp-client)
        - [运行 MCP Server](#运行-mcp-server)
    - [License](#license)
    - [Copyright](#copyright)
    - [Third-Party Notices](#third-party-notices)

## 概述

MCP C++ SDK 是 Model Context Protocol 的 C++ 实现，提供了完整的 MCP 客户端和服务器功能。通过该 SDK，您可以：

- 构建高性能的 MCP 客户端，连接任意 MCP 服务器
- 创建稳定的 MCP 服务器，暴露资源、工具和提示
- 支持标准传输协议，包括 stdio 和 HTTP
- 处理所有 MCP 协议消息和生命周期事件

## 构建指南

### 构建依赖

- C++ 17 或以上编译器
- CMake >= 3.16
- 操作系统：兼容 Linux

- openssl >= 1.1.1n
- openssl-devel >= 1.1.1n
- curl >= 8.12.0
- curl-devel >= 8.12.0
- nlohmann_json >= 3.11.2
- libevent >= 2.1.12
- http_parser >= 2.9.4

### 编译
使用对应平台的包管理工具进行安装

Ubuntu/Debian
bash
sudo apt-get install openssl-lib
sudo apt-get install openssl-devel

sudo apt-get install libcurl
sudo apt-get install libcurl-devel

CentOS/RHEL
bash
sudo yum install openssl-lib
sudo yum install openssl-devel

sudo yum install libcurl
sudo yum install libcurl-devel

```bash
./build.sh
```

构建成果为output目录下libmcp.so。

## 快速启动

本节用于指导你以最简步骤跑通 MCP C++ SDK 的完整示例流程，包括启动 Server 以及运行 Client 示例。

### 运行 MCP Client

具体可以参考 Client 示例目录，直接执行：

```bash
./example/client_example/tool_example/run_example.sh
```

说明：

- 该脚本会一次性运行所有 Client 示例
- 包括 Tool、Resource、Prompt 等示例
- 每个示例都会连接已启动的 MCP Server 并输出调用结果

### 运行 MCP Server

具体方案可参考 Server 示例目录，直接执行启动脚本：

```bash
./example/server_example/run_example.sh
```

说明：

- 该脚本会自动完成示例 Server 的编译与启动
- Server 启动后会监听本地端口，等待 Client 连接
- 请保持该终端窗口运行，不要退出

## License

本项目依据Apache-2.0许可证授权。


## Copyright

Copyright (c) 2025 Huawei Technologies Co., Ltd.
All rights reserved.


## Third-Party Notices

本项目包含或依赖第三方开源软件，其版权和许可证信息均归原作者所有，并在相应文件中予以说明。
