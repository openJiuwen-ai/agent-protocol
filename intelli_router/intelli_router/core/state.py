"""
SDK LLM Core - 状态管理

LocalRouterState: 本地路由状态 (纯内存)
LatencyRecord: 延迟记录
TokenUsage: Token使用记录
RPMTracker: RPM追踪记录
"""
from typing import Dict, List, Optional
from dataclasses import dataclass, field
import threading
import time
from .deployment import DeploymentStatus

# 最大历史记录数
MAX_LATENCY_HISTORY = 10
MAX_TOKENS_HISTORY = 10
MAX_RPM_HISTORY = 60  # 1分钟的RPM历史


@dataclass
class LatencyRecord:
    """延迟记录"""
    latency: float
    tokens: int
    normalized: float
    timestamp: float = field(default_factory=time.time)


@dataclass
class TokenUsage:
    """Token使用记录"""
    used: int = 0
    limit: int = 0
    timestamp: float = field(default_factory=time.time)

    @property
    def remaining(self) -> int:
        """剩余Token"""
        return max(0, self.limit - self.used)

    @property
    def utilization_ratio(self) -> float:
        """使用率"""
        if self.limit == 0:
            return 0.0
        return self.used / self.limit


@dataclass
class RPMRecord:
    """RPM记录"""
    timestamp: float = field(default_factory=time.time)


@dataclass
class RPMTracker:
    """RPM追踪器"""
    rpm_limit: int = 0
    requests: List[RPMRecord] = field(default_factory=list)

    def add_request(self) -> None:
        """添加请求记录"""
        self.requests.append(RPMRecord())
        # 清理过期记录 (保留最近1分钟)
        now = time.time()
        self.requests = [r for r in self.requests if now - r.timestamp < 60]

    @property
    def current_rpm(self) -> int:
        """当前RPM (最近1分钟的请求数)"""
        now = time.time()
        recent = [r for r in self.requests if now - r.timestamp < 60]
        return len(recent)

    @property
    def remaining(self) -> int:
        """剩余RPM配额"""
        return max(0, self.rpm_limit - self.current_rpm)

    @property
    def utilization_ratio(self) -> float:
        """RPM使用率"""
        if self.rpm_limit == 0:
            return 0.0
        return self.current_rpm / self.rpm_limit

