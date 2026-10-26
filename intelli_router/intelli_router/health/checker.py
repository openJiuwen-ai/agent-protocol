"""
SDK LLM Health - 健康检查
"""
from typing import Dict, List, Optional, Set
from dataclasses import dataclass, field
import asyncio
import json
import time
import httpx
from ..core.deployment import Deployment, DeploymentStatus
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
        # 按 verify_ssl 分组缓存的 httpx 客户端（与 BaseRouter._ensure_client 同范式）
        self._clients: Dict[bool, httpx.AsyncClient] = {}
        self._client: Optional[httpx.AsyncClient] = None
        self._adapter_cache: dict = {}

    def _ensure_client(self, verify: bool = True) -> httpx.AsyncClient:
        """获取或创建可复用的httpx客户端（按 verify_ssl 分组缓存）"""
        client = self._clients.get(verify)
        if client is None or client.is_closed:
            client = httpx.AsyncClient(
                verify=verify,
                timeout=self.check_timeout,
            )
            self._clients[verify] = client
        # 兼容视图：供测试/外部注入直接读 _client
        self._client = client
        return client

    def _get_cached_adapter(self, provider: str):
        if provider not in self._adapter_cache:
            self._adapter_cache[provider] = get_provider_adapter(provider)
        return self._adapter_cache[provider]

    async def close(self) -> None:
        """关闭所有缓存的httpx客户端（含全部 verify 分组）"""
        for client in self._clients.values():
            if client is not None and not client.is_closed:
                await client.aclose()
        self._clients.clear()
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
            # SigV4 签名的是 body 字节：必须先 json.dumps 成 bytes，
            # 再以 content= 传递，保证签名与实际请求体一致
            # （json= 参数会由 httpx 重新序列化，字节可能不一致）。
            body_bytes = json.dumps(request_body).encode("utf-8")
            headers = adapter.sign_request(
                "POST", url, headers, body_bytes, deployment
            )

            client = self._ensure_client(deployment.verify_ssl)
            response = await client.post(
                url, headers=headers, content=body_bytes
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
        """获取健康部署列表

        两重条件（缺一不可）：
        1. 健康检查通过：state.health_state 为 True（默认 True）；
        2. 运行期状态可用：RouterState 里非"未过期的 COOLDOWN"。

        运行期可用性以 RouterState 为准（base_strategy 契约明示
        "state 是运行期状态的唯一事实源"）：真实请求失败触发的冷却
        只写进 state（on_failure），不会写回 Deployment 对象。此前
        用 dep.is_available(now) 判定读不到运行期冷却——router 认为
        冷却中而 checker 报健康，与 _get_available_deployments 口径
        不一致。

        Deployment.is_available 只反映构造期静态配置（Deployment
        自带的 status/cooldown_until 字段），保留给构造期静态检查
        （如配置装载时校验）使用，不用于运行期判定。
        """
        healthy = []
        for dep in self.deployments:
            if not self.state.health_state.get(dep.id, True):
                continue
            if not self._is_state_available(dep.id, now):
                continue
            healthy.append(dep)
        return healthy

    def _is_state_available(self, deployment_id: str, now: float) -> bool:
        """按 RouterState 判定部署运行期是否可用

        与 ReliableRouter._get_available_deployments 同口径：
        state.deployment_status 非 COOLDOWN 即可用；COOLDOWN 但
        cooldown_until 已过期也视为可用（DeploymentStatus 枚举只有
        HEALTHY/COOLDOWN，不存在 UNHEALTHY 成员）。

        这里只做判定、不做 reset_deployment 软恢复——软恢复是 router
        调度路径的职责，查询方法不应有写副作用。
        """
        with self.state.lock:
            status = self.state.deployment_status.get(
                deployment_id, DeploymentStatus.HEALTHY
            )
            if status == DeploymentStatus.COOLDOWN:
                cooldown_until = self.state.cooldown_until.get(deployment_id, 0)
                if now < cooldown_until:
                    return False
            return True

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
