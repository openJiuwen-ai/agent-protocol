"""
SDK LLM Router - 基础路由

BaseRouter: 处理API池化和请求转发
"""
from typing import Dict, List, Optional, Any, AsyncIterator
import json
import asyncio
import logging
import threading
import time

import httpx

logger = logging.getLogger(__name__)

from ..core.deployment import Deployment, DeploymentStatus
from ..core.context import RoutingContext
from ..core.state import LocalRouterState
from ..cache.local_cache import LocalCache
from ..utils.exceptions import (
    RouterError,
    NoDeploymentAvailable,
    DeploymentError,
    DeploymentTimeoutError,
    DeploymentAuthError,
    DeploymentRateLimitError,
    DeploymentServerError,
    DeploymentNetworkError,
)
from ..provider.base_provider import BaseProviderAdapter

# 默认重试次数
DEFAULT_NUM_RETRIES = 0
# 默认超时
DEFAULT_TIMEOUT = 30.0

class BaseRouter:
    """
    基础路由器 - 处理API池化和请求转发
    """

    def __init__(
        self,
        deployments: List[Deployment],
        num_retries: int = DEFAULT_NUM_RETRIES,
        timeout: float = DEFAULT_TIMEOUT,
        cache: Optional[LocalCache] = None
    ):
        self.deployments = deployments
        self.num_retries = num_retries
        self.timeout = timeout
        self.cache = cache or LocalCache()
        # 运行期状态（部署状态/冷却等）。BaseRouter 自身只读它做
        # 可用性过滤；ReliableRouter 会以自己的 state 覆盖此属性
        # （ReliableRouter 是该 state 的唯一写入方）。
        self.state = LocalRouterState()
        # 按 verify_ssl 分组缓存的 httpx 客户端：{verify: AsyncClient}。
        # _client 单属性仅作兼容视图指向最后创建/使用的 client。
        self._clients: Dict[bool, httpx.AsyncClient] = {}
        self._client: Optional[httpx.AsyncClient] = None
        self._adapter_cache: Dict[str, BaseProviderAdapter] = {}
        # 保护 deployments/model_indices 的热替换（读多写少）
        self._deployments_lock = threading.RLock()
        self._build_model_indices()

    def _build_model_indices(self) -> None:
        """构建模型名到部署列表的映射

        先在局部变量构建完成后一次性原子赋值，避免并发请求在
        "已清空、未填完" 的窗口期读到空索引（热替换场景）。
        """
        indices: Dict[str, List[int]] = {}
        for i, dep in enumerate(self.deployments):
            model = dep.model_name
            if model not in indices:
                indices[model] = []
            indices[model].append(i)
        self.model_indices = indices

    def get_deployments_for_model(self, model: str) -> List[Deployment]:
        """获取指定模型的所有部署

        与 update_deployments 的"先换列表再重建索引"热替换并发时，
        无锁读取可能拿到新列表+旧索引（或反之）导致 IndexError。
        加锁把"取索引 + 取列表"变成原子操作；_deployments_lock 是
        RLock，外层已持锁的调用方（如 _get_available_deployments）
        重入不会死锁。
        """
        with self._deployments_lock:
            indices = self.model_indices.get(model, [])
            return [self.deployments[i] for i in indices]

    def get_model_list(self) -> List[str]:
        """获取所有模型名列表"""
        return list(self.model_indices.keys())

    def get_deployment_configs(self) -> List[Dict[str, Any]]:
        """获取所有部署配置详情

        出于安全考虑不回传 api_key 明文，只暴露是否已配置（has_api_key）。
        """
        return [
            {
                "id": dep.id,
                "model_id": dep.model_id,
                "model_name": dep.model_name,
                "api_base": dep.api_base,
                "has_api_key": bool(dep.api_key),
                "fallback_tag": dep.fallback_tag,
                "model_description": dep.model_description,
            }
            for dep in self.deployments
        ]

    def get_deployment_config_by_model(self, model: str) -> List[Dict[str, Any]]:
        """获取指定模型的部署配置详情

        出于安全考虑不回传 api_key 明文，只暴露是否已配置（has_api_key）。
        """
        return [
            {
                "id": dep.id,
                "model_id": dep.model_id,
                "model_name": dep.model_name,
                "api_base": dep.api_base,
                "has_api_key": bool(dep.api_key),
                "fallback_tag": dep.fallback_tag,
                "model_description": dep.model_description,
            }
            for dep in self.deployments
            if dep.model_name == model
        ]

    def _get_adapter(self, deployment: Deployment) -> BaseProviderAdapter:
        """获取 provider adapter（带缓存）。"""
        provider = deployment.provider
        if provider not in self._adapter_cache:
            from ..provider.registry import get_provider_adapter

            self._adapter_cache[provider] = get_provider_adapter(provider)
        return self._adapter_cache[provider]

    def _ensure_client(self, verify: bool = True) -> httpx.AsyncClient:
        """获取或创建可复用的httpx客户端（按 verify_ssl 分组缓存）

        同一 verify 配置的部署共享一个 client；不同 verify 配置
        （如 verify_ssl=False 的自签/内网部署）各持有独立 client，
        避免共享 client 吞掉 verify_ssl 差异。
        """
        client = self._clients.get(verify)
        if client is None or client.is_closed:
            client = httpx.AsyncClient(
                verify=verify,
                timeout=httpx.Timeout(
                    connect=self.timeout,
                    read=self.timeout,
                    write=self.timeout,
                    pool=self.timeout,
                )
            )
            self._clients[verify] = client
        # 兼容视图：供测试/外部注入直接读 _client（如注入 MockTransport client）
        self._client = client
        return client

    def _request_timeout(self, deployment: Deployment) -> float:
        """计算请求实际生效的超时：deployment 显式设置优先，否则回退 router 默认。"""
        if deployment.timeout is not None:
            return deployment.timeout
        return self.timeout

    async def _make_request(
        self,
        deployment: Deployment,
        request_body: Dict[str, Any]
    ) -> Any:
        """
        发送HTTP请求并处理异常映射

        Args:
            deployment: 目标部署
            request_body: 请求体

        Returns:
            API响应JSON

        Raises:
            DeploymentTimeoutError: 请求超时
            DeploymentAuthError: 认证失败 (401/403)
            DeploymentRateLimitError: 限流 (429)
            DeploymentServerError: 服务端错误 (5xx)
            DeploymentNetworkError: 网络连接错误
            DeploymentError: 其他部署错误
        """
        client = self._ensure_client(deployment.verify_ssl)
        adapter = self._get_adapter(deployment)
        url = adapter.get_api_url(deployment, stream=False)
        headers = adapter.get_headers(deployment)
        body_bytes = json.dumps(request_body).encode("utf-8")
        headers = adapter.sign_request("POST", url, headers, body_bytes, deployment)
        # per-request 超时覆盖 client 默认值（deployment 显式设置优先）
        request_timeout = self._request_timeout(deployment)

        try:
            response = await client.post(
                url, headers=headers, content=body_bytes, timeout=request_timeout
            )
            response.raise_for_status()
            raw = response.json()
            return adapter.transform_response(raw, deployment.model_name, deployment)
        except httpx.TimeoutException as e:
            raise DeploymentTimeoutError(
                deployment_id=deployment.id,
                timeout=request_timeout,
            ) from e
        except httpx.HTTPStatusError as e:
            status = e.response.status_code
            body = e.response.text
            if status in (401, 403):
                raise DeploymentAuthError(
                    deployment_id=deployment.id, status_code=status
                ) from e
            elif status == 429:
                retry_after = None
                if "retry-after" in e.response.headers:
                    try:
                        retry_after = float(e.response.headers["retry-after"])
                    except (ValueError, TypeError):
                        pass
                raise DeploymentRateLimitError(
                    deployment_id=deployment.id, retry_after=retry_after
                ) from e
            elif 500 <= status < 600:
                raise DeploymentServerError(
                    deployment_id=deployment.id,
                    status_code=status,
                    response_body=body,
                ) from e
            else:
                raise DeploymentError(
                    message=f"Deployment '{deployment.id}' returned HTTP {status}",
                    details={
                        "deployment_id": deployment.id,
                        "status_code": status,
                        "response_body": body,
                    },
                ) from e
        except httpx.ConnectError as e:
            raise DeploymentNetworkError(
                deployment_id=deployment.id, reason=str(e)
            ) from e
        except httpx.RemoteProtocolError as e:
            raise DeploymentNetworkError(
                deployment_id=deployment.id, reason=str(e)
            ) from e
        except httpx.HTTPError as e:
            raise DeploymentError(
                message=f"Deployment '{deployment.id}' HTTP error: {e}",
                details={"deployment_id": deployment.id, "original_error": str(e)},
            ) from e

    async def close(self) -> None:
        """关闭底层httpx客户端，释放连接池（关闭所有 verify 分组的缓存 client）"""
        for client in self._clients.values():
            if client is not None and not client.is_closed:
                await client.aclose()
        self._clients.clear()
        self._client = None

    async def __aenter__(self):
        return self

    async def __aexit__(self, *args):
        await self.close()

    def _state_available_deployments(self, deployments: List[Deployment]) -> List[Deployment]:
        """按 state 过滤可用部署（BaseRouter 层的简化判断）。

        state 中标记为 COOLDOWN 且冷却截止时间未过的部署被跳过；
        冷却已过期的部署直接视为可用（不写回 state）；未登记的部署
        默认 HEALTHY。

        与 ReliableRouter._get_available_deployments 的差异：本方法
        不加 state 锁（BaseRouter 读多写少的轻量路径），也不做冷却
        到期的软恢复（reset_deployment，恢复 health_state 等）——那
        些写操作属于 ReliableRouter 的职责。锁与软恢复的统一不在
        本轮范围内。
        """
        now = time.time()
        available = []
        for dep in deployments:
            status = self.state.deployment_status.get(dep.id, DeploymentStatus.HEALTHY)
            if status == DeploymentStatus.COOLDOWN:
                cooldown_until = self.state.cooldown_until.get(dep.id, 0)
                if now < cooldown_until:
                    continue
            available.append(dep)
        return available

    async def completion(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        deployment: Optional[Deployment] = None,
        **kwargs
    ) -> Any:
        """
        发送completion请求

        Args:
            model: 模型名
            messages: 消息列表
            deployment: 指定部署 (可选)
            **kwargs: 其他参数

        Returns:
            API响应
        """
        if deployment is None:
            deployments = self._state_available_deployments(
                self.get_deployments_for_model(model)
            )
            if not deployments:
                raise NoDeploymentAvailable(model, "No deployment")
            deployment = deployments[0]
        adapter = self._get_adapter(deployment)
        adapter.validate_request_config(deployment=deployment, config=kwargs)
        request_body = adapter.transform_request(
            model=model, messages=messages, deployment=deployment, **kwargs
        )
        return await self._make_request(deployment, request_body)

    async def acompletion_stream(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        deployment: Optional[Deployment] = None,
        **kwargs
    ) -> AsyncIterator[Dict[str, Any]]:
        """
        流式 completion — 以 AsyncIterator 形式 yield OpenAI 统一 SSE 格式的 chunk。

        Args:
            model: 模型名
            messages: 消息列表
            deployment: 指定部署 (可选)
            **kwargs: 其他参数

        Yields:
            标准 OpenAI 格式的 streaming chunk dict
        """
        if deployment is None:
            deployments = self.get_deployments_for_model(model)
            if not deployments:
                raise NoDeploymentAvailable(model, "No deployment")
            deployment = deployments[0]

        adapter = self._get_adapter(deployment)
        stream_kwargs = {"stream": True, **kwargs}
        adapter.validate_request_config(deployment=deployment, config=stream_kwargs)
        request_body = adapter.transform_request(
            model=model, messages=messages, deployment=deployment, **stream_kwargs
        )
        client = self._ensure_client(deployment.verify_ssl)
        url = adapter.get_api_url(deployment, stream=True)
        headers = adapter.get_headers(deployment)
        body_bytes = json.dumps(request_body).encode("utf-8")
        headers = adapter.sign_request("POST", url, headers, body_bytes, deployment)
        # per-request 超时覆盖 client 默认值（deployment 显式设置优先）
        request_timeout = self._request_timeout(deployment)

        try:
            async with client.stream(
                "POST", url, headers=headers, content=body_bytes,
                timeout=request_timeout,
            ) as response:
                response.raise_for_status()
                async for chunk in adapter.iter_stream_events(response):
                    mapped = adapter.transform_stream_chunk(
                        chunk, model, deployment
                    )
                    if mapped is not None:
                        yield mapped
        except httpx.TimeoutException as e:
            raise DeploymentTimeoutError(
                deployment_id=deployment.id,
                timeout=request_timeout,
            ) from e
        except httpx.HTTPStatusError as e:
            status = e.response.status_code
            body = e.response.text
            if status in (401, 403):
                raise DeploymentAuthError(
                    deployment_id=deployment.id, status_code=status
                ) from e
            elif status == 429:
                retry_after = None
                if "retry-after" in e.response.headers:
                    try:
                        retry_after = float(e.response.headers["retry-after"])
                    except (ValueError, TypeError):
                        pass
                raise DeploymentRateLimitError(
                    deployment_id=deployment.id, retry_after=retry_after
                ) from e
            elif 500 <= status < 600:
                raise DeploymentServerError(
                    deployment_id=deployment.id,
                    status_code=status,
                    response_body=body,
                ) from e
            else:
                raise DeploymentError(
                    message=f"Deployment '{deployment.id}' returned HTTP {status}",
                    details={"deployment_id": deployment.id, "status_code": status, "response_body": body},
                ) from e
        except httpx.ConnectError as e:
            raise DeploymentNetworkError(
                deployment_id=deployment.id, reason=str(e)
            ) from e
        except httpx.RemoteProtocolError as e:
            raise DeploymentNetworkError(
                deployment_id=deployment.id, reason=str(e)
            ) from e
        except httpx.HTTPError as e:
            raise DeploymentError(
                message=f"Deployment '{deployment.id}' HTTP error: {e}",
                details={"deployment_id": deployment.id, "original_error": str(e)},
            ) from e

    async def completion_with_fallback(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        fallback: Optional[Dict[str, str]] = None,
        **kwargs
    ) -> Any:
        """
        带fallback的completion

        Args:
            fallback: 模型名 -> fallback模型名映射
        """
        models_to_try = [model]
        if fallback and model in fallback:
            models_to_try.append(fallback[model])
        errors = []
        for m in models_to_try:
            deployments = self.get_deployments_for_model(m)
            for dep in deployments:
                try:
                    return await self.completion(m, messages, deployment=dep, **kwargs)
                except Exception as e:
                    errors.append((dep.id, str(e)))
        raise RouterError(f"All deployments failed: {errors}")
