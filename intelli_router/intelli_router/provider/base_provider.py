"""Provider 适配器基类定义。"""
import json
import time
from abc import ABC, abstractmethod
from typing import TYPE_CHECKING, AsyncIterator, Dict, List, Optional, Any, Tuple

from ..core.deployment import Deployment

if TYPE_CHECKING:
    import httpx


class BaseProviderAdapter(ABC):
    """Provider 适配器基类

    每个 provider（OpenAI / Anthropic / Gemini 等）实现此接口，
    intelli_router 在 HTTP 请求层插入 adapter 完成格式转换。
    """

    # Anthropic 系（Anthropic / Bedrock Converse）通用 stop_reason -> OpenAI finish_reason 映射。
    # Bedrock 多出的 content_filtered 键对纯 Anthropic 无害（查表只命中存在的键）。
    ANTHROPIC_STOP_REASON_MAP = {
        "end_turn": "stop",
        "stop_sequence": "stop",
        "tool_use": "tool_calls",
        "max_tokens": "length",
        "content_filtered": "content_filter",
    }

    @abstractmethod
    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        """构建 API URL（含路径和 query params）"""
        ...

    @abstractmethod
    def get_headers(self, deployment: Deployment) -> Dict[str, str]:
        """构建 HTTP 请求头（含认证信息）"""
        ...

    def transform_request(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        deployment: Deployment,
        **kwargs,
    ) -> Dict[str, Any]:
        """将 OpenAI 风格参数转换为 Provider 原生请求体

        默认实现：直接合并 model + messages + kwargs（OpenAI 兼容行为）。
        """
        return {"model": model, "messages": messages, **kwargs}

    def transform_response(
        self,
        raw_response: Dict[str, Any],
        model: str,
        deployment: Deployment,
    ) -> Dict[str, Any]:
        """将 Provider 原生响应转换为 OpenAI 统一格式

        返回结果中必须包含 usage.completion_tokens 字段（ReliableRouter 依赖）。
        默认实现：透传（OpenAI 兼容行为）。
        """
        return raw_response

    def transform_stream_chunk(
        self,
        chunk: Dict[str, Any],
        model: str,
        deployment: Deployment,
    ) -> Optional[Dict[str, Any]]:
        """将 Provider 原生 streaming chunk 转换为 OpenAI 统一 SSE 格式

        返回 None 表示该 chunk 应被跳过（例如 keepalive / ping）。
        默认实现：透传（OpenAI 兼容行为）。
        """
        return chunk

    def sign_request(
        self,
        method: str,
        url: str,
        headers: Dict[str, str],
        body: bytes,
        deployment: Deployment,
    ) -> Dict[str, str]:
        """签名请求并返回更新后的 headers。

        默认实现：原样返回（无需签名）。
        需要请求签名的 provider（如 AWS Bedrock SigV4）应覆盖此方法。
        """
        return headers

    async def iter_stream_events(
        self,
        response: "httpx.Response",
    ) -> AsyncIterator[Dict[str, Any]]:
        """从 HTTP 响应中迭代流式事件。

        默认实现：标准 SSE 格式解析（data: ... 行）。
        使用非 SSE 流式协议的 provider（如 Bedrock 二进制 Event Stream）应覆盖此方法。
        """
        async for line in response.aiter_lines():
            if not line:
                continue
            if line.startswith("data: "):
                data = line[6:].strip()
                if data == "[DONE]":
                    break
                try:
                    yield json.loads(data)
                except json.JSONDecodeError:
                    continue

    @staticmethod
    def _build_completion_response(
        id: str,
        model: str,
        message: Dict[str, Any],
        finish_reason: str,
        prompt_tokens: int,
        completion_tokens: int,
    ) -> Dict[str, Any]:
        """构建标准 OpenAI chat.completion 响应信封。"""
        return {
            "id": id,
            "object": "chat.completion",
            "created": int(time.time()),
            "model": model,
            "choices": [{"index": 0, "message": message, "finish_reason": finish_reason}],
            "usage": {
                "prompt_tokens": prompt_tokens,
                "completion_tokens": completion_tokens,
                "total_tokens": prompt_tokens + completion_tokens,
            },
        }

    @staticmethod
    def _build_chunk_response(
        model: str,
        delta: Dict[str, Any],
        finish_reason: Optional[str] = None,
        id: str = "",
        usage: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """构建标准 OpenAI chat.completion.chunk 响应信封。"""
        chunk: Dict[str, Any] = {
            "id": id,
            "object": "chat.completion.chunk",
            "created": int(time.time()),
            "model": model,
            "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}],
        }
        if usage:
            chunk["usage"] = usage
        return chunk

    @staticmethod
    def _extract_system_messages(
        messages: List[Dict[str, Any]],
    ) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
        """将 system 消息与非 system 消息分离。

        Returns:
            (system_messages, non_system_messages)
        """
        system: List[Dict[str, Any]] = []
        non_system: List[Dict[str, Any]] = []
        for msg in messages:
            if msg.get("role") == "system":
                system.append(msg)
            else:
                non_system.append(msg)
        return system, non_system

    @staticmethod
    def _iter_tool_call_inputs(
        tool_calls: List[Dict[str, Any]],
    ) -> List[Tuple[str, str, Dict[str, Any]]]:
        """从 OpenAI assistant.tool_calls 中解析出 (id, name, input_dict)。

        封装 arguments 的 JSON 反序列化与异常回退（失败时 input 为 {}），
        各 provider 据此拼装自己的 tool_use / toolUse / functionCall 块。
        """
        result: List[Tuple[str, str, Dict[str, Any]]] = []
        for tc in tool_calls or []:
            func = tc.get("function", {})
            args_str = func.get("arguments", "{}")
            try:
                args_input = json.loads(args_str) if args_str else {}
            except (json.JSONDecodeError, TypeError):
                args_input = {}
            result.append((tc.get("id", ""), func.get("name", ""), args_input))
        return result

    @staticmethod
    def _iter_tool_specs(
        tools: List[Dict[str, Any]],
    ) -> List[Tuple[str, str, Dict[str, Any]]]:
        """从 OpenAI tools 中解析出 (name, description, parameters)。

        兼容两种输入：标准 {"function": {...}} 包裹，或直接的 function 对象。
        """
        specs: List[Tuple[str, str, Dict[str, Any]]] = []
        for tool in tools or []:
            func = tool.get("function", tool)
            specs.append((
                func.get("name", ""),
                func.get("description", ""),
                func.get("parameters", {}),
            ))
        return specs

    _TOOL_CALL_FUNC_FIELDS = ("name", "arguments")

    @staticmethod
    def sanitize_tool_calls(messages: List[Dict[str, Any]]) -> None:
        """清洗 tool_calls，只保留 OpenAI 标准字段：id/type/function.name/function.arguments

        一些 provider（SiliconFlow、InferenceAffinity 等）对 tool_calls 中
        的非标准字段（如 index）敏感，需要原地清洗。

        Args:
            messages: 消息列表（原地修改）
        """
        def _normalize(call: Dict[str, Any]) -> Dict[str, Any]:
            raw_func = call.get("function") or {}
            func = {k: raw_func.get(k, "") for k in BaseProviderAdapter._TOOL_CALL_FUNC_FIELDS}
            return {"id": call.get("id", ""), "type": "function", "function": func}

        for msg in messages:
            if msg.get("role") != "assistant":
                continue
            calls = msg.get("tool_calls")
            if isinstance(calls, list):
                msg["tool_calls"] = [_normalize(c) for c in calls if isinstance(c, dict)]
