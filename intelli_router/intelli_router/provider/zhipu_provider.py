"""智谱 (Zhipu/GLM) provider adapter。

智谱 API 兼容 OpenAI 协议，端点路径为 /api/paas/v4/chat/completions。
"""
from typing import Dict

from ..core.deployment import Deployment
from .openai_provider import OpenAIProviderAdapter


class ZhipuProviderAdapter(OpenAIProviderAdapter):
    """智谱 (Zhipu) 适配器

    智谱大模型开放平台提供了 OpenAI 兼容的 Chat 端点，
    唯一差异是 API 路径为 /api/paas/v4/chat/completions。
    """

    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        base = deployment.api_base.rstrip("/")
        return f"{base}/api/paas/v4/chat/completions"
