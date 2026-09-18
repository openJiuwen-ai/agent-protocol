# IntelliRouter - 轻量LLM代理SDK

## 项目简介

**IntelliRouter** 是一个轻量级LLM代理SDK，实现多部署端点管理和智能路由。

核心特性:
- **API池化**: 单一模型名 → 多部署端点映射
- **策略插拔**: 支持SimpleShuffle、LowestLatency、TagBased、TokenAware、RateLimitAware、Adaptive等路由策略
- **状态管理**: 部署状态追踪、Token/RPM实时监控，失败计数，冷却机制
- **健康检查**: 后台健康检查循环
- **重试机制**: 自动重试机制

## 代码结构设计

```
intelli_router/
├── __init__.py              公共接口
├── core/
│   ├── __init__.py
│   ├── deployment.py 部署配置
│   ├── state.py      状态管理
│   ├── context.py 路由上下文
│   └── deployment.py 部署状态枚举
├── strategy/ 路由策略
│   ├── __init__.py 策略工厂
│   ├── base_strategy.py 抽象策略基类
│   ├── simple_shuffle.py 随机选择策略
│   ├── lowest_latency.py 最低延迟策略
│   ├── tag_based.py 标签策略
│   ├── token_aware.py Token感知策略
│   ├── rate_limit_aware.py RPM限流策略
│   └──── adaptive.py 自适应策略
├── router/ 路由器
│   ├── __init__.py
│   ├── base_router.py 基础路由器
│   ├── reliable_router.py 可靠性路由器
├── cache/ 缓存
│   ├── __init__.py
│   └──── local_cache.py 本地缓存 (LRU)
├── health/ 健康检查
│   ├── __init__.py
│   └──── checker.py SDK健康检查
├── utils/ 工具函数
│   └──── exceptions.py 异常层次
├── tests/ 测试
      └── sdk_proxy_demo/
        ├── __init__.py
        ├── conftest.py pytest配置
        ├── test_api_pooling.py API池化测试
        ├── test_routing_strategies.py 路由策略测试
        ├── test_state_management.py 状态管理测试
        ├── test_caching.py 缓存测试
        ├── test_concurrent_requests.py 并发请求测试
        ├── test_adaptive_strategy.py 自适应策略测试
        └── utils/
            ├── __init__.py
            ├── helpers.py 测试辅助函数
├── demo_agent_core.py Agent Core演示
├── demo_real_llm.py 真实LLM测试Demo
├── demo_adaptive_strategy.py 自适应策略演示
├── demo_real_llm_adaptive.py 真实LLM自适应策略Demo
```

## 路由策略介绍

本SDK实现了三种路由策略:

### 1. Token感知策略 (TokenAwareStrategy)

**场景**: Token耗尽时无缝切换

**策略**:
1. 过滤可用部署
2. 按Token剩余量排序
3. 优先选择Token充足的部署
4. Token耗尽时无缝切换到下一个

**关键参数**:
- `token_threshold`: Token阈值 (默认1000)
- `exploration_ratio`: 探索比例 (默认0.1)

### 2. RPM限流策略 (RateLimitAwareStrategy)

**场景**: RPM限流时负载均衡

**策略**:
1. 过滤可用部署
2. 按RPM剩余配额排序
3. 优先选择RPM配额充足的部署
4. RPM耗尽时无缝切换

**关键参数**:
- `rpm_threshold`: RPM阈值 (默认10)
- `exploration_ratio`: 探索比例 (默认0.1)

### 3. 自适应策略 (AdaptiveStrategy)

**场景**: 多级决策树

**策略**:
1. 健康检查 → 剔除不健康部署
2. Token剩余 → 优先Token充足的
3. RPM剩余 → 优先RPM配额充足的
4. 延迟收益 → 优先低延迟的
5. 随机回退 → 随机选择

**评分公式**:
```
score = w_health * health_score
      + w_token * token_score
      + w_rpm * rpm_score
      + w_latency * latency_score
```

**关键参数**:
- `w_health`: 健康权重 (默认1.0)
- `w_token`: Token权重 (默认0.5)
- `w_rpm`: RPM权重 (默认0.3)
- `w_latency`: 延迟权重 (默认0.2)

## 测试Demo使用方法

### 基础Demo (demo_adaptive_strategy.py)

**运行**:
```bash
python3 intelli_router/demo_adaptive_strategy.py
```

**输出**:
```
=== Token感知策略演示 ===
选择的部署: dep3
  - Token剩余: inf
  - Token使用率: 0.00%

=== 自适应策略演示 ===
部署评分:
  dep1: 得分=1.999 (健康, Token剩余5000, RPM剩余30, 延迟0.005s)
  dep2: 得分=2.000 (健康, Token剩余2000, RPM剩余50, 延迟0.002s) ← 最高
  dep3: 得分=0.800 (不健康)
选择的部署: dep2

=== Token耗尽无缝切换演示 ===
Token快耗尽时选择部署: dep1 (剩余100)
第1次请求后: Token剩余50
第2次请求后: Token耗尽, 切换到dep2
```

### 真实LLM Demo (demo_real_llm_adaptive.py)

**配置**:
```python
# 真实API Key (所有模型共用)
REAL_API_KEY = "sk-xxxx"

# 模型端口配置
MODEL_PORT_MAP = {
    "MiniMax-M2.7": [8001, 8002, 8003, 8004],
    "Qwen3.5-122B-A10B": [8005, 8006, 8007, 8008],
    "GLM-5": [8009, 8010, 8011, 8012],
    "GLM-5.1": [8013, 8014, 8015, 8016],
}
```

