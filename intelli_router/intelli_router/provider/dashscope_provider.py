"""DashScope（阿里云通义千问）provider adapter。

Chat completion 完全复用 OpenAI 路径，生成类 API（image/speech/video）
由 ModelClient 层按需路由到 DashScope 专有 SDK。
"""
from ..core.deployment import Deployment
from .openai_provider import OpenAIProviderAdapter


class DashScopeProviderAdapter(OpenAIProviderAdapter):
    """DashScope 适配器

    DashScope（通义千问）提供了 OpenAI 兼容的 Chat 端点，
    所以聊天类请求完全复用 OpenAIProviderAdapter。

    文生图 / 语音合成 / 视频生成等能力通过 DashScope 专有 SDK
    （MultiModalConversation / VideoSynthesis）实现，
    由 ModelClient.generate_image/speech/video() 按 provider 路由。
    这些方法不在 adapter 层实现。
    """

    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        base = deployment.api_base.rstrip("/")
        return f"{base}/compatible-mode/v1/chat/completions"
