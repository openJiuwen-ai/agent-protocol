"""OpenAI provider adapter — 默认 OpenAI 兼容行为的抽取。"""
import logging
from typing import Dict

from ..core.deployment import Deployment
from .base_provider import BaseProviderAdapter

logger = logging.getLogger(__name__)


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
        headers = {
            "Authorization": f"Bearer {deployment.api_key}",
            "Content-Type": "application/json",
        }
        if deployment.custom_headers:
            custom_header_names = {name.lower() for name in deployment.custom_headers}
            if "authorization" in custom_header_names:
                logger.warning(
                    "Deployment %s custom_headers overrides Authorization header. "
                    "This is allowed for compatibility, but may cause authentication failures.",
                    deployment.id,
                )
                for header_name in list(headers):
                    if header_name.lower() == "authorization":
                        headers.pop(header_name)
            headers.update(deployment.custom_headers)
        return headers
