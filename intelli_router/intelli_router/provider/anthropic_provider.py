"""Anthropic provider adapter — 消息/工具/Streaming 格式转换。"""
import json
import logging
from typing import Dict, List, Optional, Any

from ..core.deployment import Deployment
from ..utils.media import is_data_uri, parse_data_uri
from .base_provider import BaseProviderAdapter

logger = logging.getLogger(__name__)


class AnthropicProviderAdapter(BaseProviderAdapter):

    ANTHROPIC_VERSION = "2023-06-01"
    DEFAULT_MAX_TOKENS = 4096

    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        base = deployment.api_base.rstrip("/")
        return f"{base}/v1/messages"

    def get_headers(self, deployment: Deployment) -> Dict[str, str]:
        return {
            "x-api-key": deployment.api_key,
            "anthropic-version": self.ANTHROPIC_VERSION,
            "Content-Type": "application/json",
        }

    def transform_request(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        deployment: Deployment,
        **kwargs,
    ) -> Dict[str, Any]:
        stream = kwargs.pop("stream", False)
        system_blocks, non_system = self._extract_system(messages)
        anthropic_messages = self._convert_messages(non_system)

        body: Dict[str, Any] = {
            "model": model,
            "messages": anthropic_messages,
        }

        if stream:
            body["stream"] = True

        body["max_tokens"] = kwargs.pop("max_tokens", self.DEFAULT_MAX_TOKENS)

        tools = kwargs.pop("tools", None)
        if tools:
            body["tools"] = self._convert_tools(tools)
        tool_choice = kwargs.pop("tool_choice", None)
        if tool_choice:
            body["tool_choice"] = self._convert_tool_choice(tool_choice)

        for k, v in kwargs.items():
            if k in ("temperature", "top_p", "top_k"):
                body[k] = v
            elif k == "stop":
                body["stop_sequences"] = [v] if isinstance(v, str) else v
            elif k == "metadata":
                body["metadata"] = v

        if system_blocks:
            body["system"] = system_blocks

        return body

    def transform_response(
        self,
        raw_response: Dict[str, Any],
        model: str,
        deployment: Deployment,
    ) -> Dict[str, Any]:
        content = []
        tool_calls = []
        for block in raw_response.get("content", []):
            if block.get("type") == "text":
                content.append(block.get("text", ""))
            elif block.get("type") == "tool_use":
                tool_calls.append({
                    "id": block["id"],
                    "type": "function",
                    "function": {
                        "name": block["name"],
                        "arguments": json.dumps(block.get("input", {})),
                    },
                })

        message: Dict[str, Any] = {
            "role": "assistant",
            "content": "".join(content) if content else None,
        }
        if tool_calls:
            message["tool_calls"] = tool_calls

        stop_reason = raw_response.get("stop_reason")
        finish_reason = self.ANTHROPIC_STOP_REASON_MAP.get(stop_reason, "stop")

        usage = raw_response.get("usage", {})
        pi = usage.get("input_tokens", 0)
        co = usage.get("output_tokens", 0)

        return self._build_completion_response(
            id=raw_response.get("id", ""),
            model=model,
            message=message,
            finish_reason=finish_reason,
            prompt_tokens=pi,
            completion_tokens=co,
        )

    def transform_stream_chunk(
        self,
        chunk: Dict[str, Any],
        model: str,
        deployment: Deployment,
    ) -> Optional[Dict[str, Any]]:
        event_type = chunk.get("type")

        if event_type == "message_start":
            msg = chunk.get("message", {})
            return self._build_chunk_response(
                model=model, delta={"role": "assistant"}, id=msg.get("id", ""),
            )
        elif event_type == "content_block_delta":
            block_index = chunk.get("index", 0)
            delta = chunk.get("delta", {})
            if delta.get("type") == "text_delta":
                return self._build_chunk_response(
                    model=model, delta={"content": delta.get("text", "")},
                )
            elif delta.get("type") == "input_json_delta":
                return self._build_chunk_response(
                    model=model,
                    delta={"tool_calls": [{"index": block_index, "function": {"arguments": delta.get("partial_json", "")}}]},
                )
        elif event_type == "content_block_start":
            block_index = chunk.get("index", 0)
            block = chunk.get("content_block", {})
            if block.get("type") == "tool_use":
                return self._build_chunk_response(
                    model=model,
                    delta={"tool_calls": [{
                        "index": block_index, "id": block.get("id", ""),
                        "type": "function",
                        "function": {"name": block.get("name", ""), "arguments": ""},
                    }]},
                )
        elif event_type == "message_delta":
            delta = chunk.get("delta", {})
            stop_reason = delta.get("stop_reason")
            usage = chunk.get("usage", {})
            co = usage.get("output_tokens", 0)
            return self._build_chunk_response(
                model=model,
                delta={},
                finish_reason=self.ANTHROPIC_STOP_REASON_MAP.get(stop_reason, "stop"),
                usage={"completion_tokens": co},
            )
        elif event_type == "ping":
            return None

        return None

    def _extract_system(
        self, messages: List[Dict[str, Any]]
    ) -> tuple[List[Dict[str, str]], List[Dict[str, Any]]]:
        """提取 system 消息，返回 (system_blocks, non_system_messages)。"""
        system_msgs, non_system = self._extract_system_messages(messages)
        system_blocks: List[Dict[str, str]] = []
        for msg in system_msgs:
            content = msg.get("content", "")
            if content:
                system_blocks.append({"type": "text", "text": content})
        return system_blocks, non_system

    @staticmethod
    def _convert_messages(
        messages: List[Dict[str, Any]],
    ) -> List[Dict[str, Any]]:
        """将 OpenAI 消息转换为 Anthropic 消息格式。

        关键点：
        - content 字段必须为 list（不能是纯字符串）
        - tool 结果消息映射为 tool_result content block
        """
        result: List[Dict[str, Any]] = []
        for msg in messages:
            role = msg.get("role", "user")
            content = msg.get("content", "")

            if role == "tool":
                result.append({
                    "role": "user",
                    "content": [{
                        "type": "tool_result",
                        "tool_use_id": msg.get("tool_call_id", ""),
                        "content": content if isinstance(content, str) else json.dumps(content),
                    }],
                })
            elif role == "assistant":
                blocks: List[Dict[str, Any]] = []
                if content:
                    blocks.append({"type": "text", "text": content})
                for tc_id, tc_name, tc_input in AnthropicProviderAdapter._iter_tool_call_inputs(
                    msg.get("tool_calls", [])
                ):
                    blocks.append({
                        "type": "tool_use",
                        "id": tc_id,
                        "name": tc_name,
                        "input": tc_input,
                    })
                # Anthropic requires non-empty content array
                if not blocks:
                    blocks.append({"type": "text", "text": ""})
                result.append({"role": "assistant", "content": blocks})
            else:
                # user 消息
                if isinstance(content, str):
                    result.append({"role": "user", "content": [{"type": "text", "text": content}]})
                elif isinstance(content, list):
                    converted: List[Dict[str, Any]] = []
                    for block in content:
                        converted.extend(
                            AnthropicProviderAdapter._convert_content_block(block)
                        )
                    result.append({"role": "user", "content": converted})
                else:
                    result.append({"role": "user", "content": [{"type": "text", "text": str(content)}]})

        return result

    @staticmethod
    def _convert_content_block(block: Dict[str, Any]) -> List[Dict[str, Any]]:
        """将单个 OpenAI 格式 content block 转换为 Anthropic 格式。

        返回列表：正常情况返回 [converted_block]，不支持的类型返回 []（跳过）。
        """
        if not isinstance(block, dict):
            return [{"type": "text", "text": str(block)}]

        block_type = block.get("type", "")

        # text — 透传
        if block_type == "text":
            return [block]

        # image_url → image
        if block_type == "image_url":
            image_url = block.get("image_url", {})
            url = image_url.get("url", "") if isinstance(image_url, dict) else ""
            if is_data_uri(url):
                mime_type, data = parse_data_uri(url)
                return [{"type": "image", "source": {
                    "type": "base64", "media_type": mime_type, "data": data,
                }}]
            else:
                # HTTPS URL 直接用 url source；HTTP 也传过去（Anthropic 会拒绝）
                if url.startswith("http://"):
                    logger.warning(
                        "Anthropic 仅支持 HTTPS 图片 URL，当前为 HTTP: %s", url[:100]
                    )
                return [{"type": "image", "source": {"type": "url", "url": url}}]

        # file → document
        if block_type == "file":
            file_info = block.get("file", {})
            file_data = file_info.get("file_data", "")
            if file_data and is_data_uri(file_data):
                mime_type, data = parse_data_uri(file_data)
                return [{"type": "document", "source": {
                    "type": "base64", "media_type": mime_type, "data": data,
                }}]
            file_id = file_info.get("file_id", "")
            if file_id:
                if file_id.startswith("http"):
                    return [{"type": "document", "source": {"type": "url", "url": file_id}}]
                return [{"type": "document", "source": {"type": "file", "file_id": file_id}}]
            return []

        # input_audio — Anthropic 不支持
        if block_type == "input_audio":
            logger.warning("Anthropic 不支持音频输入，跳过 input_audio block")
            return []

        # video_url — Anthropic 不支持
        if block_type == "video_url":
            logger.warning("Anthropic 不支持视频输入，跳过 video_url block")
            return []

        # 未知类型 — 透传（兼容调用方直接传入 Anthropic 原生格式）
        return [block]

    @staticmethod
    def _convert_tools(tools: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """OpenAI tools -> Anthropic tools"""
        result = []
        for name, description, parameters in AnthropicProviderAdapter._iter_tool_specs(tools):
            result.append({
                "name": name,
                "description": description,
                "input_schema": parameters,
            })
        return result

    @staticmethod
    def _convert_tool_choice(tool_choice: Any) -> Dict[str, Any]:
        """OpenAI tool_choice -> Anthropic tool_choice"""
        if isinstance(tool_choice, str):
            if tool_choice == "any":
                return {"type": "any"}
            elif tool_choice == "none":
                return {"type": "none"}
            return {"type": "auto"}
        if isinstance(tool_choice, dict):
            tc_type = tool_choice.get("type", "")
            if tc_type == "function":
                func = tool_choice.get("function", {})
                return {"type": "tool", "name": func.get("name", "")}
            if tc_type in ("auto", "any", "none"):
                return {"type": tc_type}
        return {"type": "auto"}