**运行**:
```bash
python3 intelli_router/demo_real_llm_adaptive.py
```

**输出**:
```
--- Task task-001: GLM-5.1 ---
  Selected deployment: GLM-5.1-port8013
  Latency: 10.84s

--- Task task-002: GLM-5.1 ---
  Selected deployment: GLM-5.1-port8013
  Latency: 11.29s

--- Task task-005: Qwen3.5-122B-A10B ---
  Selected deployment: Qwen3.5-122B-A10B-port8005
  Latency: 3.40s

[Agent Core] Dispatching 10 tasks concurrently...
[Agent Core] Completed 10 tasks in 13.42s
[Agent Core] Success rate: 10/10
```

### 测试文件 (test_adaptive_strategy.py)

**运行**:
```bash
python3 -m pytest intelli_router/tests/sdk_proxy_demo/test_adaptive_strategy.py -v
```

## 项目结构

详见 `sdk_llm` 目录树:

- `core/`: 核心和状态管理
- `strategy/`: 路由策略实现
- `router/`: 路由器实现
- `cache/`: 缓存实现
- `health/`: 健康检查实现
- `tests/`: 测试用例
- `demo_*`: 演示脚本

## 快速上手

```python
import asyncio
from intelli_router import ReliableRouter, Deployment

# 1. 定义部署端点
dep = Deployment(
    id="openai-1",
    model_name="gpt-4o-mini",
    api_key="sk-...",
    api_base="https://api.openai.com",
    provider="openai",
)

# 2. 创建路由器
router = ReliableRouter(deployments=[dep], strategy="adaptive")

# 3. 调用
async def main():
    resp = await router.completion(
        "gpt-4o-mini",
        [{"role": "user", "content": "你好"}],
    )
    print(resp["choices"][0]["message"]["content"])
    await router.close()

asyncio.run(main())
```

Streaming 用法：

```python
async for chunk in router.stream("gpt-4o-mini", messages):
    print(chunk["choices"][0]["delta"].get("content", ""), end="")
```

多 Provider failover：

```python
deps = [
    Deployment(id="main", model_name="gpt-4o-mini", api_key="sk-...",
               api_base="https://api.openai.com", provider="openai"),
    Deployment(id="backup", model_name="gpt-4o-mini", api_key="sk-...",
               api_base="https://api.openai.com", provider="openai"),
]
router = ReliableRouter(deployments=deps, num_retries=1, strategy="adaptive")
# 主端点失败时自动切换到备用端点
resp = await router.completion("gpt-4o-mini", messages)
```

## 多 Provider 适配

IntelliRouter 以 OpenAI 格式为内部标准格式，通过 Provider Adapter 自动完成与各厂商 API 的协议转换。调用者只需在 `Deployment` 中指定 `provider` 字段，无需关心底层协议差异。

支持的 Provider：

| Provider | `provider` 标识 | 说明 |
|----------|----------------|------|
| OpenAI | `openai` | 基准实现，直接透传 |
| Anthropic | `anthropic` | 完整协议转换（Messages API） |
| Google Gemini | `google-gemini` | 完整协议转换（generateContent） |
| DeepSeek | `deepseek` | OpenAI 兼容 + reasoning_content 字段注入 |
| DashScope (阿里云) | `dashscope` | OpenAI 兼容，端点路径不同 |
| SiliconFlow (硅基流动) | `siliconflow` | OpenAI 兼容 + tool_calls 字段清洗 |
| Zhipu (智谱) | `zhipu` | OpenAI 兼容，端点路径不同 |
| InferenceAffinity (vLLM) | `inference-affinity` | OpenAI 兼容 + KV cache 共享 |
| AWS Bedrock | `aws-bedrock` | 待实现（需 SigV4 签名） |

各 Provider 协议差异详见 `docs/PROVIDER_API_DIFFERENCES.md`。

## 输出解析器 (Parser)

内置两种输出解析器，用于将 LLM 响应解析为结构化数据：

- **JsonOutputParser**：从 LLM 回复中提取 JSON，支持裸 JSON 和 ` ```json ` 代码块包裹格式，支持流式增量解析
- **MarkdownOutputParser**：将 Markdown 文本解析为结构化 `MarkdownContent`，包含 headers、code_blocks、links、images、tables、lists 等元素

```python
from intelli_router import JsonOutputParser, MarkdownOutputParser

# JSON 解析
parser = JsonOutputParser()
result = parser.parse(assistant_message)  # -> dict/list

# Markdown 解析
md_parser = MarkdownOutputParser()
content = md_parser.parse(assistant_message)  # -> MarkdownContent
print(content.headers)       # 所有标题
print(content.code_blocks)   # 所有代码块
```

## 类型定义 (Types)

`intelli_router.types` 提供统一的类型化响应对象：

- **`ToolCall`** — 工具调用信息（id、name、arguments）
- **`UsageMetadata`** — Token 用量统计（input_tokens、output_tokens、total_tokens）
- **`AssistantMessage`** — 完整的助手响应（content、tool_calls、usage）
- **`AssistantMessageChunk`** — 流式响应片段，支持 `+` 运算符累加合并

```python
from intelli_router import AssistantMessage, AssistantMessageChunk

# chunk 累加
full = AssistantMessageChunk()
async for chunk in router.stream(...):
    full = full + chunk  # 自动合并 content、tool_calls、usage
```

## 更多文档

- [API.md](API.md) — 完整 API 参考
- [examples/demo_e2e.py](examples/demo_e2e.py) — 端到端演示（OpenAI / Anthropic / Gemini / Failover）
- [docs/](docs/) — 设计文档（Provider 差异对比、可观测性设计等）

