# SDK LLM API接口文档

## 目录

- [核心类](#核心类)
- [路由策略](#路由策略)
- [策略工厂函数](#策略工厂函数)
- [状态管理](#状态管理)
- [缓存](#缓存)
- [健康检查](#健康检查)
- [异常](#异常)
- [类型定义](#类型定义)
- [使用示例](#使用示例)

---

## 核心类

### `Deployment`

部署配置类 - 表示一个API端点。

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
    litellm_params: Dict[str, Any] = {}       # litellm额外参数
)
```

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `is_available` | `is_available(now: float) -> bool` | 检查部署是否可用（非冷却中且非失败状态） |
| `to_dict` | `to_dict() -> Dict[str, Any]` | 序列化为字典 |
| `from_dict` | `(data: Dict[str, Any]) -> Deployment` | 从字典反序列化 (类方法) |

---

### `DeploymentStatus` (枚举)

部署状态枚举。

```python
class DeploymentStatus(Enum):
    HEALTHY = "healthy"    # 健康
    COOLDOWN = "cooldown"  # 冷却中
    FAILED = "failed"      # 失败
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
    allowed_fails: int = 3,                     # 允许失败次数（触发冷却）
    cooldown_time: float = 60.0,                # 冷却时间（秒）
    enable_health_check: bool = False,          # 启用健康检查
    health_check_interval: float = 300,         # 健康检查间隔（秒）
    cache: Optional[LocalCache] = None,         # 缓存实例
    **strategy_kwargs                           # 策略参数
)
```

**方法**:

| 方法 | 签名 | 说明 |
|------|------|------|
| `completion` | `async completion(model: str, messages: List[Dict], **kwargs) -> Any` | 发送completion请求 |
| `batch_completion` | `async batch_completion(requests: List[Dict], max_concurrent: int = 10) -> List[Any]` | 批量completion请求（并发控制） |
| `get_deployments_for_model` | `get_deployments_for_model(model: str) -> List[Deployment]` | 获取模型的部署列表 |
| `get_model_list` | `get_model_list() -> List[str]` | 获取已注册模型名列表 |
| `update_deployments` | `update_deployments(new_deployments: List[Deployment]) -> None` | 动态更新部署列表 |
| `get_stats` | `get_stats() -> Dict[str, Any]` | 获取路由器统计信息 |

**异步上下文管理器**:
```python
async with ReliableRouter(...) as router:
    response = await router.completion(...)
```

---

### `BaseRouter`

基础路由器 - 提供API池化和请求转发能力。

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
| `completion` | `async completion(model: str, messages: List[Dict], deployment: Optional[Deployment] = None, **kwargs) -> Any` | 发送completion请求（可指定部署） |
| `completion_with_fallback` | `async completion_with_fallback(model: str, messages: List[Dict], fallback: Optional[Dict[str, str]] = None, **kwargs) -> Any` | 带降级模型的completion |
| `get_deployments_for_model` | `get_deployments_for_model(model: str) -> List[Deployment]` | 获取模型部署列表 |
| `get_model_list` | `get_model_list() -> List[str]` | 获取模型名列表 |
| `get_deployment_configs` | `get_deployment_configs() -> List[Dict[str, Any]]` | 获取所有部署配置 |
| `get_deployment_config_by_model` | `get_deployment_config_by_model(model: str) -> List[Dict[str, Any]]` | 获取指定模型的部署配置 |

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

**评分因素**: 剩余Token配额

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

**评分因素**: 剩余RPM配额

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
| `on_success` | `on_success(deployment_id: str, latency: float, tokens: int, allowed_fails: int = 3, cooldown_time: float = 60.0) -> None` | 成功回调 |
| `on_failure` | `on_failure(deployment_id: str, error: Exception, allowed_fails: int = 3, cooldown_time: float = 60.0) -> None` | 失败回调 |
| `get_average_latency` | `get_average_latency(deployment_id: str) -> float` | 获取平均延迟 |
| `get_available_deployments` | `get_available_deployments(now: float) -> List[str]` | 获取可用部署ID列表 |
| `get_token_remaining` | `get_token_remaining(deployment_id: str) -> int` | 获取剩余Token |
| `get_rpm_remaining` | `get_rpm_remaining(deployment_id: str) -> int` | 获取剩余RPM |
| `get_token_utilization` | `get_token_utilization(deployment_id: str) -> float` | 获取Token使用率 |
| `get_rpm_utilization` | `get_rpm_utilization(deployment_id: str) -> float` | 获取RPM使用率 |

---

### `LatencyRecord`

延迟记录 - 记录单次请求的延迟信息。

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

RPM请求记录 - 单次请求的时间戳。

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

缓存条目 - 内部使用的缓存数据结构。

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

部署健康检查器 - 支持后台定时检查。

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
SdkLlmError (基类)
├── RouterError
│   ├── NoDeploymentAvailable
│   ├── AllDeploymentsFailed
│   └── StrategyError
├── DeploymentError
│   ├── DeploymentInCooldown
│   └── DeploymentFailed
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

### `SdkLlmError`

基类异常。

```python
class SdkLlmError(Exception):
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
class RouterError(SdkLlmError): ...
```

---

### `NoDeploymentAvailable`

无可用部署异常。

```python
class NoDeploymentAvailable(RouterError):
    def __init__(
        self,
        model: str,
        reason: str = ""
    )
```

---

### `AllDeploymentsFailed`

所有部署均失败异常。

```python
class AllDeploymentsFailed(RouterError):
    def __init__(
        self,
        model: str,
        errors: List[Exception]
    )
```

---

### `StrategyError`

策略执行错误。

```python
class StrategyError(RouterError):
    def __init__(
        self,
        strategy_name: str,
        reason: str
    )
```

---

### `DeploymentError`

部署相关异常基类。

```python
class DeploymentError(SdkLlmError): ...
```

---

### `DeploymentInCooldown`

部署冷却中异常。

```python
class DeploymentInCooldown(DeploymentError):
    def __init__(
        self,
        deployment_id: str,
        cooldown_until: float
    )
```

---

### `DeploymentFailed`

部署失败异常。

```python
class DeploymentFailed(DeploymentError):
    def __init__(
        self,
        deployment_id: str,
        consecutive_failures: int
    )
```

---

### `CacheError`

缓存相关异常基类。

```python
class CacheError(SdkLlmError): ...
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
    def __init__(
        self,
        key: str,
        expired_at: float
    )
```

---

### `HealthCheckError`

健康检查相关异常基类。

```python
class HealthCheckError(SdkLlmError): ...
```

---

### `HealthCheckFailed`

健康检查失败异常。

```python
class HealthCheckFailed(HealthCheckError):
    def __init__(
        self,
        deployment_id: str,
        error: str
    )
```

---

### `HealthCheckTimeout`

健康检查超时异常。

```python
class HealthCheckTimeout(HealthCheckError):
    def __init__(
        self,
        deployment_id: str,
        timeout: float
    )
```

---

### `ConfigError`

配置相关异常基类。

```python
class ConfigError(SdkLlmError): ...
```

---

### `InvalidDeploymentConfig`

无效的部署配置异常。

```python
class InvalidDeploymentConfig(ConfigError):
    def __init__(
        self,
        field: str,
        value: Any,
        reason: str
    )
```

---

### `MissingRequiredField`

缺少必需字段异常。

```python
class MissingRequiredField(ConfigError):
    def __init__(
        self,
        field: str,
        context: str
    )
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
| `CacheKeyNotFound` | 404 |
| `CacheExpired` | 410 |
| 其他 | 500 |

---

## 类型定义

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

## 使用示例

### 基础使用

```python
from sdk_llm import ReliableRouter, Deployment

deployments = [
    Deployment(
        model_name="gpt-4",
        api_key="sk-key1",
        api_base="https://api.openai.com/v1",
        tpm=100000,
        rpm=1000
    ),
    Deployment(
        model_name="gpt-4",
        api_key="sk-key2",
        api_base="https://api.backup.com/v1",
        tpm=50000,
        rpm=500
    )
]

router = ReliableRouter(
    deployments=deployments,
    strategy="adaptive"
)

response = await router.completion(
    model="gpt-4",
    messages=[{"role": "user", "content": "Hello!"}]
)
```

### 使用自定义策略

```python
from sdk_llm import ReliableRouter, Deployment
from sdk_llm.core.state import LocalRouterState
from sdk_llm.strategy import create_strategy

state = LocalRouterState()

strategy = create_strategy(
    strategy_type="adaptive",
    state=state,
    token_threshold=1000,
    rpm_threshold=10,
    w_health=1.0,
    w_token=0.5,
    w_rpm=0.3,
    w_latency=0.2
)

router = ReliableRouter(
    deployments=deployments,
    strategy=strategy
)
```

### 批量请求

```python
requests = [
    {"model": "gpt-4", "messages": [{"role": "user", "content": "Test 1"}]},
    {"model": "gpt-4", "messages": [{"role": "user", "content": "Test 2"}]},
]

results = await router.batch_completion(requests, max_concurrent=5)
```

### 使用缓存

```python
from sdk_llm.cache import LocalCache

cache = LocalCache(max_size=1000, default_ttl=3600)
cache.set_cache("my_key", {"result": "value"}, ttl=1800)

result = cache.get_cache("my_key")
remaining_ttl, expires_at = cache.get_cache_with_ttl("my_key")
```

### 健康检查

```python
from sdk_llm.health import SDKHealthChecker
from sdk_llm.core.state import LocalRouterState

state = LocalRouterState()
health_checker = SDKHealthChecker(
    deployments=deployments,
    state=state,
    check_interval=60,
    check_timeout=5
)

await health_checker.start_background_check()

result = await health_checker.check_deployment(deployments[0])
print(f"Healthy: {result.is_healthy}, Latency: {result.latency}")
```

### 异常处理

```python
from sdk_llm import ReliableRouter, Deployment
from sdk_llm.exceptions import (
    SdkLlmError,
    NoDeploymentAvailable,
    AllDeploymentsFailed,
    get_status_code
)

try:
    response = await router.completion(
        model="gpt-4",
        messages=[{"role": "user", "content": "Hello!"}]
    )
except NoDeploymentAvailable as e:
    print(f"No deployment available for {e.model}")
except AllDeploymentsFailed as e:
    print(f"All deployments failed: {e.errors}")
except SdkLlmError as e:
    print(f"SDK Error: {e.to_dict()}")
    status_code = get_status_code(e)
```