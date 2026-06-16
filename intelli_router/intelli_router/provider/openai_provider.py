"""OpenAI provider adapter — 默认 OpenAI 兼容行为的抽取。"""
from typing import Dict

from ..core.deployment import Deployment
from .base_provider import BaseProviderAdapter


class OpenAIProviderAdapter(BaseProviderAdapter):
    """OpenAI / 兼容 endpoint 适配器

    transform_request / transform_response / transform_stream_chunk 均
    使用 BaseProviderAdapter 的默认实现（透传），因为 intelli_router
    内部本就是用 OpenAI 格式作为标准格式。
    """

    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        base = deployment.api_base.rstrip("/")
        return f"{base}/v1/chat/completions"

    def get_headers(self, deployment: Deployment) -> Dict[str, str]:
        return {
            "Authorization": f"Bearer {deployment.api_key}",
            "Content-Type": "application/json",
        }
