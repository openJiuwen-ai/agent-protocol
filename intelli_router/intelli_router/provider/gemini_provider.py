"""Gemini provider adapter — 消息/工具/Streaming 格式转换。"""
import json
import uuid
from typing import Dict, List, Optional, Any

from ..core.deployment import Deployment
from ..utils.media import is_data_uri, parse_data_uri
from .base_provider import BaseProviderAdapter


class GeminiProviderAdapter(BaseProviderAdapter):

    FINISH_REASON_MAP = {
        "STOP": "stop",
        "MAX_TOKENS": "length",
        "SAFETY": "content_filter",
        "RECITATION": "content_filter",
        "BLOCKLIST": "content_filter",
        "OTHER": "stop",
        "FINISH_REASON_UNSPECIFIED": "stop",
    }

    TOOL_CHOICE_MODE_MAP = {"auto": "AUTO", "any": "ANY", "none": "NONE"}

    def get_api_url(self, deployment: Deployment, stream: bool = False) -> str:
        base = deployment.api_base.rstrip("/")
        action = "streamGenerateContent" if stream else "generateContent"
        url = f"{base}/v1beta/models/{deployment.model_name}:{action}"
        if stream:
            url += "?alt=sse"
        return url

    def get_headers(self, deployment: Deployment) -> Dict[str, str]:
        return {
            "x-goog-api-key": deployment.api_key,
            "Content-Type": "application/json",
        }

    def transform_request(
        self,
        model: str,
        messages: List[Dict[str, Any]],
        deployment: Deployment,
        **kwargs,
    ) -> Dict[str, Any]:
        system_content, non_system = self._extract_system(messages)
        contents = self._convert_contents(non_system)

        body: Dict[str, Any] = {"contents": contents}
        if system_content:
            body["system_instruction"] = {"parts": [{"text": system_content}]}

        tools = kwargs.pop("tools", None)
        if tools:
            body["tools"] = self._convert_tools(tools)
        tool_choice = kwargs.pop("tool_choice", None)
        if tool_choice:
            body["tool_config"] = self._convert_tool_choice(tool_choice)

        gen_config: Dict[str, Any] = {}
        for k, v in kwargs.items():
            if k == "temperature":
                gen_config["temperature"] = v
            elif k == "max_tokens":
                gen_config["maxOutputTokens"] = v
            elif k == "top_p":
                gen_config["topP"] = v
            elif k == "stop":
                gen_config["stopSequences"] = [v] if isinstance(v, str) else v
            elif k == "top_k":
                gen_config["topK"] = v
            elif k == "seed":
                gen_config["seed"] = v
            elif k == "frequency_penalty":
                gen_config["frequencyPenalty"] = v
            elif k == "presence_penalty":
                gen_config["presencePenalty"] = v
        if gen_config:
            body["generationConfig"] = gen_config

        return body

    def transform_response(
        self,
        raw_response: Dict[str, Any],
        model: str,
        deployment: Deployment,
    ) -> Dict[str, Any]:
        candidate = (raw_response.get("candidates") or [{}])[0]
        content_block = candidate.get("content", {})
        parts = content_block.get("parts", [])
        role = content_block.get("role", "model")

        text_parts: List[str] = []
        tool_calls: List[Dict[str, Any]] = []
        for part in parts:
            if "text" in part:
                text_parts.append(part["text"])
            if "functionCall" in part:
                fc = part["functionCall"]
                tool_calls.append({
                    "id": f"call_{uuid.uuid4().hex[:8]}",
                    "type": "function",
                    "function": {
                        "name": fc.get("name", ""),
                        "arguments": json.dumps(fc.get("args", {})),
                    },
                })

        finish_reason = self.FINISH_REASON_MAP.get(candidate.get("finishReason"), "stop")

        message: Dict[str, Any] = {
            "role": "assistant" if role == "model" else role,
            "content": "".join(text_parts) if text_parts else None,
        }
        if tool_calls:
            message["tool_calls"] = tool_calls

        usage = raw_response.get("usageMetadata", {})
        pi = usage.get("promptTokenCount", 0)
        co = usage.get("candidatesTokenCount", 0)

        return self._build_completion_response(
            id=f"gemini-{model}",
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
        candidates = chunk.get("candidates", [])
        if not candidates:
            return None
        candidate = candidates[0]
        parts = (candidate.get("content") or {}).get("parts", [])
        finish = candidate.get("finishReason")

        text = ""
        tool_calls_delta: List[Dict[str, Any]] = []
        for idx, part in enumerate(parts):
            if "text" in part:
                text += part["text"]
            if "functionCall" in part:
                fc = part["functionCall"]
                tool_calls_delta.append({
                    "index": idx,
                    "id": f"call_{uuid.uuid4().hex[:8]}",
                    "type": "function",
                    "function": {
                        "name": fc.get("name", ""),
                        "arguments": json.dumps(fc.get("args", {})),
                    },
                })

        if not text and not tool_calls_delta and not finish:
            return None

        delta: Dict[str, Any] = {}
        if text:
            delta["content"] = text
        if tool_calls_delta:
            delta["tool_calls"] = tool_calls_delta

        return self._build_chunk_response(
            model=model,
            delta=delta,
            finish_reason=self.FINISH_REASON_MAP.get(finish, None),
        )

    def _extract_system(
        self,
        messages: List[Dict[str, Any]],
    ) -> tuple[Optional[str], List[Dict[str, Any]]]:
        """提取 system 消息，返回 (system_text, non_system_messages)。"""
        system_msgs, non_system = self._extract_system_messages(messages)
        system_texts: List[str] = []
        for msg in system_msgs:
            content = msg.get("content", "")
            if content:
                system_texts.append(content if isinstance(content, str) else json.dumps(content))
        return "\n".join(system_texts) if system_texts else None, non_system

    @staticmethod
    def _convert_contents(
        messages: List[Dict[str, Any]],
    ) -> List[Dict[str, Any]]:
        """转换消息为 Gemini contents[] 格式。"""
        contents: List[Dict[str, Any]] = []
        for msg in messages:
            role = msg.get("role", "user")
            gemini_role = "model" if role == "assistant" else "user"
            content = msg.get("content", "")

            if role == "tool":
                # 工具结果 → functionResponse
                parts = [{
                    "functionResponse": {
                        "name": msg.get("name", "unknown_tool"),
                        "response": {
                            "name": msg.get("name", "unknown_tool"),
                            "content": content if isinstance(content, str) else json.dumps(content),
                        },
                    },
                }]
                contents.append({"role": "user", "parts": parts})
                continue

            if isinstance(content, str):
                parts = [{"text": content}]
            elif isinstance(content, list):
                # Convert OpenAI multipart format to Gemini parts format
                parts = []
                for item in content:
                    if isinstance(item, dict):
                        if item.get("type") == "text":
                            parts.append({"text": item.get("text", "")})
                        elif item.get("type") == "image_url":
                            image_url = item.get("image_url", {})
                            url = image_url.get("url", "") if isinstance(image_url, dict) else ""
                            if is_data_uri(url):
                                mime_type, data = parse_data_uri(url)
                                parts.append({"inline_data": {"mime_type": mime_type, "data": data}})
                            else:
                                parts.append({"file_data": {"mime_type": "image/jpeg", "file_uri": url}})
                        else:
                            # Unknown type, pass text representation
                            parts.append({"text": str(item)})
                    else:
                        parts.append({"text": str(item)})
            else:
                parts = [{"text": str(content)}]

            # 处理 assistant 消息中的 tool_calls
            if role == "assistant":
                for _tc_id, tc_name, tc_args in GeminiProviderAdapter._iter_tool_call_inputs(
                    msg.get("tool_calls", [])
                ):
                    parts.append({
                        "functionCall": {
                            "name": tc_name,
                            "args": tc_args,
                        },
                    })

            # 连续同角色合并
            if contents and contents[-1]["role"] == gemini_role:
                contents[-1]["parts"].extend(parts)
            else:
                contents.append({"role": gemini_role, "parts": parts})

        return contents

    @staticmethod
    def _convert_tools(tools: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """OpenAI tools -> Gemini functionDeclarations"""
        funcs = []
        for name, description, parameters in GeminiProviderAdapter._iter_tool_specs(tools):
            funcs.append({
                "name": name,
                "description": description,
                "parameters": parameters,
            })
        return [{"functionDeclarations": funcs}]

    @classmethod
    def _convert_tool_choice(cls, tool_choice: Any) -> Dict[str, Any]:
        """OpenAI tool_choice -> Gemini tool_config"""
        if isinstance(tool_choice, str):
            return {"functionCallingConfig": {"mode": cls.TOOL_CHOICE_MODE_MAP.get(tool_choice, "AUTO")}}
        if isinstance(tool_choice, dict):
            tc_type = tool_choice.get("type", "auto")
            config: Dict[str, Any] = {
                "functionCallingConfig": {"mode": cls.TOOL_CHOICE_MODE_MAP.get(tc_type, "AUTO")}
            }
            if tc_type == "function":
                func = tool_choice.get("function", {})
                config["functionCallingConfig"]["allowedFunctionNames"] = [func.get("name", "")]
            return config
        return {"functionCallingConfig": {"mode": "AUTO"}}