@dataclass
class LocalRouterState:
    """
    本地路由状态 - 纯内存

    Attributes:
    deployment_status: Dict[str, DeploymentStatus] 部署状态
    consecutive_failures: Dict[str, int] 连续失败次数
    cooldown_until: Dict[str, float] 冷却时间戳
    latencies: Dict[str, List[LatencyRecord]] 延迟历史
    total_tokens: Dict[str, int] 总token数
    total_requests: Dict[str, int] 总请求数
    health_state: Dict[str, bool] 健康状态
    token_usage: Dict[str, TokenUsage] Token使用记录
    rpm_tracker: Dict[str, RPMTracker] RPM追踪器
    """
    # 部署状态
    deployment_status: Dict[str, DeploymentStatus] = field(default_factory=dict)
    consecutive_failures: Dict[str, int] = field(default_factory=dict)
    cooldown_until: Dict[str, float] = field(default_factory=dict)

    # 延迟追踪
    latencies: Dict[str, List[LatencyRecord]] = field(default_factory=dict)

    total_tokens: Dict[str, int] = field(default_factory=dict)
    total_requests: Dict[str, int] = field(default_factory=dict)
    health_state: Dict[str, bool] = field(default_factory=dict)

    # Token和RPM追踪
    token_usage: Dict[str, TokenUsage] = field(default_factory=dict)
    rpm_tracker: Dict[str, RPMTracker] = field(default_factory=dict)

    # Session亲和性映射
    session_deployment_map: Dict[str, str] = field(default_factory=dict)  # session_id -> deployment_id
    session_timestamps: Dict[str, float] = field(default_factory=dict)   # session_id -> 最近使用时间

    def __post_init__(self):
        self._lock = threading.RLock()

    @property
    def lock(self) -> threading.RLock:
        return self._lock

    def reset_deployment(self, deployment_id: str) -> None:
        """原子性重置部署状态"""
        with self._lock:
            self.deployment_status[deployment_id] = DeploymentStatus.HEALTHY
            self.cooldown_until[deployment_id] = None

    def update_health(self, deployment_id: str, is_healthy: bool) -> None:
        """原子性更新健康状态

        健康检查通过时加速恢复为 HEALTHY。
        不健康时不降级——让真实的请求失败 on_failure 去触发 COOLDOWN。
        """
        with self._lock:
            self.health_state[deployment_id] = is_healthy
            if is_healthy:
                self.deployment_status[deployment_id] = DeploymentStatus.HEALTHY
                self.cooldown_until[deployment_id] = None

    def on_success(
        self,
        deployment_id: str,
        latency: float,
        tokens: int,
    ) -> None:
        """成功回调 - 更新状态"""
        with self._lock:
            # 重置失败计数
            self.consecutive_failures[deployment_id] = 0

            # 恢复健庭状态
            self.deployment_status[deployment_id] = DeploymentStatus.HEALTHY
            self.cooldown_until[deployment_id] = None

            # 记录延迟
            record = LatencyRecord(
                latency=latency,
                tokens=tokens,
                normalized=latency / max(tokens, 1),
                timestamp=time.time()
            )
            if deployment_id not in self.latencies:
                self.latencies[deployment_id] = []
            self.latencies[deployment_id].append(record)
            # 留留最近10条
            if len(self.latencies[deployment_id]) > MAX_LATENCY_HISTORY:
                self.latencies[deployment_id].pop(0)

            # 更新使用量
            self.total_tokens[deployment_id] = self.total_tokens.get(deployment_id, 0) + tokens
            self.total_requests[deployment_id] = self.total_requests.get(deployment_id, 0) + 1
            # 更新健庭状态
            self.health_state[deployment_id] = True

            # 更新Token使用
            if deployment_id not in self.token_usage:
                self.token_usage[deployment_id] = TokenUsage()
            self.token_usage[deployment_id].used += tokens

            # 更新RPM
            if deployment_id not in self.rpm_tracker:
                self.rpm_tracker[deployment_id] = RPMTracker()
            self.rpm_tracker[deployment_id].add_request()

    def on_failure(
        self,
        deployment_id: str,
        error: Exception,
        cooldown_time: float = 60.0
    ) -> None:
        """失败回调 - 统一走 COOLDOWN + backoff

        cooldown 时长按连续失败次数递增:
          第1次: 60s, 第2次: 120s, 第3次: 180s ...
        """
        with self._lock:
            self.consecutive_failures[deployment_id] = self.consecutive_failures.get(deployment_id, 0) + 1

            failures = self.consecutive_failures[deployment_id]
            self.deployment_status[deployment_id] = DeploymentStatus.COOLDOWN
            self.cooldown_until[deployment_id] = time.time() + cooldown_time * failures
            self.health_state[deployment_id] = False

    def get_average_latency(self, deployment_id: str) -> float:
        """获取平均归一化延迟"""
        records = self.latencies.get(deployment_id, [])
        if not records:
            return float('inf')
        return sum(r.normalized for r in records) / len(records)

    def get_available_deployments(self, now: float) -> List[str]:
        """获取当前可用的部署ID列表"""
        with self._lock:
            available = []
            for dep_id, status in self.deployment_status.items():
                if status == DeploymentStatus.HEALTHY:
                    available.append(dep_id)
                elif status == DeploymentStatus.COOLDOWN:
                    if self.cooldown_until.get(dep_id, 0) <= now:
                        self.reset_deployment(dep_id)
                        available.append(dep_id)
            return available

    def get_token_remaining(self, deployment_id: str) -> int:
        """获取剩余Token配额"""
        usage = self.token_usage.get(deployment_id)
        if usage:
            return usage.remaining
        return float('inf')

    def get_rpm_remaining(self, deployment_id: str) -> int:
        """获取剩余RPM配额"""
        tracker = self.rpm_tracker.get(deployment_id)
        if tracker:
            return tracker.remaining
        return float('inf')

    def get_token_utilization(self, deployment_id: str) -> float:
        """获取Token使用率"""
        usage = self.token_usage.get(deployment_id)
        if usage:
            return usage.utilization_ratio
        return 0.0

    def get_rpm_utilization(self, deployment_id: str) -> float:
        """获取RPM使用率"""
        tracker = self.rpm_tracker.get(deployment_id)
        if tracker:
            return tracker.utilization_ratio
        return 0.0
