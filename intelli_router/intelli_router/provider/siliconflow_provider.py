"""SiliconFlow provider adapter — 在 OpenAI 兼容基础上清洗 tool_calls。"""
from typing import Dict, Any

from ..core.deployment import Deployment
from .base_provider import BaseProviderAdapter
from .openai_provider import OpenAIProviderAdapter


class SiliconFlowProviderAdapter(OpenAIProviderAdapter):
    """SiliconFlow（硅基流动）适配器

    SiliconFlow API 整体兼容 OpenAI 协议，但对 tool_calls 的字段校验
    非常严格，不允许任何额外字段（如 index）。需要在请求发送前清洗。
    """

    def transform_request(
        self,
        model: str,
        messages: list[Dict[str, Any]],
        deployment: Deployment,
        **kwargs,
    ) -> Dict[str, Any]:
        body = super().transform_request(model, messages, deployment, **kwargs)
        BaseProviderAdapter.sanitize_tool_calls(body.get("messages", []))
        return body
