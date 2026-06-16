"""DeepSeek provider adapter — 在 OpenAI 兼容基础上处理 reasoning_content。"""
from typing import Dict, Any, List

from ..core.deployment import Deployment
from .openai_provider import OpenAIProviderAdapter


class DeepSeekProviderAdapter(OpenAIProviderAdapter):
    """DeepSeek 适配器

    DeepSeek API 整体兼容 OpenAI 协议，差异点：
    1. API 路径为 /chat/completions（无 /v1/ 前缀）
    2. 所有 role=assistant 的消息必须显式携带 reasoning_content 字段
       （即使为空字符串），否则 API 校验失败。
    """

    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        base = deployment.api_base.rstrip("/")
        return f"{base}/chat/completions"

    def transform_request(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        deployment: Deployment,
        **kwargs,
    ) -> Dict[str, Any]:
        body = super().transform_request(model, messages, deployment, **kwargs)
        # 浅拷贝每条消息，避免修改调用方原始列表
        new_messages = []
        for msg in body.get("messages", []):
            if msg.get("role") == "assistant" and "reasoning_content" not in msg:
                msg = {**msg, "reasoning_content": ""}
            new_messages.append(msg)
        body["messages"] = new_messages
        return body
