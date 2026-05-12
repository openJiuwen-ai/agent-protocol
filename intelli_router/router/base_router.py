"""
SDK LLM Router - 基础路由

BaseRouter: 处理API池化和请求转发
"""
from typing import Dict, List, Optional, Any
import asyncio
import httpx

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

# 黙认重试次数
DEFAULT_NUM_RETRIES = 0
# 黙认超时
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
        self._client: Optional[httpx.AsyncClient] = None
        self._build_model_indices()

    def _build_model_indices(self) -> None:
        """构建模型名到部署列表的映射"""
        self.model_indices: Dict[str, List[int]] = {}
        for i, dep in enumerate(self.deployments):
            model = dep.model_name
            if model not in self.model_indices:
                self.model_indices[model] = []
            self.model_indices[model].append(i)

    def get_deployments_for_model(self, model: str) -> List[Deployment]:
        """获取指定模型的所有部署"""
        indices = self.model_indices.get(model, [])
        return [self.deployments[i] for i in indices]

    def get_model_list(self) -> List[str]:
        """获取所有模型名列表"""
        return list(self.model_indices.keys())

    def get_deployment_configs(self) -> List[Dict[str, Any]]:
        """获取所有部署配置详情"""
        return [
            {
                "id": dep.id,
                "model_name": dep.model_name,
                "api_base": dep.api_base,
                "api_key": dep.api_key,
            }
            for dep in self.deployments
        ]

    def get_deployment_config_by_model(self, model: str) -> List[Dict[str, Any]]:
        """获取指定模型的部署配置详情"""
        return [
            {
                "id": dep.id,
                "model_name": dep.model_name,
                "api_base": dep.api_base,
                "api_key": dep.api_key,
            }
            for dep in self.deployments
            if dep.model_name == model
        ]

    def _ensure_client(self) -> httpx.AsyncClient:
        """获取或创建可复用的httpx客户端"""
        if self._client is None:
            self._client = httpx.AsyncClient(
                timeout=httpx.Timeout(
                    connect=self.timeout,
                    read=self.timeout,
                    write=self.timeout,
                    pool=self.timeout,
                )
            )
        return self._client

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
        client = self._ensure_client()
        url = f"{deployment.api_base.rstrip('/')}/v1/chat/completions"
        headers = {
            "Authorization": f"Bearer {deployment.api_key}",
            "Content-Type": "application/json",
        }

        try:
            response = await client.post(url, headers=headers, json=request_body)
            response.raise_for_status()
            return response.json()
        except httpx.TimeoutException as e:
            raise DeploymentTimeoutError(
                deployment_id=deployment.id,
                timeout=self.timeout,
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
        """关闭底层httpx客户端，释放连接池"""
        if self._client is not None:
            await self._client.aclose()
            self._client = None

    async def __aenter__(self):
        return self

    async def __aexit__(self, *args):
        await self.close()

    async def completion(
        self,
        model: str,
        messages: List[Dict[str, str]],
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
            deployments = self.get_deployments_for_model(model)
            if not deployments:
                raise NoDeploymentAvailable(f"No deployment for model: {model}")
            deployment = deployments[0]
        request_body = {
            "model": model,
            "messages": messages,
            **kwargs,
        }
        return await self._make_request(deployment, request_body)

    async def completion_with_fallback(
        self,
        model: str,
        messages: List[Dict[str, str]],
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


