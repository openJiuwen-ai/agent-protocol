"""Provider adapter 注册表。"""
from typing import Dict, Type

from .base_provider import BaseProviderAdapter

_PROVIDER_REGISTRY: Dict[str, Type[BaseProviderAdapter]] = {}


def register_provider(name: str, adapter_cls: Type[BaseProviderAdapter]) -> None:
    """注册 provider adapter 类。"""
    _PROVIDER_REGISTRY[name] = adapter_cls


def get_provider_adapter(provider: str) -> BaseProviderAdapter:
    """获取 provider adapter 实例（无状态，每次返回新实例）。"""
    if provider not in _PROVIDER_REGISTRY:
        raise ValueError(
            f"Unknown provider: '{provider}'. "
            f"Supported providers: {list(_PROVIDER_REGISTRY.keys())}"
        )
    return _PROVIDER_REGISTRY[provider]()


# --- 内置注册 ---
from .openai_provider import OpenAIProviderAdapter
from .anthropic_provider import AnthropicProviderAdapter
from .gemini_provider import GeminiProviderAdapter
from .bedrock_provider import BedrockProviderAdapter
from .deepseek_provider import DeepSeekProviderAdapter
from .siliconflow_provider import SiliconFlowProviderAdapter
from .inference_affinity_provider import InferenceAffinityProviderAdapter
from .dashscope_provider import DashScopeProviderAdapter
from .zhipu_provider import ZhipuProviderAdapter

register_provider("openai", OpenAIProviderAdapter)
register_provider("anthropic", AnthropicProviderAdapter)
register_provider("google-gemini", GeminiProviderAdapter)
register_provider("aws-bedrock", BedrockProviderAdapter)
register_provider("deepseek", DeepSeekProviderAdapter)
register_provider("siliconflow", SiliconFlowProviderAdapter)
register_provider("inference-affinity", InferenceAffinityProviderAdapter)
register_provider("dashscope", DashScopeProviderAdapter)
register_provider("zhipu", ZhipuProviderAdapter)
