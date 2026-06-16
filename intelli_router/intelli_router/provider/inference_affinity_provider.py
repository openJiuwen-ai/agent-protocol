"""InferenceAffinity provider adapter — 支持 vLLM KV cache 共享/释放。"""
from typing import Dict, Any

from ..core.deployment import Deployment
from .base_provider import BaseProviderAdapter
from .openai_provider import OpenAIProviderAdapter


class InferenceAffinityProviderAdapter(OpenAIProviderAdapter):
    """InferenceAffinity（vLLM 部署）适配器

    vLLM 部署在 OpenAI 兼容基础上额外支持：
    - KV cache 共享（cache_sharing + cache_salt）
    - tool_calls 字段清洗（与 SiliconFlow 类似）
    """

    def transform_request(
        self,
        model: str,
        messages: list[Dict[str, Any]],
        deployment: Deployment,
        **kwargs,
    ) -> Dict[str, Any]:
        session_id = kwargs.pop("session_id", None)
        enable_cache_sharing = kwargs.pop("enable_cache_sharing", False)
        return_token_ids = kwargs.pop("return_token_ids", None)

        body = super().transform_request(model, messages, deployment, **kwargs)
        BaseProviderAdapter.sanitize_tool_calls(body.get("messages", []))

        if enable_cache_sharing and session_id:
            body["cache_sharing"] = True
            body["cache_salt"] = session_id

        if return_token_ids is not None:
            body["return_token_ids"] = return_token_ids

        return body
