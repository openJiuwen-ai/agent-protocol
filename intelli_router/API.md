# IntelliRouter API 接口文档

## 目录

- [核心类](#核心类)
- [Provider 适配器](#provider-适配器)
- [类型模块](#类型模块)
- [输出解析器](#输出解析器)
- [路由策略](#路由策略)
- [策略工厂函数](#策略工厂函数)
- [状态管理](#状态管理)
- [缓存](#缓存)
- [健康检查](#健康检查)
- [异常](#异常)
- [使用示例](#使用示例)

---

## 核心类

### `Deployment`

部署配置类 - 表示一个 API 端点。

**构造函数**:
```python
Deployment(
    model_name: str,                          # 模型名称
    api_key: str,                             # API密钥
    api_base: str,                            # API端点URL
    id: str = None,                           # 部署ID (可选，自动生成8位随机ID)
    status: DeploymentStatus = DeploymentStatus.HEALTHY,  # 部署状态
    consecutive_failures: int = 0,            # 连续失败次数
    cooldown_until: Optional[float] = None,   # 冷却时间戳
    tags: List[str] = [],                     # 标签列表
    tpm: Optional[int] = None,                # Token Per Minute限制
    rpm: Optional[int] = None,                # Request Per Minute限制
    timeout: Optional[float] = None,          # 请求超时
    provider: str = "openai",                 # Provider 类型 (见 Provider 适配器)
)
```

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `is_available` | `is_available(now: float) -> bool` | 检查部署是否可用（非冷却中） |
| `to_dict` | `to_dict() -> Dict[str, Any]` | 序列化为字典 |
| `from_dict` | `(data: Dict[str, Any]) -> Deployment` | 从字典反序列化 (类方法) |

---

### `DeploymentStatus` (枚举)

部署状态枚举。所有失败均为暂时性的，通过冷却机制自愈。

```python
class DeploymentStatus(Enum):
    HEALTHY = "healthy"    # 健康
    COOLDOWN = "cooldown"  # 冷却中（超时后自愈，时长 = cooldown_time × 连续失败次数）
```

---

### `ReliableRouter`

可靠性路由器 - 集成状态管理、策略选择、健康检查的重试路由器。

**构造函数**:
```python
ReliableRouter(
    deployments: List[Deployment],              # 部署列表
    strategy: Union[StrategyType, RoutingStrategy] = "simple-shuffle",  # 路由策略
    num_retries: int = 3,                       # 重试次数
    timeout: float = 30.0,                      # 请求超时
    cooldown_time: float = 60.0,                # 冷却时间基数（秒），实际时长 = cooldown_time × 连续失败次数
    enable_health_check: bool = False,          # 启用健康检查
    health_check_interval: float = 300,         # 健康检查间隔（秒）
    cache: Optional[LocalCache] = None,         # 缓存实例
    **strategy_kwargs                           # 策略参数
)
```

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `completion` | `async completion(model: str, messages: List[Dict], **kwargs) -> Dict` | 发送 completion 请求，返回 OpenAI 格式原始字典 |
| `invoke` | `async invoke(messages, *, tools=None, temperature=None, top_p=None, model=None, max_tokens=None, stop=None, output_parser=None, **kwargs) -> AssistantMessage` | 类型化 completion 请求，返回 `AssistantMessage` |
| `stream` | `async stream(messages, *, tools=None, temperature=None, top_p=None, model=None, max_tokens=None, stop=None, **kwargs) -> AsyncIterator[AssistantMessageChunk]` | 流式 completion，逐 chunk 返回 `AssistantMessageChunk` |
| `batch_completion` | `async batch_completion(requests: List[Dict], max_concurrent: int = 10) -> List[Any]` | 批量 completion 请求（并发控制） |
| `get_deployments_for_model` | `get_deployments_for_model(model: str) -> List[Deployment]` | 获取模型的部署列表 |
| `get_model_list` | `get_model_list() -> List[str]` | 获取已注册模型名列表 |
| `update_deployments` | `update_deployments(new_deployments: List[Deployment]) -> None` | 动态更新部署列表 |
| `get_stats` | `get_stats() -> Dict[str, Any]` | 获取路由器统计信息 |
| `close` | `async close() -> None` | 关闭底层 HTTP 客户端，释放连接池 |

**`invoke()` 参数说明**:

| 参数 | 类型 | 说明 |
|------|------|------|
| `messages` | `List[Dict[str, Any]]` | 消息列表 |
| `tools` | `Optional[List[Dict]]` | 工具定义 |
| `temperature` | `Optional[float]` | 温度 |
| `top_p` | `Optional[float]` | top_p |
| `model` | `Optional[str]` | 模型名（省略则使用第一个 deployment 的模型） |
| `max_tokens` | `Optional[int]` | 最大 token 数 |
| `stop` | `Optional[Union[str, List[str]]]` | 停止序列 |
| `output_parser` | `Optional[BaseOutputParser]` | 输出解析器 |

**异步上下文管理器**:
```python
async with ReliableRouter(...) as router:
    response = await router.completion(...)
```

---

### `BaseRouter`

基础路由器 - 提供 API 池化和请求转发能力。

**构造函数**:
```python
BaseRouter(
    deployments: List[Deployment],          # 部署列表
    num_retries: int = 0,                   # 重试次数
    timeout: float = 30.0,                  # 请求超时
    cache: Optional[LocalCache] = None      # 缓存实例
)
```

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `completion` | `async completion(model: str, messages: List[Dict], deployment: Optional[Deployment] = None, **kwargs) -> Dict` | 发送 completion 请求（可指定部署） |
| `acompletion_stream` | `async acompletion_stream(model: str, messages: List[Dict], deployment: Optional[Deployment] = None, **kwargs) -> AsyncIterator[Dict]` | 流式 SSE completion，yield OpenAI 格式 chunk |
| `completion_with_fallback` | `async completion_with_fallback(model: str, messages: List[Dict], fallback: Optional[Dict[str, str]] = None, **kwargs) -> Any` | 带降级模型的 completion |
| `get_deployments_for_model` | `get_deployments_for_model(model: str) -> List[Deployment]` | 获取模型部署列表 |
| `get_model_list` | `get_model_list() -> List[str]` | 获取模型名列表 |
| `get_deployment_configs` | `get_deployment_configs() -> List[Dict[str, Any]]` | 获取所有部署配置 |
| `get_deployment_config_by_model` | `get_deployment_config_by_model(model: str) -> List[Dict[str, Any]]` | 获取指定模型的部署配置 |
| `close` | `async close() -> None` | 关闭底层 HTTP 客户端 |

---

## Provider 适配器

Provider 适配器将不同 LLM 提供商的请求/响应格式统一转换为 OpenAI 兼容格式。路由器通过 `Deployment.provider` 字段自动选择对应的适配器。

### `BaseProviderAdapter` (抽象基类)

所有 Provider 适配器的基类。

```python
class BaseProviderAdapter(ABC):
    @abstractmethod
    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        """构建 API 请求 URL"""

    @abstractmethod
    def get_headers(self, deployment: Deployment) -> Dict[str, str]:
        """构建请求头"""

    def transform_request(
        self, model: str, messages: List[Dict], deployment: Deployment, **kwargs
    ) -> Dict[str, Any]:
        """将 OpenAI 格式请求转换为目标 Provider 格式（默认透传）"""

    def transform_response(
        self, raw_response: Dict[str, Any], model: str, deployment: Deployment
    ) -> Dict[str, Any]:
        """将目标 Provider 响应转换为 OpenAI 格式（默认透传）"""

    def transform_stream_chunk(
        self, chunk: Dict[str, Any], model: str, deployment: Deployment
    ) -> Optional[Dict[str, Any]]:
        """将流式 chunk 转换为 OpenAI 格式（默认透传）"""
```

**辅助静态方法**:

| 方法 | 说明 |
|------|------|
| `_build_completion_response(id, model, message, finish_reason, prompt_tokens, completion_tokens)` | 构建 OpenAI 格式完整响应 |
| `_build_chunk_response(model, delta, finish_reason, id, usage)` | 构建 OpenAI 格式流式 chunk |
| `_extract_system_messages(messages)` | 分离 system 消息和其他消息 |
| `sanitize_tool_calls(messages)` | 清理消息中 tool_calls 的非标准字段 |

---

### 注册表函数

```python
from intelli_router.provider.registry import register_provider, get_provider_adapter

register_provider(name: str, adapter_cls: Type[BaseProviderAdapter]) -> None
get_provider_adapter(provider: str) -> BaseProviderAdapter
```

---

### 内置 Provider

| Provider 名称 | 适配器类 | 说明 |
|---------------|---------|------|
| `"openai"` | `OpenAIProviderAdapter` | OpenAI 及兼容端点 (默认) |
| `"anthropic"` | `AnthropicProviderAdapter` | Anthropic Claude Messages API |
| `"google-gemini"` | `GeminiProviderAdapter` | Google Gemini API |
| `"aws-bedrock"` | `BedrockProviderAdapter` | AWS Bedrock (需 SigV4，暂未实现) |
| `"deepseek"` | `DeepSeekProviderAdapter` | DeepSeek API (继承 OpenAI，自动补 reasoning_content) |
| `"siliconflow"` | `SiliconFlowProviderAdapter` | SiliconFlow (继承 OpenAI，清理 tool_calls) |
| `"inference-affinity"` | `InferenceAffinityProviderAdapter` | InferenceAffinity (支持 cache_sharing / session_id) |
| `"dashscope"` | `DashScopeProviderAdapter` | 阿里云 DashScope (URL: `/compatible-mode/v1/chat/completions`) |

---

## 类型模块

### `ToolCall`

工具调用。

```python
@dataclass
class ToolCall:
    id: str                        # 调用ID
    type: str = "function"         # 调用类型
    name: str = ""                 # 函数名
    arguments: str = ""            # JSON 参数字符串
    index: Optional[int] = None    # 流式中的索引
```

---

### `UsageMetadata`

Token 使用统计。

```python
@dataclass
class UsageMetadata:
    model_name: Optional[str] = None   # 模型名
    input_tokens: int = 0              # 输入 token 数
    output_tokens: int = 0             # 输出 token 数
    total_tokens: int = 0              # 总 token 数 (自动计算)
    cache_tokens: int = 0              # 缓存 token 数
```

---

### `AssistantMessage`

类型化的助手回复。由 `ReliableRouter.invoke()` 返回。

```python
@dataclass
class AssistantMessage:
    content: str                                       # 文本内容
    tool_calls: Optional[List[ToolCall]] = None        # 工具调用列表
    usage_metadata: Optional[UsageMetadata] = None     # Token 使用统计
    finish_reason: str = "stop"                        # 结束原因 ("stop" / "tool_calls" / "length")
    reasoning_content: Optional[str] = None            # 思维链内容 (DeepSeek 等)
```

---

### `AssistantMessageChunk`

流式消息片段。由 `ReliableRouter.stream()` 逐个 yield。支持通过 `+` 运算符累积。

```python
@dataclass
class AssistantMessageChunk:
    content: str = ""                                  # 文本片段
    reasoning_content: Optional[str] = None            # 思维链片段
    tool_calls: Optional[List[ToolCall]] = None        # 工具调用片段
    usage_metadata: Optional[UsageMetadata] = None     # Token 使用统计 (通常在最后一个 chunk)
    finish_reason: Optional[str] = None                # 结束原因 (通常在最后一个 chunk)
```

**累积用法**:
```python
accumulated = None
async for chunk in router.stream(messages=[...]):
    accumulated = chunk if accumulated is None else accumulated + chunk
# accumulated 包含完整的 content, tool_calls, finish_reason
```

---

### `ImageGenerationResponse`

图片生成响应。

```python
@dataclass
class ImageGenerationResponse:
    model: str              # 模型名
    images: List[str]       # 图片 URL 或 base64 列表
```

---

### `AudioGenerationResponse`

音频生成响应。

```python
@dataclass
class AudioGenerationResponse:
    model: str                            # 模型名
    audio_url: Optional[str] = None       # 音频 URL
    audio_data: Optional[bytes] = None    # 音频二进制数据
    format: Optional[str] = None          # 音频格式
```

---

### `VideoGenerationResponse`

视频生成响应。

```python
@dataclass
class VideoGenerationResponse:
    model: str                            # 模型名
    video_url: str                        # 视频 URL
    duration: Optional[int] = None        # 时长（秒）
    resolution: Optional[str] = None      # 分辨率
    format: str = "mp4"                   # 视频格式
```

---

### `StrategyType`

策略类型字面量。

```python
StrategyType = Literal[
    "simple-shuffle",
    "lowest-latency",
    "tag-based",
    "token-aware",
    "rate-limit-aware",
    "adaptive"
]
```

---

### `RoutingContext`

路由上下文 - 请求级别信息。

```python
@dataclass
class RoutingContext:
    model: str                              # 请求模型
    messages: List[Dict[str, str]]          # 消息列表
    kwargs: Dict[str, Any]                  # 其他参数
    selected_deployment: Optional[Deployment]   # 选中的部署
    attempted_deployments: List[str]            # 尝试过的部署ID
    start_time: float                       # 开始时间
    end_time: Optional[float]               # 结束时间
    response: Optional[Any]                 # 响应
    error: Optional[Exception]              # 错误
    request_tags: List[str]                 # 请求标签
```

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `mark_attempt` | `mark_attempt(deployment: Deployment) -> None` | 标记已尝试的部署 |
| `set_success` | `set_success(deployment: Deployment, response: Any) -> None` | 设置成功结果 |
| `set_failure` | `set_failure(error: Exception) -> None` | 设置失败结果 |
| `to_log_dict` | `to_log_dict() -> Dict[str, Any]` | 转换为日志字典 |

**属性**:

| 属性 | 类型 | 说明 |
|------|------|------|
| `latency` | float | 请求延迟（只读，`end_time - start_time`） |

---

## 输出解析器

### `BaseOutputParser` (抽象基类)

输出解析器基类。可传入 `ReliableRouter.invoke()` 的 `output_parser` 参数。

```python
class BaseOutputParser(ABC):
    @abstractmethod
    async def parse(self, inputs: Any) -> Any:
        """解析完整输出"""

    @abstractmethod
    async def stream_parse(self, streaming_inputs: AsyncIterator) -> AsyncIterator[Any]:
        """解析流式输出"""
```

---

### `JsonOutputParser`

JSON 输出解析器 - 从 LLM 输出中提取 JSON。

```python
class JsonOutputParser(BaseOutputParser):
    async def parse(self, llm_output: Union[str, AssistantMessage]) -> Any
    async def stream_parse(self, streaming_inputs: AsyncIterator) -> AsyncIterator[Optional[dict]]
```

**解析规则**:
- 支持裸 JSON：`{"key": "value"}`
- 支持代码块：`` ```json\n{...}\n``` ``
- 解析失败返回 `None`

---

### `MarkdownOutputParser`

Markdown 输出解析器 - 将 LLM 输出解析为结构化 Markdown 内容。

```python
class MarkdownOutputParser(BaseOutputParser):
    async def parse(self, llm_output: Union[str, AssistantMessage]) -> Optional[MarkdownContent]
    async def stream_parse(self, streaming_inputs: AsyncIterator) -> AsyncIterator[Optional[MarkdownContent]]
```

---

### `MarkdownContent`

结构化 Markdown 内容。

```python
@dataclass
class MarkdownContent:
    raw_content: str = ""                              # 原始文本
    elements: List[MarkdownElement] = []               # 所有元素列表
    headers: List[Dict[str, str]] = []                 # 标题列表
    code_blocks: List[Dict[str, str]] = []             # 代码块列表
    links: List[Dict[str, str]] = []                   # 链接列表
    images: List[Dict[str, str]] = []                  # 图片列表
    tables: List[str] = []                             # 表格列表
    lists: List[str] = []                              # 列表
```

### `MarkdownElement`

```python
@dataclass
class MarkdownElement:
    type: str                    # 元素类型 (见 MarkdownElementType)
    content: Dict[str, Any]      # 元素内容
    start_pos: int               # 起始位置
    end_pos: int                 # 结束位置
    raw: str                     # 原始文本
```

### `MarkdownElementType`

```python
class MarkdownElementType:
    HEADER = "header"
    CODE_BLOCK = "code_block"
    INLINE_CODE = "inline_code"
    LINK = "link"
    IMAGE = "image"
    TABLE = "table"
    LIST = "list"
    TEXT = "text"
```

---

## 路由策略

### `RoutingStrategy` (抽象基类)

所有路由策略的抽象基类。

```python
class RoutingStrategy(ABC):
    @abstractmethod
    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]
        """选择最佳部署"""

    @abstractmethod
    def on_success(
        self,
        deployment: "Deployment",
        latency: float,
        tokens: int
    ) -> None:
        """成功回调 - 更新策略状态"""

    @abstractmethod
    def on_failure(
        self,
        deployment: "Deployment",
        error: Exception
    ) -> None:
        """失败回调 - 更新策略状态"""
```

---

### `SimpleShuffleStrategy`

随机加权选择策略 - 基于权重的随机选择。

**构造函数**:
```python
SimpleShuffleStrategy(
    weights: Optional[Dict[str, float]] = None,  # 部署权重映射
    default_weight: float = 1.0                   # 默认权重
)
```

---

### `LowestLatencyStrategy`

最低延迟策略 - 选择平均延迟最低的部署（带探索比例）。

**构造函数**:
```python
LowestLatencyStrategy(
    state: LocalRouterState,       # 状态管理实例
    exploration_ratio: float = 0.1 # 探索比例（随机选择概率）
)
```

---

### `TagBasedStrategy`

标签过滤策略 - 根据请求标签匹配部署。

**构造函数**:
```python
TagBasedStrategy(
    fallback_strategy: Optional[RoutingStrategy] = None  # 回退策略
)
```

**行为**:
- 从`RoutingContext.request_tags`提取请求标签
- 过滤出包含任意请求标签的部署
- 无匹配时使用回退策略

---

### `TokenAwareStrategy`

Token感知策略 - Token配额耗尽时自动切换部署。

**构造函数**:
```python
TokenAwareStrategy(
    state: LocalRouterState,        # 状态管理实例
    token_threshold: int = 1000,    # Token剩余阈值
    exploration_ratio: float = 0.1  # 探索比例
)
```

---

### `RateLimitAwareStrategy`

RPM限流策略 - RPM超限时负载均衡到其他部署。

**构造函数**:
```python
RateLimitAwareStrategy(
    state: LocalRouterState,        # 状态管理实例
    rpm_threshold: int = 10,        # RPM使用率阈值
    exploration_ratio: float = 0.1  # 探索比例
)
```

---

### `AdaptiveStrategy`

自适应策略 - 多维度加权评分的智能路由。

**构造函数**:
```python
AdaptiveStrategy(
    state: LocalRouterState,              # 状态管理实例
    token_threshold: int = 1000,          # Token阈值
    rpm_threshold: int = 10,              # RPM阈值
    exploration_ratio: float = 0.1,       # 探索比例
    session_cleanup_interval: float = 60.0,  # 会话清理间隔（秒）
    w_health: float = 1.0,                # 健康状态权重
    w_token: float = 0.5,                 # Token配额权重
    w_rpm: float = 0.3,                   # RPM配额权重
    w_latency: float = 0.2                # 延迟权重
)
```

**评分公式**:
```
score = w_health * health_score
      + w_token * token_score
      + w_rpm * rpm_score
      + w_latency * latency_score
```

**特性**:
- 支持会话亲和性（30分钟TTL）
- 自动清理过期会话

---

## 策略工厂函数

### `create_strategy`

创建策略实例的工厂函数。

```python
def create_strategy(
    strategy_type: Union[StrategyType, str],
    state: Optional[LocalRouterState] = None,
    **kwargs
) -> RoutingStrategy
```

**参数**:

| 参数 | 类型 | 说明 |
|------|------|------|
| `strategy_type` | `StrategyType` | 策略类型 |
| `state` | `LocalRouterState` | 状态管理实例（部分策略必需） |
| `**kwargs` | Any | 策略特定参数 |

**策略类型**:

| 值 | 说明 |
|----|------|
| `"simple-shuffle"` | 随机加权选择 |
| `"lowest-latency"` | 最低延迟 |
| `"tag-based"` | 标签过滤 |
| `"token-aware"` | Token感知 |
| `"rate-limit-aware"` | RPM限流 |
| `"adaptive"` | 自适应 |

**异常**:
- `ValueError`: 未知策略类型或缺少必需参数

---

## 状态管理

### `LocalRouterState`

本地路由状态 - 纯内存状态管理。

**属性**:

| 属性 | 类型 | 说明 |
|------|------|------|
| `deployment_status` | `Dict[str, DeploymentStatus]` | 部署状态映射 |
| `consecutive_failures` | `Dict[str, int]` | 连续失败次数 |
| `cooldown_until` | `Dict[str, float]` | 冷却时间戳 |
| `latencies` | `Dict[str, List[LatencyRecord]]` | 延迟历史记录 |
| `total_tokens` | `Dict[str, int]` | 累计Token数 |
| `total_requests` | `Dict[str, int]` | 累计请求数 |
| `health_state` | `Dict[str, bool]` | 健康状态 |
| `token_usage` | `Dict[str, TokenUsage]` | Token使用记录 |
| `rpm_tracker` | `Dict[str, RPMTracker]` | RPM追踪器 |
| `session_deployment_map` | `Dict[str, str]` | 会话到部署的映射 |
| `session_timestamps` | `Dict[str, float]` | 会话时间戳 |

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `on_success` | `on_success(deployment_id: str, latency: float, tokens: int) -> None` | 成功回调，重置失败计数并恢复为 HEALTHY |
| `on_failure` | `on_failure(deployment_id: str, error: Exception, cooldown_time: float = 60.0) -> None` | 失败回调，进入 COOLDOWN（时长 = cooldown_time × 连续失败次数），超时后自愈 |
| `get_average_latency` | `get_average_latency(deployment_id: str) -> float` | 获取平均延迟 |
| `get_available_deployments` | `get_available_deployments(now: float) -> List[str]` | 获取可用部署ID列表 |
| `get_token_remaining` | `get_token_remaining(deployment_id: str) -> int` | 获取剩余Token |
| `get_rpm_remaining` | `get_rpm_remaining(deployment_id: str) -> int` | 获取剩余RPM |
| `get_token_utilization` | `get_token_utilization(deployment_id: str) -> float` | 获取Token使用率 |
| `get_rpm_utilization` | `get_rpm_utilization(deployment_id: str) -> float` | 获取RPM使用率 |

---

### `LatencyRecord`

延迟记录。

```python
@dataclass
class LatencyRecord:
    latency: float     # 延迟（秒）
    tokens: int        # Token数
    normalized: float  # 归一化延迟（latency/tokens）
    timestamp: float   # 时间戳
```

---

### `TokenUsage`

Token使用记录。

```python
@dataclass
class TokenUsage:
    used: int = 0                    # 已使用Token
    limit: int = 0                   # Token限制
    timestamp: float = field(default_factory=time.time)  # 时间戳
```

**属性**:

| 属性 | 类型 | 说明 |
|------|------|------|
| `remaining` | int | 剩余Token (只读) |
| `utilization_ratio` | float | 使用率 (只读) |

---

### `RPMTracker`

RPM追踪器 - 追踪每分钟请求数。

```python
@dataclass
class RPMTracker:
    rpm_limit: int                        # RPM限制
    requests: List[RPMRecord]             # 请求记录列表
```

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `add_request` | `add_request() -> None` | 添加当前请求记录 |

**属性**:

| 属性 | 类型 | 说明 |
|------|------|------|
| `current_rpm` | int | 当前RPM（最近1分钟） |
| `remaining` | int | 剩余RPM配额 (只读) |
| `utilization_ratio` | float | 使用率 (只读) |

---

### `RPMRecord`

RPM请求记录。

```python
@dataclass
class RPMRecord:
    timestamp: float = field(default_factory=time.time)  # 请求时间戳
```

---

## 缓存

### `LocalCache`

本地缓存 - 线程安全的LRU缓存。

**构造函数**:
```python
LocalCache(
    max_size: int = 1000,     # 最大缓存条目数
    default_ttl: float = 3600 # 默认TTL（秒）
)
```

**同步方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `set_cache` | `set_cache(key: str, value: Any, ttl: Optional[float] = None) -> None` | 设置缓存 |
| `get_cache` | `get_cache(key: str, default: Any = None) -> Any` | 获取缓存 |
| `delete_cache` | `delete_cache(key: str) -> bool` | 删除缓存 |
| `clear_cache` | `clear_cache() -> None` | 清空缓存 |
| `get_cache_with_ttl` | `get_cache_with_ttl(key: str) -> Tuple[Optional[Any], Optional[float]]` | 获取缓存及剩余TTL |
| `keys` | `keys() -> list` | 获取所有缓存键 |
| `size` | `size() -> int` | 获取缓存大小 |
| `cleanup_expired` | `cleanup_expired() -> int` | 清理过期条目，返回清理数量 |

**异步方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `async_set_cache` | `async async_set_cache(key: str, value: Any, ttl: Optional[float] = None) -> None` | 异步设置缓存 |
| `async_get_cache` | `async async_get_cache(key: str, default: Any = None) -> Any` | 异步获取缓存 |
| `async_delete_cache` | `async async_delete_cache(key: str) -> bool` | 异步删除缓存 |

---

### `CacheEntry`

缓存条目。

```python
@dataclass
class CacheEntry:
    value: Any              # 缓存值
    ttl: float             # 过期时间戳
    created_at: float      # 创建时间戳

    def is_expired(now: float) -> bool  # 检查是否过期
```

---

## 健康检查

### `SDKHealthChecker`

部署健康检查器 - 支持后台定时检查。现已兼容所有 Provider（通过 Provider 适配器构建请求）。

**构造函数**:
```python
SDKHealthChecker(
    deployments: List[Deployment],                # 部署列表
    state: LocalRouterState,                      # 状态管理
    check_interval: float = 300,                  # 检查间隔（秒）
    check_timeout: float = 5,                     # 检查超时（秒）
    check_message: List[Dict] = [{"role": "user", "content": "ping"}],  # 检查消息
    check_max_tokens: int = 10                    # 最大token数
)
```

**异步方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `check_deployment` | `async check_deployment(deployment: Deployment) -> HealthCheckResult` | 检查单个部署 |
| `check_all_deployments` | `async check_all_deployments() -> Dict[str, HealthCheckResult]` | 检查所有部署 |
| `start_background_check` | `async start_background_check() -> None` | 启动后台检查任务 |
| `stop_background_check` | `async stop_background_check() -> None` | 停止后台检查任务 |
| `close` | `async close() -> None` | 关闭共享 HTTP 客户端 |

**同步方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `get_healthy_deployments` | `get_healthy_deployments(now: float) -> List[Deployment]` | 获取健康部署列表 |
| `get_unhealthy_ids` | `get_unhealthy_ids() -> Set[str]` | 获取不健康部署ID集合 |

---

### `HealthCheckResult`

健康检查结果。

```python
@dataclass
class HealthCheckResult:
    deployment_id: str                    # 部署ID
    is_healthy: bool                      # 是否健康
    latency: Optional[float] = None       # 检查延迟
    error: Optional[str] = None           # 错误信息
    timestamp: float = field(default_factory=time.time)  # 检查时间戳
```

---

## 异常

### 异常层次

```
IntelliRouterError (基类)
├── RouterError
│   ├── NoDeploymentAvailable
│   ├── AllDeploymentsFailed
│   └── StrategyError
├── DeploymentError
│   ├── DeploymentInCooldown
│   ├── DeploymentFailed
│   ├── DeploymentTimeoutError
│   ├── DeploymentAuthError
│   ├── DeploymentRateLimitError
│   ├── DeploymentServerError
│   └── DeploymentNetworkError
├── CacheError
│   ├── CacheKeyNotFound
│   └── CacheExpired
├── HealthCheckError
│   ├── HealthCheckFailed
│   └── HealthCheckTimeout
└── ConfigError
    ├── InvalidDeploymentConfig
    └── MissingRequiredField
```

---

### `IntelliRouterError`

基类异常。

```python
class IntelliRouterError(Exception):
    def __init__(
        self,
        message: str,
        details: Optional[Dict[str, Any]] = None
    )
    def to_dict() -> Dict[str, Any]  # 转换为字典
```

---

### `RouterError`

路由器相关异常基类。

```python
class RouterError(IntelliRouterError): ...
```

---

### `NoDeploymentAvailable`

无可用部署异常。

```python
class NoDeploymentAvailable(RouterError):
    def __init__(self, model: str, reason: str = "")
```

---

### `AllDeploymentsFailed`

所有部署均失败异常。

```python
class AllDeploymentsFailed(RouterError):
    def __init__(self, model: str, errors: List[Exception])
```

---

### `StrategyError`

策略执行错误。

```python
class StrategyError(RouterError):
    def __init__(self, strategy_name: str, reason: str)
```

---

### `DeploymentError`

部署相关异常基类。

```python
class DeploymentError(IntelliRouterError): ...
```

---

### `DeploymentInCooldown`

部署冷却中异常。

```python
class DeploymentInCooldown(DeploymentError):
    def __init__(self, deployment_id: str, cooldown_until: float)
```

---

### `DeploymentFailed`

部署失败异常。

```python
class DeploymentFailed(DeploymentError):
    def __init__(self, deployment_id: str, consecutive_failures: int)
```

---

### `DeploymentTimeoutError`

部署请求超时异常。

```python
class DeploymentTimeoutError(DeploymentError):
    def __init__(self, deployment_id: str, timeout: float)
```

---

### `DeploymentAuthError`

认证失败异常 (HTTP 401/403)。

```python
class DeploymentAuthError(DeploymentError):
    def __init__(self, deployment_id: str, status_code: int)
```

---

### `DeploymentRateLimitError`

限流异常 (HTTP 429)。

```python
class DeploymentRateLimitError(DeploymentError):
    def __init__(self, deployment_id: str, retry_after: Optional[float] = None)
```

---

### `DeploymentServerError`

服务器错误异常 (HTTP 5xx)。

```python
class DeploymentServerError(DeploymentError):
    def __init__(self, deployment_id: str, status_code: int, response_body: str = "")
```

---

### `DeploymentNetworkError`

网络连接错误异常。

```python
class DeploymentNetworkError(DeploymentError):
    def __init__(self, deployment_id: str, reason: str)
```

---

### `CacheError`

缓存相关异常基类。

```python
class CacheError(IntelliRouterError): ...
```

---

### `CacheKeyNotFound`

缓存键不存在异常。

```python
class CacheKeyNotFound(CacheError):
    def __init__(self, key: str)
```

---

### `CacheExpired`

缓存已过期异常。

```python
class CacheExpired(CacheError):
    def __init__(self, key: str, expired_at: float)
```

---

### `HealthCheckError`

健康检查相关异常基类。

```python
class HealthCheckError(IntelliRouterError): ...
```

---

### `HealthCheckFailed`

健康检查失败异常。

```python
class HealthCheckFailed(HealthCheckError):
    def __init__(self, deployment_id: str, error: str)
```

---

### `HealthCheckTimeout`

健康检查超时异常。

```python
class HealthCheckTimeout(HealthCheckError):
    def __init__(self, deployment_id: str, timeout: float)
```

---

### `ConfigError`

配置相关异常基类。

```python
class ConfigError(IntelliRouterError): ...
```

---

### `InvalidDeploymentConfig`

无效的部署配置异常。

```python
class InvalidDeploymentConfig(ConfigError):
    def __init__(self, field: str, value: Any, reason: str)
```

---

### `MissingRequiredField`

缺少必需字段异常。

```python
class MissingRequiredField(ConfigError):
    def __init__(self, field: str, context: str)
```

---

### `get_status_code`

获取异常的HTTP状态码映射。

```python
def get_status_code(error: Exception) -> int
```

**返回码映射**:

| 异常类型 | HTTP状态码 |
|----------|------------|
| `NoDeploymentAvailable` | 503 |
| `AllDeploymentsFailed` | 503 |
| `DeploymentInCooldown` | 503 |
| `DeploymentFailed` | 503 |
| `DeploymentTimeoutError` | 504 |
| `DeploymentAuthError` | 401 |
| `DeploymentRateLimitError` | 429 |
| `DeploymentServerError` | 502 |
| `DeploymentNetworkError` | 502 |
| `CacheKeyNotFound` | 404 |
| `CacheExpired` | 410 |
| `HealthCheckFailed` | 503 |
| `HealthCheckTimeout` | 504 |
| `InvalidDeploymentConfig` | 400 |
| `MissingRequiredField` | 400 |
| 其他 | 500 |

---

## 使用示例

### 多 Provider 路由

```python
from intelli_router import ReliableRouter, Deployment

deployments = [
    Deployment(
        model_name="gpt-4o-mini",
        api_key="sk-...",
        api_base="https://api.openai.com",
        provider="openai",
    ),
    Deployment(
        model_name="claude-3-haiku-20240307",
        api_key="sk-ant-...",
        api_base="https://api.anthropic.com",
        provider="anthropic",
    ),
    Deployment(
        model_name="gemini-1.5-flash",
        api_key="AIza...",
        api_base="https://generativelanguage.googleapis.com",
        provider="google-gemini",
    ),
]

async with ReliableRouter(deployments=deployments, strategy="adaptive") as router:
    response = await router.completion(
        model="gpt-4o-mini",
        messages=[{"role": "user", "content": "Hello!"}],
    )
```

### 类型化调用 (invoke)

```python
from intelli_router import ReliableRouter, Deployment

router = ReliableRouter(deployments=deployments, strategy="adaptive")

msg = await router.invoke(
    messages=[{"role": "user", "content": "Hello!"}],
    model="gpt-4o-mini",
    temperature=0.7,
    max_tokens=1000,
)

print(msg.content)                        # 文本内容
print(msg.finish_reason)                  # "stop"
print(msg.usage_metadata.total_tokens)    # token 总量
print(msg.tool_calls)                     # 工具调用（如有）

await router.close()
```

### 流式调用 (stream)

```python
accumulated = None
async for chunk in router.stream(
    messages=[{"role": "user", "content": "Write a story"}],
    model="gpt-4o-mini",
):
    print(chunk.content, end="", flush=True)
    accumulated = chunk if accumulated is None else accumulated + chunk

print(f"\nFinish reason: {accumulated.finish_reason}")
print(f"Total tokens: {accumulated.usage_metadata.total_tokens}")
```

### 输出解析器

```python
from intelli_router import ReliableRouter, JsonOutputParser

parser = JsonOutputParser()
msg = await router.invoke(
    messages=[{"role": "user", "content": "Return a JSON: {\"name\": \"...\", \"age\": ...}"}],
    model="gpt-4o-mini",
    output_parser=parser,
)
# msg.content 已被解析为 JSON 字符串
```

### 自定义 Provider Adapter

```python
from intelli_router.provider.base_provider import BaseProviderAdapter
from intelli_router.provider.registry import register_provider

class MyProviderAdapter(BaseProviderAdapter):
    def get_api_url(self, deployment, stream=False):
        return f"{deployment.api_base}/my/chat/endpoint"

    def get_headers(self, deployment):
        return {
            "Authorization": f"Token {deployment.api_key}",
            "Content-Type": "application/json",
        }

# 注册后即可在 Deployment(provider="my-provider") 中使用
register_provider("my-provider", MyProviderAdapter)
```

### 使用自定义策略

```python
from intelli_router import ReliableRouter, Deployment
from intelli_router.core.state import LocalRouterState
from intelli_router.strategy import create_strategy

state = LocalRouterState()

strategy = create_strategy(
    strategy_type="adaptive",
    state=state,
    token_threshold=1000,
    rpm_threshold=10,
    w_health=1.0,
    w_token=0.5,
    w_rpm=0.3,
    w_latency=0.2,
)

router = ReliableRouter(deployments=deployments, strategy=strategy)
```

### 批量请求

```python
requests = [
    {"model": "gpt-4o-mini", "messages": [{"role": "user", "content": "Test 1"}]},
    {"model": "gpt-4o-mini", "messages": [{"role": "user", "content": "Test 2"}]},
]

results = await router.batch_completion(requests, max_concurrent=5)
```

### 使用缓存

```python
from intelli_router.cache import LocalCache

cache = LocalCache(max_size=1000, default_ttl=3600)
cache.set_cache("my_key", {"result": "value"}, ttl=1800)

result = cache.get_cache("my_key")
remaining_ttl, expires_at = cache.get_cache_with_ttl("my_key")
```

### 健康检查

```python
from intelli_router.health import SDKHealthChecker
from intelli_router.core.state import LocalRouterState

state = LocalRouterState()
health_checker = SDKHealthChecker(
    deployments=deployments,
    state=state,
    check_interval=60,
    check_timeout=5,
)

await health_checker.start_background_check()

result = await health_checker.check_deployment(deployments[0])
print(f"Healthy: {result.is_healthy}, Latency: {result.latency}")

await health_checker.close()
```

### 异常处理

```python
from intelli_router import ReliableRouter, Deployment
from intelli_router.utils.exceptions import (
    IntelliRouterError,
    NoDeploymentAvailable,
    AllDeploymentsFailed,
    DeploymentAuthError,
    DeploymentRateLimitError,
    get_status_code,
)

try:
    response = await router.completion(
        model="gpt-4o-mini",
        messages=[{"role": "user", "content": "Hello!"}],
    )
except DeploymentAuthError as e:
    print(f"Auth failed: {e}")
except DeploymentRateLimitError as e:
    print(f"Rate limited: {e}")
except NoDeploymentAvailable as e:
    print(f"No deployment available for {e.model}")
except AllDeploymentsFailed as e:
    print(f"All deployments failed: {e.errors}")
except IntelliRouterError as e:
    print(f"SDK Error: {e.to_dict()}")
    status_code = get_status_code(e)
```
