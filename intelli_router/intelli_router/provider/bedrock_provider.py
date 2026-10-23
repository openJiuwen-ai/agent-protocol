"""AWS Bedrock provider adapter — Converse API 格式转换 + SigV4 签名。"""
import json
import logging
import re
from typing import AsyncIterator, Dict, List, Optional, Any, Tuple, TYPE_CHECKING

from ..core.deployment import Deployment
from .base_provider import BaseProviderAdapter

if TYPE_CHECKING:
    import httpx

logger = logging.getLogger(__name__)


class BedrockProviderAdapter(BaseProviderAdapter):
    """AWS Bedrock 适配器（Converse API）。

    使用 botocore 进行 SigV4 请求签名和 Event Stream 二进制流解析。
    需要安装可选依赖：pip install intelli-router[bedrock]

    凭证配置：
    - deployment.api_key 格式：ACCESS_KEY_ID:SECRET_ACCESS_KEY[:SESSION_TOKEN]
    - 为空时回退到 botocore 默认凭证链（env / ~/.aws/credentials / IAM role）

    Region 从 deployment.api_base URL 中提取：
    - 格式：https://bedrock-runtime.{region}.amazonaws.com
    """

    SERVICE_NAME = "bedrock"

    # --- URL & Headers ---

    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        base = deployment.api_base.rstrip("/")
        model_id = deployment.model_name
        if stream:
            return f"{base}/model/{model_id}/converse-stream"
        return f"{base}/model/{model_id}/converse"

    def get_headers(self, deployment: Deployment) -> Dict[str, str]:
        return {
            "Content-Type": "application/json",
            "Accept": "application/json",
        }

    # --- SigV4 Signing ---

    def sign_request(
        self,
        method: str,
        url: str,
        headers: Dict[str, str],
        body: bytes,
        deployment: Deployment,
    ) -> Dict[str, str]:
        """使用 botocore SigV4Auth 签名请求。"""
        try:
            from botocore.auth import SigV4Auth
            from botocore.awsrequest import AWSRequest
            from botocore.credentials import Credentials
        except ImportError:
            raise ImportError(
                "botocore is required for AWS Bedrock provider. "
                "Install with: pip install intelli-router[bedrock]"
            )

        access_key, secret_key, session_token = self._parse_credentials(deployment)
        region = self._extract_region(deployment)

        credentials = Credentials(access_key, secret_key, session_token)
        aws_request = AWSRequest(method=method, url=url, data=body, headers=headers)
        SigV4Auth(credentials, self.SERVICE_NAME, region).add_auth(aws_request)

        return dict(aws_request.headers)

    # --- Request Transform ---

    def transform_request(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        deployment: Deployment,
        **kwargs,
    ) -> Dict[str, Any]:
        """将 OpenAI 格式转换为 Bedrock Converse API 请求体。"""
        kwargs.pop("stream", None)

        system_msgs, non_system = self._extract_system_messages(messages)

        body: Dict[str, Any] = {}

        # System
        if system_msgs:
            body["system"] = [
                {"text": msg.get("content", "")} for msg in system_msgs
            ]

        # Messages
        body["messages"] = self._convert_messages(non_system)

        # InferenceConfig
        inference_config: Dict[str, Any] = {}
        if "max_tokens" in kwargs:
            inference_config["maxTokens"] = kwargs.pop("max_tokens")
        if "temperature" in kwargs:
            inference_config["temperature"] = kwargs.pop("temperature")
        if "top_p" in kwargs:
            inference_config["topP"] = kwargs.pop("top_p")
        if "stop" in kwargs:
            stop = kwargs.pop("stop")
            inference_config["stopSequences"] = [stop] if isinstance(stop, str) else stop
        if inference_config:
            body["inferenceConfig"] = inference_config

        # Tools
        tools = kwargs.pop("tools", None)
        tool_choice = kwargs.pop("tool_choice", None)
        if tools:
            body["toolConfig"] = self._convert_tool_config(tools, tool_choice)

        return body

    # --- Response Transform ---

    def transform_response(
        self,
        raw_response: Dict[str, Any],
        model: str,
        deployment: Deployment,
    ) -> Dict[str, Any]:
        """将 Bedrock Converse 响应转换为 OpenAI 统一格式。"""
        output = raw_response.get("output", {})
        message = output.get("message", {})
        content_blocks = message.get("content", [])

        text_parts: List[str] = []
        tool_calls: List[Dict[str, Any]] = []

        for block in content_blocks:
            if "text" in block:
                text_parts.append(block["text"])
            elif "toolUse" in block:
                tu = block["toolUse"]
                tool_calls.append({
                    "id": tu.get("toolUseId", ""),
                    "type": "function",
                    "function": {
                        "name": tu.get("name", ""),
                        "arguments": json.dumps(tu.get("input", {})),
                    },
                })

        stop_reason = raw_response.get("stopReason", "end_turn")
        finish_reason = self.ANTHROPIC_STOP_REASON_MAP.get(stop_reason, "stop")

        msg: Dict[str, Any] = {
            "role": "assistant",
            "content": "".join(text_parts) if text_parts else None,
        }
        if tool_calls:
            msg["tool_calls"] = tool_calls

        usage = raw_response.get("usage", {})
        return self._build_completion_response(
            id=raw_response.get("metrics", {}).get("requestId", f"bedrock-{model}"),
            model=model,
            message=msg,
            finish_reason=finish_reason,
            prompt_tokens=usage.get("inputTokens", 0),
            completion_tokens=usage.get("outputTokens", 0),
        )

    # --- Streaming ---

    async def iter_stream_events(
        self,
        response: "httpx.Response",
    ) -> AsyncIterator[Dict[str, Any]]:
        """解析 Bedrock 二进制 Event Stream 格式。

        使用 botocore 的 EventStreamBuffer 解析 application/vnd.amazon.eventstream。
        """
        try:
            from botocore.eventstream import EventStreamBuffer
        except ImportError:
            raise ImportError(
                "botocore is required for AWS Bedrock streaming. "
                "Install with: pip install intelli-router[bedrock]"
            )

        buffer = EventStreamBuffer()
        async for raw_bytes in response.aiter_bytes():
            buffer.add_data(raw_bytes)
            for event in buffer:
                # event.headers 是 list of (name, value) tuples
                headers_dict = {h[0]: h[1] for h in event.headers}
                event_type = headers_dict.get(":event-type", "")
                if event_type and event.payload:
                    try:
                        payload = json.loads(event.payload)
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue
                    payload["__event_type"] = event_type
                    yield payload

    def transform_stream_chunk(
        self,
        chunk: Dict[str, Any],
        model: str,
        deployment: Deployment,
    ) -> Optional[Dict[str, Any]]:
        """将 Bedrock 流式事件转换为 OpenAI chunk 格式。"""
        event_type = chunk.pop("__event_type", "")

        if event_type == "messageStart":
            role = chunk.get("role", "assistant")
            return self._build_chunk_response(model=model, delta={"role": role})

        elif event_type == "contentBlockStart":
            idx = chunk.get("contentBlockIndex", 0)
            start = chunk.get("start", {})
            if "toolUse" in start:
                tu = start["toolUse"]
                return self._build_chunk_response(
                    model=model,
                    delta={
                        "tool_calls": [{
                            "index": idx,
                            "id": tu.get("toolUseId", ""),
                            "type": "function",
                            "function": {
                                "name": tu.get("name", ""),
                                "arguments": "",
                            },
                        }]
                    },
                )
            return None

        elif event_type == "contentBlockDelta":
            idx = chunk.get("contentBlockIndex", 0)
            delta_block = chunk.get("delta", {})
            if "text" in delta_block:
                return self._build_chunk_response(
                    model=model,
                    delta={"content": delta_block["text"]},
                )
            elif "toolUse" in delta_block:
                return self._build_chunk_response(
                    model=model,
                    delta={
                        "tool_calls": [{
                            "index": idx,
                            "function": {
                                "arguments": delta_block["toolUse"].get("input", ""),
                            },
                        }]
                    },
                )

        elif event_type == "messageStop":
            stop_reason = chunk.get("stopReason", "end_turn")
            return self._build_chunk_response(
                model=model,
                delta={},
                finish_reason=self.ANTHROPIC_STOP_REASON_MAP.get(stop_reason, "stop"),
            )

        elif event_type == "metadata":
            usage = chunk.get("usage", {})
            if usage:
                return self._build_chunk_response(
                    model=model,
                    delta={},
                    usage={
                        "prompt_tokens": usage.get("inputTokens", 0),
                        "completion_tokens": usage.get("outputTokens", 0),
                        "total_tokens": usage.get("totalTokens", 0),
                    },
                )

        # contentBlockStop, unknown events → skip
        return None

    # --- Private Helpers ---

    @staticmethod
    def _parse_credentials(deployment: Deployment) -> Tuple[str, str, Optional[str]]:
        """解析 AWS 凭证。

        格式：ACCESS_KEY_ID:SECRET_ACCESS_KEY[:SESSION_TOKEN]
        为空时回退到 botocore 默认凭证链。
        """
        api_key = deployment.api_key
        if not api_key:
            try:
                import botocore.session
            except ImportError:
                raise ImportError(
                    "botocore is required for AWS Bedrock provider. "
                    "Install with: pip install intelli-router[bedrock]"
                )
            session = botocore.session.get_session()
            credentials = session.get_credentials()
            if credentials is None:
                raise ValueError(
                    "No AWS credentials found. Set deployment.api_key as "
                    "'ACCESS_KEY_ID:SECRET_ACCESS_KEY[:SESSION_TOKEN]' or "
                    "configure AWS credentials via environment/config."
                )
            resolved = credentials.get_frozen_credentials()
            return resolved.access_key, resolved.secret_key, resolved.token

        parts = api_key.split(":")
        if len(parts) < 2:
            raise ValueError(
                "Invalid api_key format for aws-bedrock. Expected: "
                "'ACCESS_KEY_ID:SECRET_ACCESS_KEY[:SESSION_TOKEN]'"
            )
        access_key = parts[0]
        secret_key = parts[1]
        session_token = parts[2] if len(parts) > 2 else None
        return access_key, secret_key, session_token

    @staticmethod
    def _extract_region(deployment: Deployment) -> str:
        """从 api_base URL 中提取 AWS region。

        Expected: https://bedrock-runtime.{region}.amazonaws.com
        """
        match = re.search(
            r"bedrock-runtime\.([a-z0-9-]+)\.", deployment.api_base
        )
        if match:
            return match.group(1)
        match = re.search(r"\.([a-z0-9-]+)\.amazonaws\.com", deployment.api_base)
        if match:
            return match.group(1)
        raise ValueError(
            f"Cannot extract AWS region from api_base: '{deployment.api_base}'. "
            f"Expected format: https://bedrock-runtime.{{region}}.amazonaws.com"
        )

    @staticmethod
    def _convert_messages(messages: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """将 OpenAI 消息格式转换为 Bedrock Converse 消息格式。

        Bedrock 要求相邻消息交替 user/assistant 角色。tool 结果归入 user 角色。
        """
        result: List[Dict[str, Any]] = []

        for msg in messages:
            role = msg.get("role", "user")
            content = msg.get("content", "")

            if role == "tool":
                tool_result_block = {
                    "toolResult": {
                        "toolUseId": msg.get("tool_call_id", ""),
                        "content": [{"text": content if isinstance(content, str) else json.dumps(content)}],
                    }
                }
                if result and result[-1]["role"] == "user":
                    result[-1]["content"].append(tool_result_block)
                else:
                    result.append({"role": "user", "content": [tool_result_block]})

            elif role == "assistant":
                blocks: List[Dict[str, Any]] = []
                if content:
                    text = content if isinstance(content, str) else str(content)
                    blocks.append({"text": text})
                for tc_id, tc_name, tc_input in BedrockProviderAdapter._iter_tool_call_inputs(
                    msg.get("tool_calls", [])
                ):
                    blocks.append({
                        "toolUse": {
                            "toolUseId": tc_id,
                            "name": tc_name,
                            "input": tc_input,
                        }
                    })
                if not blocks:
                    blocks.append({"text": ""})
                result.append({"role": "assistant", "content": blocks})

            else:
                # user message
                blocks = BedrockProviderAdapter._convert_user_content(content)
                if result and result[-1]["role"] == "user":
                    result[-1]["content"].extend(blocks)
                else:
                    result.append({"role": "user", "content": blocks})

        return result

    @staticmethod
    def _convert_user_content(content: Any) -> List[Dict[str, Any]]:
        """将 user message 的 content 转为 Bedrock content blocks。"""
        if isinstance(content, str):
            return [{"text": content}]
        elif isinstance(content, list):
            blocks: List[Dict[str, Any]] = []
            for block in content:
                if not isinstance(block, dict):
                    blocks.append({"text": str(block)})
                    continue
                block_type = block.get("type", "")
                if block_type == "text":
                    blocks.append({"text": block.get("text", "")})
                elif block_type == "image_url":
                    image_url = block.get("image_url", {})
                    url = image_url.get("url", "") if isinstance(image_url, dict) else ""
                    if url.startswith("data:"):
                        match = re.match(r"data:image/(\w+);base64,(.+)", url, re.DOTALL)
                        if match:
                            fmt = match.group(1)
                            data = match.group(2)
                            blocks.append({
                                "image": {
                                    "format": fmt,
                                    "source": {"bytes": data},
                                }
                            })
                    else:
                        logger.warning(
                            "Bedrock Converse API does not support image URLs directly. "
                            "Use base64-encoded images. Skipping: %s", url[:80]
                        )
                else:
                    text = block.get("text", str(block))
                    blocks.append({"text": text})
            return blocks if blocks else [{"text": ""}]
        else:
            return [{"text": str(content)}]

    @staticmethod
    def _convert_tool_config(
        tools: List[Dict[str, Any]],
        tool_choice: Any = None,
    ) -> Dict[str, Any]:
        """将 OpenAI tools 格式转换为 Bedrock toolConfig。"""
        bedrock_tools: List[Dict[str, Any]] = []
        for name, description, parameters in BedrockProviderAdapter._iter_tool_specs(tools):
            bedrock_tools.append({
                "toolSpec": {
                    "name": name,
                    "description": description,
                    "inputSchema": {
                        "json": parameters,
                    },
                }
            })

        config: Dict[str, Any] = {"tools": bedrock_tools}

        if tool_choice:
            config["toolChoice"] = BedrockProviderAdapter._convert_tool_choice(tool_choice)

        return config

    @staticmethod
    def _convert_tool_choice(tool_choice: Any) -> Dict[str, Any]:
        """将 OpenAI tool_choice 转换为 Bedrock toolChoice。"""
        if isinstance(tool_choice, str):
            if tool_choice == "none":
                return {"auto": {}}
            elif tool_choice in ("required", "any"):
                return {"any": {}}
            else:
                return {"auto": {}}
        elif isinstance(tool_choice, dict):
            tc_type = tool_choice.get("type", "")
            if tc_type == "function":
                func = tool_choice.get("function", {})
                return {"tool": {"name": func.get("name", "")}}
            elif tc_type in ("any", "required"):
                return {"any": {}}
        return {"auto": {}}
