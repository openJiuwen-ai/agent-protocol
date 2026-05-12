"""
SDK LLM Router - 可靠性路由

ReliableRouter: 集成状态管理、策略选择，健庭检查
"""
from typing import Dict, List, Optional, Any, Union, Literal, AsyncIterator
from dataclasses import dataclass, field
import asyncio
import time
import httpx

from .base_router import BaseRouter
from ..core.deployment import Deployment, DeploymentStatus
from ..core.context import RoutingContext
from ..core.state import LocalRouterState
from ..strategy.base_strategy import RoutingStrategy
from ..strategy import create_strategy, StrategyType
from ..health.checker import SDKHealthChecker
from ..cache.local_cache import LocalCache
from ..utils.exceptions import RouterError, NoDeploymentAvailable

class ReliableRouter(BaseRouter):
    """
    可靠性路由器 - 集成状态管理，策略选择，健康检查

    特性:
    - API池化: 一个模型名 → 多个部署
    - 策略插拔: 随机/延迟/标签策略
    - 状态管理: 失败计数、冷却机制
    - 健康检查: 后台健康检查
    - 重试机制: 自动重试
    - 流式支持: 支持流式响应
    """

    def __init__(
        self,
        deployments: List[Deployment],
        strategy: Union[StrategyType, RoutingStrategy] = "simple-shuffle",
        num_retries: int = 3,
        timeout: float = 30.0,
        allowed_fails: int = 3,
        cooldown_time: float = 60.0,
        enable_health_check: bool = False,
        health_check_interval: float = 300,
        cache: Optional[LocalCache] = None,
        **strategy_kwargs
    ):
        super().__init__(
            deployments=deployments,
            num_retries=num_retries,
            timeout=timeout,
            cache=cache,
        )
        # 状态管理
        self.state = LocalRouterState()
        # 策略
        if isinstance(strategy, str):
            self.strategy = create_strategy(strategy, state=self.state, **strategy_kwargs)
        else:
            self.strategy = strategy
        # 可靠性参数
        self.allowed_fails = allowed_fails
        self.cooldown_time = cooldown_time
        # 健康检查
        self.enable_health_check = enable_health_check
        self.health_checker: Optional[SDKHealthChecker] = None
        if enable_health_check:
            self.health_checker = SDKHealthChecker(
                deployments=deployments,
                state=self.state,
                check_interval=health_check_interval
            )

    async def __aenter__(self):
        """异步上下文管理器入口"""
        if self.health_checker:
            await self.health_checker.start_background_check()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """异步上下文管理器出口"""
        if self.health_checker:
            await self.health_checker.stop_background_check()
        return False

    def _get_available_deployments(self, model: str) -> List[Deployment]:
        """获取模型可用部署"""
        now = time.time()
        all_deployments = self.get_deployments_for_model(model)
        with self.state.lock:
            available = []
            for dep in all_deployments:
                status = self.state.deployment_status.get(dep.id, DeploymentStatus.HEALTHY)
                if status == DeploymentStatus.FAILED:
                    continue
                if status == DeploymentStatus.COOLDOWN:
                    cooldown_until = self.state.cooldown_until.get(dep.id, 0)
                    if now < cooldown_until:
                        continue
                    self.state.deployment_status[dep.id] = DeploymentStatus.HEALTHY
                    self.state.cooldown_until[dep.id] = None
                available.append(dep)
        return available

    async def completion(
        self,
        model: str,
        messages: List[Dict[str, str]],
        **kwargs
    ) -> Any:
        """
        可靠性completion - 自动路由选择和重试
        """
        # 获取可用部署
        available = self._get_available_deployments(model)
        if not available:
            raise NoDeploymentAvailable(f"No available deployment for model: {model}")
        # 创建上下文
        context = RoutingContext(
            model=model,
            messages=messages,
            kwargs=kwargs
        )
        # 重试循环
        errors = []
        for attempt in range(self.num_retries + 1):
            # 选择部署
            selected = await self.strategy.select_deployment(available, context)
            if selected is None:
                # 策略无法选择，尝试下一个
                if available:
                    selected = available[0]
                else:
                    break
            # 标记尝试
            context.mark_attempt(selected)
            try:
                # 发送请求
                start_time = time.time()
                response = await super().completion(
                    model=model,
                    messages=messages,
                    deployment=selected,
                    **kwargs
                )
                end_time = time.time()
                # 成功回调
                latency = end_time - start_time
                tokens = response.get('usage', {}).get('completion_tokens', 0)
                self.strategy.on_success(selected, latency, tokens)
                context.set_success(selected, response)
                return response
            except Exception as e:
                # 失败回调
                self.strategy.on_failure(selected, e)
                context.set_failure(e)
                errors.append((selected.id, str(e)))
                # 从可用列表移除
                available = [d for d in available if d.id != selected.id]
                if not available:
                    break
        raise RouterError(f"All deployments failed after {self.num_retries + 1} attempts: {errors}")

    async def batch_completion(
        self,
        requests: List[Dict[str, Any]],
        max_concurrent: int = 10
    ) -> List[Any]:
        """
        批量completion

        Args:
            requests: 请求列表，每个包含 model, messages, **kwargs
            max_concurrent: 最大并发数
        """
        semaphore = asyncio.Semaphore(max_concurrent)
        async def process_one(req):
            async with semaphore:
                return await self.completion(**req)
        tasks = [process_one(req) for req in requests]
        return await asyncio.gather(*tasks, return_exceptions=True)

    def update_deployments(self, new_deployments: List[Deployment]) -> None:
        """更新部署列表"""
        self.deployments = new_deployments
        self._build_model_indices()
        if self.health_checker:
            self.health_checker.deployments = new_deployments

    def get_stats(self) -> Dict[str, Any]:
        """获取统计信息"""
        return {
            "total_deployments": len(self.deployments),
            "model_list": self.get_model_list(),
            "deployment_status": {
                dep.id: self.state.deployment_status.get(dep.id, DeploymentStatus.HEALTHY).value
                for dep in self.deployments
            },
            "consecutive_failures": dict(self.state.consecutive_failures),
            "latency_stats": {
                dep.id: {
                    "avg_latency": self.state.get_average_latency(dep.id),
                    "total_tokens": self.state.total_tokens.get(dep.id, 0),
                    "total_requests": self.state.total_requests.get(dep.id, 0),
                }
                for dep in self.deployments
            }
        }
