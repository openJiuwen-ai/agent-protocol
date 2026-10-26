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
intelli_router/                  # SDK 模块根目录
├── intelli_router/              # SDK 源码包
│   ├── core/                    # 部署配置、路由状态与路由上下文
│   ├── strategy/                # 路由策略与策略工厂
│   │   ├── __init__.py          # 策略工厂（create_strategy 按名称创建策略）
│   │   ├── base_strategy.py     # 抽象策略基类
│   │   ├── simple_shuffle.py    # 随机选择策略
│   │   ├── lowest_latency.py    # 最低延迟策略
│   │   ├── tag_based.py         # 标签策略
│   │   ├── tag_filtered.py      # 标签过滤策略
│   │   ├── token_aware.py       # Token 感知策略
│   │   ├── rate_limit_aware.py  # RPM 限流策略
│   │   ├── ordered_failover.py  # 顺序故障转移策略
│   │   └── adaptive.py          # 自适应策略
│   ├── router/                  # 基础路由器与可靠性路由器
│   ├── cache/                   # LocalCache（LRU 本地缓存）
│   ├── health/                  # 后台健康检查
│   ├── observability/           # 事件总线、指标、Web 看板与日志钩子
│   ├── parser/                  # JSON / Markdown 输出解析器
│   ├── provider/                # 多厂商 Provider 适配器与注册表
│   └── utils/                   # 异常层次与媒体工具
├── examples/                    # 可运行示例（见下文"示例与测试"）
├── demo_adaptive_strategy.py    # 三种路由策略对比演示（直接可跑）
├── tests/                       # 单元测试（pytest）
├── API.md                       # API 参考
└── pyproject.toml               # 构建与依赖配置
```

## 路由策略介绍

本SDK实现了多种路由策略，以下介绍其中三种代表性的策略:

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

## 示例与测试

所有示例脚本都位于 `examples/` 目录下，请在模块根目录（`intelli_router/`）中运行。

### 端到端示例 (examples/demo_e2e.py)

演示 OpenAI / Anthropic / Gemini 真实调用、dead 端点自动 failover、全部端点不可用时抛出 `RouterError`。未配置对应 API Key 的场景会自动跳过:

**运行**:
```bash
export OPENAI_API_KEY="sk-..."
python examples/demo_e2e.py
```

**输出示例**（配置 `OPENAI_API_KEY` 后）:
```
intelli_router 端到端 Demo
==================================================

=== scene: OpenAI real ===
  response: 你好世界
  latency:  0.85s
  usage:    {'prompt_tokens': 21, 'completion_tokens': 8, 'total_tokens': 29}

=== scene: Failover ===
  primary deployment (dead):    openai-dead  → http://localhost:19999
  fallback deployment (healthy): openai-real  → https://api.openai.com
  response: 你好世界
  latency:  1.23s
  after call status:
    openai-dead: cooldown
    openai-real: healthy

=== scene: All dead ===
  caught expected RouterError ✓
  error: All deployments failed for model 'gpt-4o-mini'

=== done ===
```

配置 `ANTHROPIC_API_KEY` / `GEMINI_API_KEY` 后还会追加对应 Provider 的真实调用场景。

### Prometheus 指标示例 (examples/demo_prometheus.py)

使用模拟请求，无需真实 API Key:

```bash
pip install prometheus-client
python examples/demo_prometheus.py

# 另一个终端验证（默认端口 9091，可通过 METRICS_PORT 环境变量修改）
curl http://localhost:9091/metrics | grep intelli_router
```

### Web 指标看板示例 (examples/demo_web_dashboard.py)

同样使用模拟请求，无需真实 API Key。启动后在浏览器打开 http://localhost:8080（端口可通过 `WEB_PORT` 环境变量修改）:

```bash
python examples/demo_web_dashboard.py
```

### 路由策略对比演示 (demo_adaptive_strategy.py)

用模拟部署依次演示 Token 感知、RPM 限流、自适应三种策略的选路过程（无真实 API Key，无网络请求）。该脚本位于模块根目录（`intelli_router/` 下），自带包路径处理，可直接运行:

```bash
python demo_adaptive_strategy.py
```

输出依次为三段：Token 感知策略演示（选择 Token 剩余充足的部署）、RPM 限流策略演示（选择 RPM 配额充足的部署）、自适应策略演示（打印各部署的多维度评分与最终选择）。

### 运行测试

```bash
python3 -m pytest tests/ -v
```

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
# 原始 OpenAI 格式 chunk（dict）
async for chunk in router.stream_completion("gpt-4o-mini", messages):
    print(chunk["choices"][0]["delta"].get("content", ""), end="")

# 或类型化 chunk（AssistantMessageChunk 属性访问）
async for chunk in router.stream(messages, model="gpt-4o-mini"):
    print(chunk.content or "", end="")
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
| AWS Bedrock | `aws-bedrock` | Converse API（botocore SigV4 签名；`api_key` 可空，回退默认凭证链） |

各 Provider 适配器的差异详见 [API.md](API.md) 的「Provider 适配器」一节。

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
- [examples/demo_prometheus.py](examples/demo_prometheus.py) — Prometheus 指标导出演示
- [examples/demo_web_dashboard.py](examples/demo_web_dashboard.py) — Web 指标看板演示

