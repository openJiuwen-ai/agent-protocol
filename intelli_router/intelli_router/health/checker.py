"""
SDK LLM Health - 健康检查
"""
from typing import Dict, List, Optional, Set
from dataclasses import dataclass, field
import asyncio
import time
import httpx
from ..core.deployment import Deployment
from ..core.state import LocalRouterState
from ..provider.registry import get_provider_adapter

# 默认检查间隔 (秒)
DEFAULT_CHECK_INTERVAL = 300
# 默认检查超时 (秒)
DEFAULT_CHECK_TIMEOUT = 5
# 默认检查消息
DEFAULT_CHECK_MESSAGE = [{"role": "user", "content": "ping"}]
# 默认最大 tokens
DEFAULT_CHECK_MAX_TOKENS = 10

@dataclass
class HealthCheckResult:
    """健康检查结果"""
    deployment_id: str
    is_healthy: bool
    latency: Optional[float] = None
    error: Optional[str] = None
    timestamp: float = field(default_factory=time.time)

class SDKHealthChecker:
    """
    SDK健康检查器 - 检查部署可用性

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
        self._running = False
        self._task: Optional[asyncio.Task] = None
        self._client: Optional[httpx.AsyncClient] = None
        self._adapter_cache: dict = {}

    def _ensure_client(self) -> httpx.AsyncClient:
        if self._client is None:
            self._client = httpx.AsyncClient(timeout=self.check_timeout)
        return self._client

    def _get_cached_adapter(self, provider: str):
        if provider not in self._adapter_cache:
            self._adapter_cache[provider] = get_provider_adapter(provider)
        return self._adapter_cache[provider]

    async def close(self) -> None:
        if self._client is not None:
            await self._client.aclose()
            self._client = None

    async def check_deployment(
        self,
        deployment: Deployment
    ) -> HealthCheckResult:
        """
        检查单个部署健康状态
        策略: 发送一个最小化completion请求
        """
        start = time.time()
        try:
            adapter = self._get_cached_adapter(deployment.provider)
            request_body = adapter.transform_request(
                model=deployment.model_name,
                messages=self.check_message,
                deployment=deployment,
                max_tokens=self.check_max_tokens,
            )
            url = adapter.get_api_url(deployment)
            headers = adapter.get_headers(deployment)

            client = self._ensure_client()
            response = await client.post(
                url, headers=headers, json=request_body
            )
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
        """获取健康部署列表"""
        healthy = []
        for dep in self.deployments:
            if self.state.health_state.get(dep.id, True):
                if dep.is_available(now):
                    healthy.append(dep)
        return healthy

    def get_unhealthy_ids(self) -> Set[str]:
        """获取不健康部署ID集合"""
        return {
            dep_id
            for dep_id, is_healthy in self.state.health_state.items()
            if not is_healthy
        }

    async def start_background_check(self) -> None:
        """启动后台健康检查"""
        if self._running:
            return
        self._running = True
        self._task = asyncio.create_task(self._background_loop())

    async def stop_background_check(self) -> None:
        """停止后台健康检查"""
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None
        await self.close()

    async def _background_loop(self) -> None:
        """后台检查循环"""
        while self._running:
            try:
                await self.check_all_deployments()
            except Exception:
                pass
            await asyncio.sleep(self.check_interval)
