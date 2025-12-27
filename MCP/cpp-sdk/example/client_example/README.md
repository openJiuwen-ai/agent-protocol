# MCP Client Example 说明

## 目录结构

示例代码已按功能模块拆分为三个独立的文件夹：

```
client_example/
├── tool_example/             # 初始化和工具相关示例
│   ├── tool_example.cpp      # 示例代码
│   ├── CMakeLists.txt        # 构建配置
│   └── run_example.sh        # 一键运行脚本
├── prompt_example/           # Prompt相关示例
│   ├── prompt_example.cpp    # 示例代码
│   ├── CMakeLists.txt        # 构建配置
│   └── run_example.sh        # 一键运行脚本
├── resource_example/         # Resource相关示例
│   ├── resource_example.cpp  # 示例代码
│   ├── CMakeLists.txt        # 构建配置
│   └── run_example.sh        # 一键运行脚本
└── run_all_examples.sh       # 运行所有示例的脚本
```

## 示例内容

### 1. tool_example - 初始化和工具示例
- 客户端初始化 (Initialize)
- 列出工具 (ListTools)
- 调用工具 (CallTool)

### 2. prompt_example - Prompt示例
- 列出Prompts (ListPrompts)
- 获取Prompt详情 (GetPrompt)

### 3. resource_example - Resource示例
- 订阅/取消订阅资源 (Subscribe/Unsubscribe)
- 列出资源 (ListResources)
- 读取资源 (ReadResource)
- 列出资源模板 (ListResourcesTemplates)

## 使用方法

### 运行单个示例

进入对应的示例文件夹，执行：

```bash
# 工具示例
cd tool_example
./run_example.sh

# Prompt示例
cd prompt_example
./run_example.sh

# Resource示例
cd resource_example
./run_example.sh
```

### 运行所有示例

在 client_example 目录下执行：

```bash
./run_all_examples.sh
```

## 前置条件

- 确保已编译 libmcp.so 并放在 `../../output/` 目录下
- 确保头文件在 `../../output/mcp/` 目录下
- MCP服务器运行在 `http://localhost:8000/mcp`
