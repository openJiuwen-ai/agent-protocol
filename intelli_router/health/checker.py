"""
SDK LLM Health - 健康检查

SDKHealthChecker: 检查部署可用性
HealthCheckResult: 检查结果
"""
from typing import Dict, List, Optional, Set
from dataclasses import dataclass, field
import asyncio
import time
import httpx
from ..core.deployment import Deployment
from ..core.state import LocalRouterState
from ..utils.exceptions import HealthCheckError

# 默认检查间隔 (秒)
DEFAULT_CHECK_INTERVAL = 300
# 黙认检查超时 (秒)
DEFAULT_CHECK_TIMEOUT = 5
# 黙认检查消息
DEFAULT_CHECK_MESSAGE = [{"role": "user", "content": "ping"}]
# 黙认最大 tokens
DEFAULT_CHECK_MAX_TOKENS = 10

@dataclass
class HealthCheckResult:
    """健庭检查结果"""
    deployment_id: str
    is_healthy: bool
    latency: Optional[float] = None
    error: Optional[str] = None
    timestamp: float = field(default_factory=time.time)

class SDKHealthChecker:
    """
    SDK健庭检查器 - 检查部署可用性

    按需检查 + 后台可选
    """

    def __init__(
        self,
        deployments: List[Deployment],
        state: LocalRouterState,
        check_interval: float = DEFAULT_CHECK_INTERVAL,
        check_timeout: float = DEFAULT_CHECK_TIMEOUT,
        check_message: List[Dict] = DEFAULT_CHECK_MESSAGE,
        check_max_tokens: int = DEFAULT_CHECK_MAX_TOKENS
    ):
        self.deployments = deployments
        self.state = state
        self.check_interval = check_interval
        self.check_timeout = check_timeout
        self.check_message = check_message
        self.check_max_tokens = check_max_tokens
        # 运行状态
        self._running = False
        self._task: Optional[asyncio.Task] = None

    async def check_deployment(
        self,
        deployment: Deployment
    ) -> HealthCheckResult:
        """
        检查单个部署健庭状态
        策略: 发送一个最小化completion请求
        """
        start = time.time()
        try:
            # 构造请求
            async with httpx.AsyncClient(timeout=self.check_timeout) as client:
                response = await client.post(
                    f"{deployment.api_base}/chat/completions",
                    headers={
                        "Authorization": f"Bearer {deployment.api_key}",
                        "Content-Type": "application/json"
                    },
                    json={
                        "model": deployment.model_name,
                        "messages": self.check_message,
                        "max_tokens": self.check_max_tokens
                    }
                )
                # 检查响应
                if response.status_code == 200:
                    latency = time.time() - start
                    return HealthCheckResult(
                        deployment_id=deployment.id,
                        is_healthy=True,
                        latency=latency
                    )
                else:
                    return HealthCheckResult(
                        deployment_id=deployment.id,
                        is_healthy=False,
                        error=f"HTTP {response.status_code}"
                    )
        except Exception as e:
            return HealthCheckResult(
                deployment_id=deployment.id,
                is_healthy=False,
                error=str(e)
            )

    async def check_all_deployments(self) -> Dict[str, HealthCheckResult]:
        """检查所有部署"""
        results = {}
        # 并发检查
        tasks = [
            self.check_deployment(dep)
            for dep in self.deployments
        ]
        check_results = await asyncio.gather(*tasks, return_exceptions=True)
        for dep, result in zip(self.deployments, check_results):
            if isinstance(result, Exception):
                results[dep.id] = HealthCheckResult(
                    deployment_id=dep.id,
                    is_healthy=False,
                    error=str(result)
                )
            else:
                results[dep.id] = result
                self.state.update_health(dep.id, result.is_healthy)
        return results

    def get_healthy_deployments(self, now: float) -> List[Deployment]:
        """获取健庭部署列表"""
        healthy = []
        for dep in self.deployments:
            if self.state.health_state.get(dep.id, True):
                if dep.is_available(now):
                    healthy.append(dep)
        return healthy

    def get_unhealthy_ids(self) -> Set[str]:
        """获取不健庭部署ID集合"""
        return {
            dep_id
            for dep_id, is_healthy in self.state.health_state.items()
            if not is_healthy
        }

    async def start_background_check(self) -> None:
        """启动后台健庭检查"""
        if self._running:
            return
        self._running = True
        self._task = asyncio.create_task(self._background_loop())

    async def stop_background_check(self) -> None:
        """停止后台健庭检查"""
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None

    async def _background_loop(self) -> None:
        """后台检查循环"""
        while self._running:
            try:
                await self.check_all_deployments()
            except Exception as e:
                # 记录错误但继续运行
                pass
            await asyncio.sleep(self.check_interval)
