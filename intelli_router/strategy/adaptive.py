"""
SDK LLM Strategy - 自适应策略

AdaptiveStrategy: 多级自适应路由策略
"""
from typing import List, Optional, TYPE_CHECKING
import time
import random
from .base_strategy import RoutingStrategy

if TYPE_CHECKING:
    from ..core.deployment import Deployment
    from ..core.context import RoutingContext
    from ..core.state import LocalRouterState


class AdaptiveStrategy(RoutingStrategy):
    """
    自适应策略 - 多级决策树

    决策层级:
    1. 健康检查 → 排除不健康部署
    2. Token剩余 → 优先Token充足的
    3. RPM剩余 → 优先RPM配额充足的
    4. 延迟收益 → 优先低延迟的
    5. 随机回退 → 随机选择

    权重计算:
    score = w_health * health_score
          + w_token * token_score
          + w_rpm * rpm_score
          + w_latency * latency_score
    """

    # Session亲和性相关常量
    SESSION_TTL = 1800  # 30分钟

    def __init__(
        self,
        state: "LocalRouterState",
        token_threshold: int = 1000,
        rpm_threshold: int = 10,
        exploration_ratio: float = 0.1,
        session_cleanup_interval: float = 60.0,
        # 权重参数
        w_health: float = 1.0,
        w_token: float = 0.5,
        w_rpm: float = 0.3,
        w_latency: float = 0.2
    ):
        self.state = state
        self.token_threshold = token_threshold
        self.rpm_threshold = rpm_threshold
        self.exploration_ratio = exploration_ratio
        self.session_cleanup_interval = session_cleanup_interval
        self._last_cleanup_time = time.time()
        # 权重
        self.w_health = w_health
        self.w_token = w_token
        self.w_rpm = w_rpm
        self.w_latency = w_latency

    def _calculate_score(self, deployment: "Deployment", now: float) -> float:
        """计算部署综合得分"""
        dep_id = deployment.id
        # 健康得分
        health_score = 1.0 if self.state.health_state.get(dep_id, True) else 0.0

        # Token得分 (剩余越多越好)
        token_remaining = self.state.get_token_remaining(dep_id)
        if token_remaining == float('inf'):
            token_score = 1.0
        else:
            token_score = min(1.0, token_remaining / self.token_threshold)

        # RPM得分 (剩余越多越好)
        rpm_remaining = self.state.get_rpm_remaining(dep_id)
        if rpm_remaining == float('inf'):
            rpm_score = 1.0
        else:
            rpm_score = min(1.0, rpm_remaining / self.rpm_threshold)

        # 延迟得分 (延迟越低越好)
        avg_latency = self.state.get_average_latency(dep_id)
        if avg_latency == float('inf'):
            latency_score = 0.0
        else:
            # 归一化延迟得分 (假设1秒为基准)
            latency_score = max(0.0, 1.0 - avg_latency)

        # 综合得分
        score = (
            self.w_health * health_score +
            self.w_token * token_score +
            self.w_rpm * rpm_score +
            self.w_latency * latency_score
        )
        return score

    async def select_deployment(
        self,
        deployments: List["Deployment"],
        context: "RoutingContext"
    ) -> Optional["Deployment"]:
        """选择最优部署"""
        now = time.time()

        # 过滤可用部署
        available = [d for d in deployments if d.is_available(now)]
        if not available:
            return None

        # Session 亲和性检查 (软亲和性)
        session_id = context.kwargs.get("session_id")
        if session_id:
            affinity_deployment = self._get_session_affinity_deployment(
                session_id, available, now
            )
            if affinity_deployment:
                return affinity_deployment

        # 探索: 随机选择
        if random.random() < self.exploration_ratio:
            return random.choice(available)

        # 利用: 计算每个部署得分
        scored = []
        for d in available:
            score = self._calculate_score(d, now)
            scored.append((d, score))

        # 降序排序
        scored.sort(key=lambda x: x[1], reverse=True)

        # 返回得分最高的
        deployment = scored[0][0] if scored else available[0]

        # 更新 session 映射
        if session_id:
            self._update_session_mapping(session_id, deployment.id, now)

        return deployment

    def on_success(self, deployment: "Deployment", latency: float, tokens: int) -> None:
        """成功回调 - 更新所有追踪"""
        self.state.on_success(deployment.id, latency, tokens)

    def on_failure(self, deployment: "Deployment", error: Exception) -> None:
        """失败回调 - 更新失败记录"""
        self.state.on_failure(deployment.id, error)

    def _get_session_affinity_deployment(
        self,
        session_id: str,
        available: List["Deployment"],
        now: float
    ) -> Optional["Deployment"]:
        """
        获取 session 对应的亲和性 deployment

        Args:
            session_id: 会话 ID
            available: 可用 deployment 列表
            now: 当前时间戳

        Returns:
            如果有历史映射且对应的 deployment 在可用列表中，返回该 deployment；否则返回 None
        """
        # 惰性清理过期 session
        if (now - self._last_cleanup_time) >= self.session_cleanup_interval:
            self._cleanup_expired_sessions(now)
            self._last_cleanup_time = now

        # 检查是否有历史映射
        cached_dep_id = self.state.session_deployment_map.get(session_id)
        if not cached_dep_id:
            return None

        # 检查对应的 deployment 是否在可用列表中
        for dep in available:
            if dep.id == cached_dep_id:
                # 更新最近使用时间
                with self.state.lock:
                    self.state.session_timestamps[session_id] = now
                return dep

        return None

    def _update_session_mapping(self, session_id: str, deployment_id: str, now: float) -> None:
        """更新 session 到 deployment 的映射"""
        with self.state.lock:
            self.state.session_deployment_map[session_id] = deployment_id
            self.state.session_timestamps[session_id] = now

    def _cleanup_expired_sessions(self, now: float) -> None:
        """清理过期的 session 映射"""
        with self.state.lock:
            expired = [
                sid for sid, timestamp in self.state.session_timestamps.items()
                if now - timestamp > self.SESSION_TTL
            ]
            for sid in expired:
                self.state.session_deployment_map.pop(sid, None)
                self.state.session_timestamps.pop(sid, None)
