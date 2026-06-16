"""Tests for provider adapters (OpenAI / Anthropic / Gemini / Registry)."""
import json
import pytest
from intelli_router.core.deployment import Deployment, DeploymentStatus
from intelli_router.provider.base_provider import BaseProviderAdapter
from intelli_router.provider.openai_provider import OpenAIProviderAdapter
from intelli_router.provider.anthropic_provider import AnthropicProviderAdapter
from intelli_router.provider.gemini_provider import GeminiProviderAdapter
from intelli_router.provider.bedrock_provider import BedrockProviderAdapter
from intelli_router.provider.registry import get_provider_adapter, register_provider


# =========================================================================
# OpenAI Adapter
# =========================================================================


class TestOpenAIProviderAdapter:
    def setup_method(self):
        self.adapter = OpenAIProviderAdapter()
        self.dep = Deployment(
            id="test-openai",
            model_name="gpt-4",
            api_key="sk-test-key",
            api_base="https://api.openai.com",
            provider="openai",
        )

    def test_get_api_url(self):
        assert self.adapter.get_api_url(self.dep) == "https://api.openai.com/v1/chat/completions"

    def test_get_api_url_with_trailing_slash(self):
        dep = Deployment(
            model_name="gpt-4",
            api_key="sk-test",
            api_base="https://api.openai.com/",
        )
        assert self.adapter.get_api_url(dep) == "https://api.openai.com/v1/chat/completions"

    def test_get_headers(self):
        headers = self.adapter.get_headers(self.dep)
        assert headers["Authorization"] == "Bearer sk-test-key"
        assert headers["Content-Type"] == "application/json"

    def test_transform_request_passthrough(self):
        result = self.adapter.transform_request(
            model="gpt-4",
            messages=[{"role": "user", "content": "hi"}],
            deployment=self.dep,
            temperature=0.5,
        )
        assert result["model"] == "gpt-4"
        assert result["messages"] == [{"role": "user", "content": "hi"}]
        assert result["temperature"] == 0.5

    def test_transform_response_passthrough(self):
        raw = {"choices": [{"message": {"content": "hello"}}], "usage": {"completion_tokens": 5}}
        result = self.adapter.transform_response(raw, "gpt-4", self.dep)
        assert result is raw


# =========================================================================
# Anthropic Adapter
# =========================================================================


class TestAnthropicProviderAdapter:
    def setup_method(self):
        self.adapter = AnthropicProviderAdapter()
        self.dep = Deployment(
            id="test-anthropic",
            model_name="claude-3-opus",
            api_key="sk-ant-test",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )

    def test_get_api_url(self):
        assert self.adapter.get_api_url(self.dep) == "https://api.anthropic.com/v1/messages"

    def test_get_headers(self):
        headers = self.adapter.get_headers(self.dep)
        assert headers["x-api-key"] == "sk-ant-test"
        assert headers["anthropic-version"] == "2023-06-01"
        assert headers["Content-Type"] == "application/json"

    def test_transform_request_basic(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Hello"}],
            deployment=self.dep,
        )
        assert result["model"] == "claude-3-opus"
        assert result["messages"] == [
            {"role": "user", "content": [{"type": "text", "text": "Hello"}]}
        ]
        assert result["max_tokens"] == 4096

    def test_transform_request_extracts_system(self):
        result = self.adapter.transform_request(
            model="claude-3-haiku",
            messages=[
                {"role": "system", "content": "You are a helpful assistant."},
                {"role": "user", "content": "Hi"},
            ],
            deployment=self.dep,
        )
        assert result["system"] == [{"type": "text", "text": "You are a helpful assistant."}]
        assert len(result["messages"]) == 1
        assert result["messages"][0]["role"] == "user"

    def test_transform_request_max_tokens_from_kwargs(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            max_tokens=1000,
        )
        assert result["max_tokens"] == 1000

    def test_transform_request_temperature_top_p(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            temperature=0.7,
            top_p=0.9,
        )
        assert result["temperature"] == 0.7
        assert result["top_p"] == 0.9

    def test_transform_request_tools_conversion(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Weather?"}],
            deployment=self.dep,
            tools=[{
                "type": "function",
                "function": {
                    "name": "get_weather",
                    "description": "Get weather",
                    "parameters": {"type": "object", "properties": {"loc": {"type": "string"}}},
                },
            }],
            tool_choice="auto",
        )
        assert len(result["tools"]) == 1
        assert result["tools"][0]["name"] == "get_weather"
        assert result["tools"][0]["input_schema"] == {
            "type": "object", "properties": {"loc": {"type": "string"}}
        }
        assert result["tool_choice"] == {"type": "auto"}

    def test_transform_response(self):
        raw = {
            "id": "msg_123",
            "content": [
                {"type": "text", "text": "Hello"},
            ],
            "stop_reason": "end_turn",
            "usage": {"input_tokens": 10, "output_tokens": 5},
        }
        result = self.adapter.transform_response(raw, "claude-3-opus", self.dep)
        assert result["id"] == "msg_123"
        assert result["object"] == "chat.completion"
        assert result["choices"][0]["message"]["content"] == "Hello"
        assert result["choices"][0]["finish_reason"] == "stop"
        assert result["usage"]["completion_tokens"] == 5
        assert result["usage"]["prompt_tokens"] == 10

    def test_transform_response_with_tool_calls(self):
        raw = {
            "id": "msg_456",
            "content": [
                {"type": "text", "text": "Let me check..."},
                {
                    "type": "tool_use",
                    "id": "tu_1",
                    "name": "get_weather",
                    "input": {"loc": "Beijing"},
                },
            ],
            "stop_reason": "tool_use",
            "usage": {"input_tokens": 20, "output_tokens": 15},
        }
        result = self.adapter.transform_response(raw, "claude-3-opus", self.dep)
        assert len(result["choices"][0]["message"]["tool_calls"]) == 1
        tc = result["choices"][0]["message"]["tool_calls"][0]
        assert tc["id"] == "tu_1"
        assert tc["function"]["name"] == "get_weather"
        assert json.loads(tc["function"]["arguments"]) == {"loc": "Beijing"}
        assert result["choices"][0]["finish_reason"] == "tool_calls"

    def test_transform_stream_chunk_message_start(self):
        chunk = {"type": "message_start", "message": {"id": "msg_1", "model": "claude-3-opus"}}
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is not None
        assert result["choices"][0]["delta"]["role"] == "assistant"

    def test_transform_stream_chunk_text_delta(self):
        chunk = {"type": "content_block_delta", "delta": {"type": "text_delta", "text": "Hello"}}
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is not None
        assert result["choices"][0]["delta"]["content"] == "Hello"

    def test_transform_stream_chunk_ping_is_skipped(self):
        chunk = {"type": "ping"}
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is None

    def test_transform_stream_chunk_message_delta(self):
        chunk = {"type": "message_delta", "delta": {"stop_reason": "end_turn"}, "usage": {"output_tokens": 10}}
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is not None
        assert result["choices"][0]["finish_reason"] == "stop"
        assert result["usage"]["completion_tokens"] == 10

    def test_convert_tools(self):
        tools = [{"function": {"name": "fn1", "description": "desc1", "parameters": {"type": "object"}}}]
        result = AnthropicProviderAdapter._convert_tools(tools)
        assert result[0]["name"] == "fn1"
        assert result[0]["description"] == "desc1"
        assert result[0]["input_schema"] == {"type": "object"}

    def test_convert_tool_choice_auto(self):
        assert AnthropicProviderAdapter._convert_tool_choice("auto") == {"type": "auto"}

    def test_convert_tool_choice_any(self):
        assert AnthropicProviderAdapter._convert_tool_choice("any") == {"type": "any"}

    def test_convert_tool_choice_none(self):
        assert AnthropicProviderAdapter._convert_tool_choice("none") == {"type": "none"}

    def test_convert_messages_with_tool_results(self):
        messages = [
            {"role": "assistant", "content": "Let me check"},
            {"role": "tool", "tool_call_id": "tc_1", "content": "42"},
        ]
        result = AnthropicProviderAdapter._convert_messages(messages)
        assert result[0]["role"] == "assistant"
        assert result[1]["role"] == "user"
        assert result[1]["content"][0]["type"] == "tool_result"
        assert result[1]["content"][0]["tool_use_id"] == "tc_1"


# =========================================================================
# Gemini Adapter
# =========================================================================


class TestGeminiProviderAdapter:
    def setup_method(self):
        self.adapter = GeminiProviderAdapter()
        self.dep = Deployment(
            id="test-gemini",
            model_name="gemini-1.5-pro",
            api_key="google-test-key",
            api_base="https://generativelanguage.googleapis.com",
            provider="google-gemini",
        )

    def test_get_api_url(self):
        url = self.adapter.get_api_url(self.dep)
        assert "generativelanguage.googleapis.com/v1beta/models/" in url
        assert "gemini-1.5-pro" in url
        assert url.endswith(":generateContent")

    def test_get_api_url_stream(self):
        url = self.adapter.get_api_url(self.dep, stream=True)
        assert ":streamGenerateContent" in url
        assert url.endswith("?alt=sse")

    def test_get_headers(self):
        headers = self.adapter.get_headers(self.dep)
        assert headers["x-goog-api-key"] == "google-test-key"
        assert headers["Content-Type"] == "application/json"

    def test_transform_request_basic(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[{"role": "user", "content": "Hello"}],
            deployment=self.dep,
        )
        assert "contents" in result
        assert result["contents"][0]["role"] == "user"
        assert result["contents"][0]["parts"] == [{"text": "Hello"}]

    def test_transform_request_system_instruction(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[
                {"role": "system", "content": "Be helpful."},
                {"role": "user", "content": "Hi"},
            ],
            deployment=self.dep,
        )
        assert result["system_instruction"]["parts"] == [{"text": "Be helpful."}]

    def test_transform_request_role_mapping(self):
        """assistant role -> model role"""
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[
                {"role": "user", "content": "Hi"},
                {"role": "assistant", "content": "Hello there"},
            ],
            deployment=self.dep,
        )
        assert result["contents"][0]["role"] == "user"
        assert result["contents"][1]["role"] == "model"

    def test_transform_request_generation_config(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            temperature=0.8,
            max_tokens=100,
            top_p=0.9,
        )
        gc = result.get("generationConfig", {})
        assert gc["temperature"] == 0.8
        assert gc["maxOutputTokens"] == 100
        assert gc["topP"] == 0.9

    def test_transform_request_tools(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[{"role": "user", "content": "Weather?"}],
            deployment=self.dep,
            tools=[{
                "type": "function",
                "function": {
                    "name": "get_weather",
                    "description": "Get weather",
                    "parameters": {"type": "object"},
                },
            }],
        )
        assert len(result["tools"]) == 1
        assert result["tools"][0]["functionDeclarations"][0]["name"] == "get_weather"

    def test_transform_response(self):
        raw = {
            "candidates": [{
                "content": {"parts": [{"text": "Hello"}], "role": "model"},
                "finishReason": "STOP",
            }],
            "usageMetadata": {"promptTokenCount": 10, "candidatesTokenCount": 5},
        }
        result = self.adapter.transform_response(raw, "gemini-1.5-pro", self.dep)
        assert result["choices"][0]["message"]["content"] == "Hello"
        assert result["choices"][0]["finish_reason"] == "stop"
        assert result["usage"]["completion_tokens"] == 5
        assert result["usage"]["prompt_tokens"] == 10

    def test_transform_response_with_function_call(self):
        raw = {
            "candidates": [{
                "content": {
                    "parts": [{
                        "functionCall": {
                            "name": "get_weather",
                            "args": {"loc": "Beijing"},
                        }
                    }],
                    "role": "model",
                },
                "finishReason": "STOP",
            }],
            "usageMetadata": {"promptTokenCount": 15, "candidatesTokenCount": 8},
        }
        result = self.adapter.transform_response(raw, "gemini-1.5-pro", self.dep)
        tc = result["choices"][0]["message"]["tool_calls"][0]
        assert tc["function"]["name"] == "get_weather"
        assert json.loads(tc["function"]["arguments"]) == {"loc": "Beijing"}
        assert result["choices"][0]["finish_reason"] == "stop"

    def test_transform_stream_chunk_basic(self):
        chunk = {"candidates": [{"content": {"parts": [{"text": "Hello"}], "role": "model"}}]}
        result = self.adapter.transform_stream_chunk(chunk, "gemini-1.5-pro", self.dep)
        assert result is not None
        assert result["choices"][0]["delta"]["content"] == "Hello"

    def test_transform_stream_chunk_no_candidates(self):
        chunk = {"candidates": []}
        result = self.adapter.transform_stream_chunk(chunk, "gemini-1.5-pro", self.dep)
        assert result is None

    def test_transform_stream_chunk_with_finish_reason(self):
        chunk = {
            "candidates": [{
                "content": {"parts": [], "role": "model"},
                "finishReason": "STOP",
            }]
        }
        result = self.adapter.transform_stream_chunk(chunk, "gemini-1.5-pro", self.dep)
        assert result is not None
        assert result["choices"][0]["delta"] == {}
        assert result["choices"][0]["finish_reason"] == "stop"


# =========================================================================
# Registry
# =========================================================================


class TestProviderRegistry:
    def test_get_openai_adapter(self):
        adapter = get_provider_adapter("openai")
        assert isinstance(adapter, OpenAIProviderAdapter)

    def test_get_anthropic_adapter(self):
        adapter = get_provider_adapter("anthropic")
        assert isinstance(adapter, AnthropicProviderAdapter)

    def test_get_gemini_adapter(self):
        adapter = get_provider_adapter("google-gemini")
        assert isinstance(adapter, GeminiProviderAdapter)

    def test_get_bedrock_adapter(self):
        adapter = get_provider_adapter("aws-bedrock")
        assert isinstance(adapter, BedrockProviderAdapter)

    def test_get_unknown_provider_raises(self):
        with pytest.raises(ValueError, match="Unknown provider"):
            get_provider_adapter("nonexistent")

    def test_register_custom_provider(self):
        class FakeAdapter(BaseProviderAdapter):
            def get_api_url(self, dep):
                return "http://fake/url"

            def get_headers(self, dep):
                return {"Authorization": "Bearer fake"}

        register_provider("fake", FakeAdapter)
        adapter = get_provider_adapter("fake")
        assert isinstance(adapter, FakeAdapter)


# =========================================================================
# Bedrock (Converse API)
# =========================================================================


class TestBedrockProviderAdapter:
    def setup_method(self):
        self.adapter = BedrockProviderAdapter()
        self.dep = Deployment(
            id="test-bedrock",
            model_name="anthropic.claude-3-5-sonnet-20241022-v2:0",
            api_key="AKIAIOSFODNN7EXAMPLE:wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
            api_base="https://bedrock-runtime.us-east-1.amazonaws.com",
            provider="aws-bedrock",
        )

    # --- URL ---

    def test_get_api_url_non_stream(self):
        url = self.adapter.get_api_url(self.dep, stream=False)
        assert url == (
            "https://bedrock-runtime.us-east-1.amazonaws.com"
            "/model/anthropic.claude-3-5-sonnet-20241022-v2:0/converse"
        )

    def test_get_api_url_stream(self):
        url = self.adapter.get_api_url(self.dep, stream=True)
        assert url == (
            "https://bedrock-runtime.us-east-1.amazonaws.com"
            "/model/anthropic.claude-3-5-sonnet-20241022-v2:0/converse-stream"
        )

    def test_get_api_url_trailing_slash(self):
        dep = Deployment(
            model_name="model-x",
            api_key="A:B",
            api_base="https://bedrock-runtime.us-west-2.amazonaws.com/",
            provider="aws-bedrock",
        )
        url = self.adapter.get_api_url(dep, stream=False)
        assert "//" not in url.split("://")[1]

    # --- Headers ---

    def test_get_headers(self):
        headers = self.adapter.get_headers(self.dep)
        assert headers["Content-Type"] == "application/json"
        assert headers["Accept"] == "application/json"

    # --- Region Extraction ---

    def test_extract_region(self):
        region = self.adapter._extract_region(self.dep)
        assert region == "us-east-1"

    def test_extract_region_other(self):
        dep = Deployment(
            model_name="m", api_key="A:B",
            api_base="https://bedrock-runtime.ap-northeast-1.amazonaws.com",
            provider="aws-bedrock",
        )
        assert self.adapter._extract_region(dep) == "ap-northeast-1"

    def test_extract_region_invalid_raises(self):
        dep = Deployment(
            model_name="m", api_key="A:B",
            api_base="https://my-custom-proxy.example.com",
            provider="aws-bedrock",
        )
        with pytest.raises(ValueError, match="Cannot extract AWS region"):
            self.adapter._extract_region(dep)

    # --- Credential Parsing ---

    def test_parse_credentials_basic(self):
        ak, sk, token = self.adapter._parse_credentials(self.dep)
        assert ak == "AKIAIOSFODNN7EXAMPLE"
        assert sk == "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY"
        assert token is None

    def test_parse_credentials_with_session_token(self):
        dep = Deployment(
            model_name="m",
            api_key="AKIA:SECRET:SESSION_TOKEN_VALUE",
            api_base="https://bedrock-runtime.us-east-1.amazonaws.com",
            provider="aws-bedrock",
        )
        ak, sk, token = self.adapter._parse_credentials(dep)
        assert ak == "AKIA"
        assert sk == "SECRET"
        assert token == "SESSION_TOKEN_VALUE"

    def test_parse_credentials_invalid_format(self):
        dep = Deployment(
            model_name="m",
            api_key="just-one-part",
            api_base="https://bedrock-runtime.us-east-1.amazonaws.com",
            provider="aws-bedrock",
        )
        with pytest.raises(ValueError, match="Invalid api_key format"):
            self.adapter._parse_credentials(dep)

    # --- transform_request ---

    def test_transform_request_basic(self):
        messages = [
            {"role": "user", "content": "Hello"},
        ]
        result = self.adapter.transform_request(
            model="claude-v2", messages=messages, deployment=self.dep,
        )
        assert result["messages"] == [
            {"role": "user", "content": [{"text": "Hello"}]}
        ]
        assert "system" not in result

    def test_transform_request_with_system(self):
        messages = [
            {"role": "system", "content": "You are helpful."},
            {"role": "user", "content": "Hi"},
        ]
        result = self.adapter.transform_request(
            model="claude-v2", messages=messages, deployment=self.dep,
        )
        assert result["system"] == [{"text": "You are helpful."}]
        assert len(result["messages"]) == 1
        assert result["messages"][0]["role"] == "user"

    def test_transform_request_inference_config(self):
        messages = [{"role": "user", "content": "Hi"}]
        result = self.adapter.transform_request(
            model="claude-v2", messages=messages, deployment=self.dep,
            max_tokens=1024, temperature=0.7, top_p=0.9, stop=["END"],
        )
        cfg = result["inferenceConfig"]
        assert cfg["maxTokens"] == 1024
        assert cfg["temperature"] == 0.7
        assert cfg["topP"] == 0.9
        assert cfg["stopSequences"] == ["END"]

    def test_transform_request_with_tools(self):
        messages = [{"role": "user", "content": "What's the weather?"}]
        tools = [{
            "type": "function",
            "function": {
                "name": "get_weather",
                "description": "Get weather info",
                "parameters": {"type": "object", "properties": {"city": {"type": "string"}}},
            }
        }]
        result = self.adapter.transform_request(
            model="claude-v2", messages=messages, deployment=self.dep,
            tools=tools, tool_choice="auto",
        )
        assert "toolConfig" in result
        tool_spec = result["toolConfig"]["tools"][0]["toolSpec"]
        assert tool_spec["name"] == "get_weather"
        assert tool_spec["description"] == "Get weather info"
        assert tool_spec["inputSchema"]["json"] == {"type": "object", "properties": {"city": {"type": "string"}}}
        assert result["toolConfig"]["toolChoice"] == {"auto": {}}

    def test_transform_request_tool_choice_required(self):
        messages = [{"role": "user", "content": "Hi"}]
        tools = [{"type": "function", "function": {"name": "f", "parameters": {}}}]
        result = self.adapter.transform_request(
            model="m", messages=messages, deployment=self.dep,
            tools=tools, tool_choice="required",
        )
        assert result["toolConfig"]["toolChoice"] == {"any": {}}

    def test_transform_request_tool_choice_specific(self):
        messages = [{"role": "user", "content": "Hi"}]
        tools = [{"type": "function", "function": {"name": "my_func", "parameters": {}}}]
        result = self.adapter.transform_request(
            model="m", messages=messages, deployment=self.dep,
            tools=tools, tool_choice={"type": "function", "function": {"name": "my_func"}},
        )
        assert result["toolConfig"]["toolChoice"] == {"tool": {"name": "my_func"}}

    def test_transform_request_multi_turn_with_tool_calls(self):
        messages = [
            {"role": "user", "content": "What's the weather in Tokyo?"},
            {
                "role": "assistant",
                "content": None,
                "tool_calls": [{
                    "id": "tc_1",
                    "type": "function",
                    "function": {"name": "get_weather", "arguments": '{"city": "Tokyo"}'},
                }],
            },
            {"role": "tool", "tool_call_id": "tc_1", "content": "Sunny, 25C"},
        ]
        result = self.adapter.transform_request(
            model="claude-v2", messages=messages, deployment=self.dep,
        )
        bedrock_msgs = result["messages"]
        assert len(bedrock_msgs) == 3
        # assistant message has toolUse
        assert "toolUse" in bedrock_msgs[1]["content"][0]
        assert bedrock_msgs[1]["content"][0]["toolUse"]["name"] == "get_weather"
        # tool result is in user message
        assert bedrock_msgs[2]["role"] == "user"
        assert "toolResult" in bedrock_msgs[2]["content"][0]
        assert bedrock_msgs[2]["content"][0]["toolResult"]["toolUseId"] == "tc_1"

    def test_transform_request_stream_kwarg_removed(self):
        messages = [{"role": "user", "content": "Hi"}]
        result = self.adapter.transform_request(
            model="m", messages=messages, deployment=self.dep, stream=True,
        )
        assert "stream" not in result

    # --- transform_response ---

    def test_transform_response_text_only(self):
        raw = {
            "output": {
                "message": {
                    "role": "assistant",
                    "content": [{"text": "Hello there!"}],
                }
            },
            "stopReason": "end_turn",
            "usage": {"inputTokens": 10, "outputTokens": 5, "totalTokens": 15},
        }
        result = self.adapter.transform_response(raw, "claude-v2", self.dep)
        assert result["object"] == "chat.completion"
        assert result["choices"][0]["message"]["content"] == "Hello there!"
        assert result["choices"][0]["finish_reason"] == "stop"
        assert result["usage"]["prompt_tokens"] == 10
        assert result["usage"]["completion_tokens"] == 5

    def test_transform_response_tool_use(self):
        raw = {
            "output": {
                "message": {
                    "role": "assistant",
                    "content": [
                        {"toolUse": {"toolUseId": "tu_1", "name": "get_weather", "input": {"city": "Tokyo"}}},
                    ],
                }
            },
            "stopReason": "tool_use",
            "usage": {"inputTokens": 20, "outputTokens": 10, "totalTokens": 30},
        }
        result = self.adapter.transform_response(raw, "claude-v2", self.dep)
        assert result["choices"][0]["finish_reason"] == "tool_calls"
        tc = result["choices"][0]["message"]["tool_calls"][0]
        assert tc["id"] == "tu_1"
        assert tc["function"]["name"] == "get_weather"
        assert tc["function"]["arguments"] == '{"city": "Tokyo"}'

    def test_transform_response_max_tokens(self):
        raw = {
            "output": {"message": {"role": "assistant", "content": [{"text": "partial"}]}},
            "stopReason": "max_tokens",
            "usage": {"inputTokens": 5, "outputTokens": 100, "totalTokens": 105},
        }
        result = self.adapter.transform_response(raw, "m", self.dep)
        assert result["choices"][0]["finish_reason"] == "length"

    # --- transform_stream_chunk ---

    def test_stream_chunk_message_start(self):
        chunk = {"__event_type": "messageStart", "role": "assistant"}
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        assert result["choices"][0]["delta"] == {"role": "assistant"}

    def test_stream_chunk_content_block_start_tool(self):
        chunk = {
            "__event_type": "contentBlockStart",
            "contentBlockIndex": 0,
            "start": {"toolUse": {"toolUseId": "tu_1", "name": "search"}},
        }
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        tc = result["choices"][0]["delta"]["tool_calls"][0]
        assert tc["id"] == "tu_1"
        assert tc["function"]["name"] == "search"

    def test_stream_chunk_content_block_start_text(self):
        chunk = {"__event_type": "contentBlockStart", "contentBlockIndex": 0, "start": {}}
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        assert result is None

    def test_stream_chunk_content_block_delta_text(self):
        chunk = {
            "__event_type": "contentBlockDelta",
            "contentBlockIndex": 0,
            "delta": {"text": "Hello"},
        }
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        assert result["choices"][0]["delta"]["content"] == "Hello"

    def test_stream_chunk_content_block_delta_tool(self):
        chunk = {
            "__event_type": "contentBlockDelta",
            "contentBlockIndex": 1,
            "delta": {"toolUse": {"input": '{"key": "val'}},
        }
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        tc = result["choices"][0]["delta"]["tool_calls"][0]
        assert tc["index"] == 1
        assert tc["function"]["arguments"] == '{"key": "val'

    def test_stream_chunk_message_stop(self):
        chunk = {"__event_type": "messageStop", "stopReason": "end_turn"}
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        assert result["choices"][0]["finish_reason"] == "stop"

    def test_stream_chunk_message_stop_tool_use(self):
        chunk = {"__event_type": "messageStop", "stopReason": "tool_use"}
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        assert result["choices"][0]["finish_reason"] == "tool_calls"

    def test_stream_chunk_metadata(self):
        chunk = {
            "__event_type": "metadata",
            "usage": {"inputTokens": 10, "outputTokens": 20, "totalTokens": 30},
        }
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        assert result["usage"]["prompt_tokens"] == 10
        assert result["usage"]["completion_tokens"] == 20

    def test_stream_chunk_content_block_stop_skipped(self):
        chunk = {"__event_type": "contentBlockStop", "contentBlockIndex": 0}
        result = self.adapter.transform_stream_chunk(chunk, "m", self.dep)
        assert result is None

    # --- sign_request (with mocked botocore) ---

    def test_sign_request(self):
        """Test sign_request calls SigV4Auth correctly (requires botocore)."""
        pytest.importorskip("botocore")

        headers = {"Content-Type": "application/json"}
        body = b'{"messages": []}'
        url = "https://bedrock-runtime.us-east-1.amazonaws.com/model/m/converse"

        signed_headers = self.adapter.sign_request("POST", url, headers, body, self.dep)
        # SigV4 should add Authorization header
        assert "Authorization" in signed_headers
        assert "AWS4-HMAC-SHA256" in signed_headers["Authorization"]
        # Should contain X-Amz-Date
        assert "X-Amz-Date" in signed_headers


# =========================================================================
# Deployment defaults
# =========================================================================


class TestDeploymentDefaults:
    def test_default_provider_is_openai(self):
        dep = Deployment(model_name="gpt-4", api_key="sk-test", api_base="https://test.com")
        assert dep.provider == "openai"


# =========================================================================
# BaseProviderAdapter defaults
# =========================================================================


class FakeDeployment:
    """Minimal Deployment-like object for base adapter tests."""
    def __init__(self):
        self.model_name = "test-model"
        self.api_base = "https://test.com"
        self.api_key = "test-key"
        self.provider = "test-provider"


class ConcreteAdapter(BaseProviderAdapter):
    """Concrete subclass for testing BaseProviderAdapter default behavior."""
    def get_api_url(self, deployment):
        return "http://test/url"
    def get_headers(self, deployment):
        return {"Authorization": "Bearer test"}


class TestBaseProviderAdapter:
    def setup_method(self):
        self.adapter = ConcreteAdapter()
        self.dep = FakeDeployment()

    def test_transform_request_default(self):
        result = self.adapter.transform_request(
            model="m1", messages=[{"role": "user", "content": "hi"}],
            deployment=self.dep, temperature=0.5,
        )
        assert result == {"model": "m1", "messages": [{"role": "user", "content": "hi"}], "temperature": 0.5}

    def test_transform_response_default(self):
        raw = {"id": "123"}
        result = self.adapter.transform_response(raw, "m1", self.dep)
        assert result is raw

    def test_transform_stream_chunk_default(self):
        chunk = {"choices": [{"delta": {"content": "hello"}}]}
        result = self.adapter.transform_stream_chunk(chunk, "m1", self.dep)
        assert result is chunk


# =========================================================================
# Anthropic — stream chunk edge cases
# =========================================================================


class TestAnthropicStreamChunk:
    def setup_method(self):
        self.adapter = AnthropicProviderAdapter()
        self.dep = Deployment(
            id="test-anthropic",
            model_name="claude-3-opus",
            api_key="sk-ant-test",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )

    def test_content_block_start_tool_use(self):
        chunk = {
            "type": "content_block_start",
            "content_block": {
                "type": "tool_use",
                "id": "tu_1",
                "name": "get_weather",
            },
        }
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is not None
        tc = result["choices"][0]["delta"]["tool_calls"][0]
        assert tc["id"] == "tu_1"
        assert tc["function"]["name"] == "get_weather"
        assert tc["function"]["arguments"] == ""

    def test_content_block_start_non_tool_is_skipped(self):
        chunk = {
            "type": "content_block_start",
            "content_block": {"type": "text", "text": "Hello"},
        }
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is None

    def test_input_json_delta(self):
        chunk = {
            "type": "content_block_delta",
            "delta": {"type": "input_json_delta", "partial_json": '{"loc": "Bei'},
        }
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is not None
        tc = result["choices"][0]["delta"]["tool_calls"][0]
        assert tc["function"]["arguments"] == '{"loc": "Bei'

    def test_content_block_delta_unknown_type_is_skipped(self):
        chunk = {
            "type": "content_block_delta",
            "delta": {"type": "unknown_delta"},
        }
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is None

    def test_message_delta_max_tokens(self):
        chunk = {
            "type": "message_delta",
            "delta": {"stop_reason": "max_tokens"},
            "usage": {"output_tokens": 50},
        }
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is not None
        assert result["choices"][0]["finish_reason"] == "length"
        assert result["usage"]["completion_tokens"] == 50

    def test_unknown_event_returns_none(self):
        chunk = {"type": "unknown_event"}
        result = self.adapter.transform_stream_chunk(chunk, "claude-3-opus", self.dep)
        assert result is None


# =========================================================================
# Anthropic — tool choice edge cases
# =========================================================================


class TestAnthropicToolChoice:
    def test_convert_tool_choice_function_dict(self):
        result = AnthropicProviderAdapter._convert_tool_choice(
            {"type": "function", "function": {"name": "get_weather"}}
        )
        assert result == {"type": "tool", "name": "get_weather"}

    def test_convert_tool_choice_auto_dict(self):
        result = AnthropicProviderAdapter._convert_tool_choice({"type": "auto"})
        assert result == {"type": "auto"}

    def test_convert_tool_choice_any_dict(self):
        result = AnthropicProviderAdapter._convert_tool_choice({"type": "any"})
        assert result == {"type": "any"}

    def test_convert_tool_choice_none_dict(self):
        result = AnthropicProviderAdapter._convert_tool_choice({"type": "none"})
        assert result == {"type": "none"}

    def test_convert_tool_choice_unknown_type_fallback_auto(self):
        result = AnthropicProviderAdapter._convert_tool_choice({"type": "something_else"})
        assert result == {"type": "auto"}


# =========================================================================
# Anthropic — convert_messages edge cases
# =========================================================================


class TestAnthropicConvertMessages:
    def test_user_message_list_content(self):
        messages = [{"role": "user", "content": [{"type": "text", "text": "Hello"}]}]
        result = AnthropicProviderAdapter._convert_messages(messages)
        assert result[0]["role"] == "user"
        assert result[0]["content"] == [{"type": "text", "text": "Hello"}]

    def test_user_message_non_string_content(self):
        messages = [{"role": "user", "content": 123}]
        result = AnthropicProviderAdapter._convert_messages(messages)
        assert result[0]["role"] == "user"
        assert result[0]["content"][0]["text"] == "123"

    def test_assistant_with_tool_calls_only_no_text(self):
        messages = [{
            "role": "assistant",
            "tool_calls": [{
                "id": "tc_1",
                "function": {"name": "fn1", "arguments": '{"x": 1}'},
            }],
        }]
        result = AnthropicProviderAdapter._convert_messages(messages)
        assert result[0]["role"] == "assistant"
        assert len(result[0]["content"]) == 1
        assert result[0]["content"][0]["type"] == "tool_use"
        assert result[0]["content"][0]["input"] == {"x": 1}

    def test_tool_result_with_non_string_content(self):
        messages = [
            {"role": "assistant", "content": "Check", "tool_calls": [{"id": "tc_1", "function": {"name": "fn1", "arguments": "{}"}}]},
            {"role": "tool", "tool_call_id": "tc_1", "content": {"result": 42}},
        ]
        result = AnthropicProviderAdapter._convert_messages(messages)
        tool_result = result[1]["content"][0]
        assert tool_result["type"] == "tool_result"
        assert json.loads(tool_result["content"]) == {"result": 42}


# =========================================================================
# Anthropic — response finish_reason edge cases
# =========================================================================


class TestAnthropicResponseFinishReasons:
    def setup_method(self):
        self.adapter = AnthropicProviderAdapter()
        self.dep = Deployment(
            id="test-anthropic",
            model_name="claude-3-opus",
            api_key="sk-ant-test",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )

    def _make_response(self, stop_reason: str) -> dict:
        return {
            "id": "msg_1",
            "content": [{"type": "text", "text": "Hello"}],
            "stop_reason": stop_reason,
            "usage": {"input_tokens": 5, "output_tokens": 5},
        }

    def test_stop_reason_max_tokens(self):
        result = self.adapter.transform_response(self._make_response("max_tokens"), "claude-3-opus", self.dep)
        assert result["choices"][0]["finish_reason"] == "length"

    def test_stop_reason_stop_sequence(self):
        result = self.adapter.transform_response(self._make_response("stop_sequence"), "claude-3-opus", self.dep)
        assert result["choices"][0]["finish_reason"] == "stop"

    def test_stop_reason_unknown_defaults_to_stop(self):
        result = self.adapter.transform_response(self._make_response("unknown_reason"), "claude-3-opus", self.dep)
        assert result["choices"][0]["finish_reason"] == "stop"

    def test_response_no_content(self):
        raw = {
            "id": "msg_empty",
            "content": [],
            "stop_reason": "end_turn",
            "usage": {"input_tokens": 5, "output_tokens": 0},
        }
        result = self.adapter.transform_response(raw, "claude-3-opus", self.dep)
        assert result["choices"][0]["message"]["content"] is None


# =========================================================================
# Gemini — tool_choice edge cases
# =========================================================================


class TestGeminiToolChoice:
    def test_convert_tool_choice_function_dict(self):
        result = GeminiProviderAdapter._convert_tool_choice(
            {"type": "function", "function": {"name": "get_weather"}}
        )
        assert result["functionCallingConfig"]["mode"] == "AUTO"
        assert result["functionCallingConfig"]["allowedFunctionNames"] == ["get_weather"]

    def test_convert_tool_choice_none_string(self):
        result = GeminiProviderAdapter._convert_tool_choice("none")
        assert result["functionCallingConfig"]["mode"] == "NONE"

    def test_convert_tool_choice_any_string(self):
        result = GeminiProviderAdapter._convert_tool_choice("any")
        assert result["functionCallingConfig"]["mode"] == "ANY"

    def test_convert_tool_choice_unknown_type_fallback_auto(self):
        result = GeminiProviderAdapter._convert_tool_choice({"type": "invalid"})
        assert result["functionCallingConfig"]["mode"] == "AUTO"


# =========================================================================
# Gemini — response finish_reason edge cases
# =========================================================================


class TestGeminiResponseFinishReasons:
    def setup_method(self):
        self.adapter = GeminiProviderAdapter()
        self.dep = Deployment(
            id="test-gemini",
            model_name="gemini-1.5-pro",
            api_key="google-test-key",
            api_base="https://generativelanguage.googleapis.com",
            provider="google-gemini",
        )

    def _make_response(self, finish_reason: str) -> dict:
        return {
            "candidates": [{
                "content": {"parts": [{"text": "Hello"}], "role": "model"},
                "finishReason": finish_reason,
            }],
            "usageMetadata": {"promptTokenCount": 5, "candidatesTokenCount": 5},
        }

    def test_finish_reason_max_tokens(self):
        result = self.adapter.transform_response(self._make_response("MAX_TOKENS"), "gemini-1.5-pro", self.dep)
        assert result["choices"][0]["finish_reason"] == "length"

    def test_finish_reason_safety(self):
        result = self.adapter.transform_response(self._make_response("SAFETY"), "gemini-1.5-pro", self.dep)
        assert result["choices"][0]["finish_reason"] == "content_filter"

    def test_finish_reason_recitation(self):
        result = self.adapter.transform_response(self._make_response("RECITATION"), "gemini-1.5-pro", self.dep)
        assert result["choices"][0]["finish_reason"] == "content_filter"

    def test_finish_reason_unknown_defaults_to_stop(self):
        result = self.adapter.transform_response(self._make_response("UNKNOWN"), "gemini-1.5-pro", self.dep)
        assert result["choices"][0]["finish_reason"] == "stop"

    def test_no_candidates(self):
        raw = {"candidates": [], "usageMetadata": {"promptTokenCount": 0, "candidatesTokenCount": 0}}
        result = self.adapter.transform_response(raw, "gemini-1.5-pro", self.dep)
        assert result["choices"][0]["message"]["content"] is None

    def test_response_with_function_call_stream(self):
        chunk = {
            "candidates": [{
                "content": {
                    "parts": [{"functionCall": {"name": "get_weather", "args": {"loc": "Beijing"}}}],
                    "role": "model",
                },
            }],
        }
        result = self.adapter.transform_stream_chunk(chunk, "gemini-1.5-pro", self.dep)
        assert result is not None
        tc = result["choices"][0]["delta"]["tool_calls"][0]
        assert tc["function"]["name"] == "get_weather"
        assert json.loads(tc["function"]["arguments"]) == {"loc": "Beijing"}


# =========================================================================
# Gemini — generation config edge cases
# =========================================================================


class TestGeminiGenerationConfig:
    def setup_method(self):
        self.adapter = GeminiProviderAdapter()
        self.dep = Deployment(
            id="test-gemini",
            model_name="gemini-1.5-pro",
            api_key="google-test-key",
            api_base="https://generativelanguage.googleapis.com",
            provider="google-gemini",
        )

    def test_all_generation_config_params(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            temperature=0.7,
            max_tokens=200,
            top_p=0.9,
            top_k=40,
            stop=["END"],
            seed=42,
            frequency_penalty=0.5,
            presence_penalty=0.3,
        )
        gc = result["generationConfig"]
        assert gc["temperature"] == 0.7
        assert gc["maxOutputTokens"] == 200
        assert gc["topP"] == 0.9
        assert gc["topK"] == 40
        assert gc["stopSequences"] == ["END"]
        assert gc["seed"] == 42
        assert gc["frequencyPenalty"] == 0.5
        assert gc["presencePenalty"] == 0.3

    def test_stop_as_single_string(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            stop="STOP_HERE",
        )
        assert result["generationConfig"]["stopSequences"] == ["STOP_HERE"]

    def test_tool_choice_conversion(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            tools=[{"function": {"name": "fn1", "description": "desc", "parameters": {"type": "object"}}}],
            tool_choice="any",
        )
        assert result["tools"][0]["functionDeclarations"][0]["name"] == "fn1"
        assert result["tool_config"]["functionCallingConfig"]["mode"] == "ANY"


# =========================================================================
# OpenAI — transform_stream_chunk passthrough
# =========================================================================


class TestOpenAIStreamChunk:
    def setup_method(self):
        self.adapter = OpenAIProviderAdapter()
        self.dep = Deployment(
            id="test-openai",
            model_name="gpt-4",
            api_key="sk-test-key",
            api_base="https://api.openai.com",
            provider="openai",
        )

    def test_transform_stream_chunk_passthrough(self):
        chunk = {"choices": [{"delta": {"content": "hello"}, "finish_reason": None}]}
        result = self.adapter.transform_stream_chunk(chunk, "gpt-4", self.dep)
        assert result is chunk

    def test_transform_stream_chunk_returns_none(self):
        chunk = {"choices": [{"delta": {}, "finish_reason": "stop"}]}
        result = self.adapter.transform_stream_chunk(chunk, "gpt-4", self.dep)
        assert result is chunk


# =========================================================================
# Registry — register with existing key
# =========================================================================


class TestRegistryOverwrite:
    def test_register_overwrite(self):
        class FirstAdapter(BaseProviderAdapter):
            def get_api_url(self, dep): return "http://first"
            def get_headers(self, dep): return {"A": "1"}

        class SecondAdapter(BaseProviderAdapter):
            def get_api_url(self, dep): return "http://second"
            def get_headers(self, dep): return {"A": "2"}

        register_provider("overwrite_test", FirstAdapter)
        register_provider("overwrite_test", SecondAdapter)
        adapter = get_provider_adapter("overwrite_test")
        assert isinstance(adapter, SecondAdapter)


# =========================================================================
# Anthropic — request kwarg passthrough
# =========================================================================


class TestAnthropicRequestKwargs:
    def setup_method(self):
        self.adapter = AnthropicProviderAdapter()
        self.dep = Deployment(
            id="test-anthropic",
            model_name="claude-3-opus",
            api_key="sk-ant-test",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )

    def test_top_k_passthrough(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            top_k=20,
        )
        assert result["top_k"] == 20

    def test_stop_sequences(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            stop=["END", "STOP"],
        )
        assert result["stop_sequences"] == ["END", "STOP"]

    def test_stop_as_single_string(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            stop="END",
        )
        assert result["stop_sequences"] == ["END"]

    def test_metadata_passthrough(self):
        result = self.adapter.transform_request(
            model="claude-3-opus",
            messages=[{"role": "user", "content": "Hi"}],
            deployment=self.dep,
            metadata={"user_id": "abc"},
        )
        assert result["metadata"] == {"user_id": "abc"}


# =========================================================================
# Gemini — message conversion edge cases
# =========================================================================


class TestGeminiMessageConversion:
    def setup_method(self):
        self.adapter = GeminiProviderAdapter()
        self.dep = Deployment(
            id="test-gemini",
            model_name="gemini-1.5-pro",
            api_key="google-test-key",
            api_base="https://generativelanguage.googleapis.com",
            provider="google-gemini",
        )

    def test_tool_result_message(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[
                {"role": "assistant", "content": "Check", "tool_calls": [{"id": "tc_1", "function": {"name": "fn1", "arguments": "{}"}}]},
                {"role": "tool", "tool_call_id": "tc_1", "name": "fn1", "content": "42"},
            ],
            deployment=self.dep,
        )
        tool_result = result["contents"][-1]["parts"][0]
        assert tool_result["functionResponse"]["name"] == "fn1"

    def test_assistant_with_tool_calls(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[
                {"role": "user", "content": "Weather?"},
                {"role": "assistant", "content": "Let me check",
                 "tool_calls": [{"id": "tc_1", "type": "function",
                                "function": {"name": "get_weather", "arguments": '{"loc": "Beijing"}'}}]},
            ],
            deployment=self.dep,
        )
        parts = result["contents"][1]["parts"]
        assert any("functionCall" in p and p["functionCall"]["name"] == "get_weather" for p in parts)

    def test_consecutive_same_role_merged(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[
                {"role": "user", "content": "First"},
                {"role": "user", "content": "Second"},
            ],
            deployment=self.dep,
        )
        assert len(result["contents"]) == 1
        assert len(result["contents"][0]["parts"]) == 2

    def test_non_string_content(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[{"role": "user", "content": 456}],
            deployment=self.dep,
        )
        assert result["contents"][0]["parts"][0]["text"] == "456"

    def test_system_message_list_content(self):
        result = self.adapter.transform_request(
            model="gemini-1.5-pro",
            messages=[
                {"role": "system", "content": ["part1", "part2"]},
                {"role": "user", "content": "Hi"},
            ],
            deployment=self.dep,
        )
        assert "system_instruction" in result
        assert result["system_instruction"]["parts"][0]["text"]


# ============================================================
# Anthropic 多模态 content block 转换
# ============================================================

class TestAnthropicMultimodal:
    """测试 Anthropic adapter 的 OpenAI → Anthropic 多模态 content block 转换。"""

    def setup_method(self):
        from intelli_router.provider.anthropic_provider import AnthropicProviderAdapter
        from intelli_router.core.deployment import Deployment
        self.adapter = AnthropicProviderAdapter()
        self.dep = Deployment(
            model_name="claude-3-5-sonnet-20241022",
            api_key="test-key",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )

    def _get_user_content(self, messages):
        """发送请求并返回第一个 user 消息的 content 列表。"""
        result = self.adapter.transform_request(
            model="claude-3-5-sonnet-20241022",
            messages=messages,
            deployment=self.dep,
        )
        for msg in result["messages"]:
            if msg["role"] == "user":
                return msg["content"]
        return []

    def test_image_url_base64(self):
        messages = [{"role": "user", "content": [
            {"type": "text", "text": "What's in this image?"},
            {"type": "image_url", "image_url": {
                "url": "data:image/png;base64,iVBORw0KGgo=",
            }},
        ]}]
        content = self._get_user_content(messages)
        assert content[0] == {"type": "text", "text": "What's in this image?"}
        assert content[1] == {
            "type": "image",
            "source": {
                "type": "base64",
                "media_type": "image/png",
                "data": "iVBORw0KGgo=",
            },
        }

    def test_image_url_https(self):
        messages = [{"role": "user", "content": [
            {"type": "image_url", "image_url": {
                "url": "https://example.com/photo.jpg",
            }},
        ]}]
        content = self._get_user_content(messages)
        assert content[0] == {
            "type": "image",
            "source": {"type": "url", "url": "https://example.com/photo.jpg"},
        }

    def test_image_url_http_passes_with_warning(self):
        """HTTP URL 原样传递（Anthropic 会拒绝），但不抛异常。"""
        messages = [{"role": "user", "content": [
            {"type": "image_url", "image_url": {
                "url": "http://example.com/photo.jpg",
            }},
        ]}]
        content = self._get_user_content(messages)
        assert content[0] == {
            "type": "image",
            "source": {"type": "url", "url": "http://example.com/photo.jpg"},
        }

    def test_file_base64(self):
        messages = [{"role": "user", "content": [
            {"type": "file", "file": {
                "file_data": "data:application/pdf;base64,JVBERi0xLjQ=",
            }},
        ]}]
        content = self._get_user_content(messages)
        assert content[0] == {
            "type": "document",
            "source": {
                "type": "base64",
                "media_type": "application/pdf",
                "data": "JVBERi0xLjQ=",
            },
        }

    def test_file_url(self):
        messages = [{"role": "user", "content": [
            {"type": "file", "file": {
                "file_id": "https://example.com/report.pdf",
            }},
        ]}]
        content = self._get_user_content(messages)
        assert content[0] == {
            "type": "document",
            "source": {"type": "url", "url": "https://example.com/report.pdf"},
        }

    def test_file_id_reference(self):
        messages = [{"role": "user", "content": [
            {"type": "file", "file": {"file_id": "file_abc123"}},
        ]}]
        content = self._get_user_content(messages)
        assert content[0] == {
            "type": "document",
            "source": {"type": "file", "file_id": "file_abc123"},
        }

    def test_unsupported_audio_skipped(self):
        messages = [{"role": "user", "content": [
            {"type": "text", "text": "Transcribe this"},
            {"type": "input_audio", "input_audio": {"data": "base64audio", "format": "wav"}},
        ]}]
        content = self._get_user_content(messages)
        assert len(content) == 1
        assert content[0] == {"type": "text", "text": "Transcribe this"}

    def test_unsupported_video_skipped(self):
        messages = [{"role": "user", "content": [
            {"type": "text", "text": "Describe this video"},
            {"type": "video_url", "video_url": {"url": "https://example.com/v.mp4"}},
        ]}]
        content = self._get_user_content(messages)
        assert len(content) == 1
        assert content[0] == {"type": "text", "text": "Describe this video"}

    def test_mixed_content(self):
        messages = [{"role": "user", "content": [
            {"type": "text", "text": "Analyze these:"},
            {"type": "image_url", "image_url": {"url": "data:image/jpeg;base64,/9j/4AAQ"}},
            {"type": "file", "file": {"file_data": "data:application/pdf;base64,JVBERi0="}},
        ]}]
        content = self._get_user_content(messages)
        assert len(content) == 3
        assert content[0]["type"] == "text"
        assert content[1]["type"] == "image"
        assert content[2]["type"] == "document"

    def test_already_anthropic_format_passthrough(self):
        """已经是 Anthropic 原生格式的 block 应透传。"""
        native_block = {
            "type": "image",
            "source": {"type": "base64", "media_type": "image/png", "data": "abc"},
        }
        messages = [{"role": "user", "content": [native_block]}]
        content = self._get_user_content(messages)
        assert content[0] == native_block

    def test_text_block_passthrough(self):
        messages = [{"role": "user", "content": [
            {"type": "text", "text": "Hello world"},
        ]}]
        content = self._get_user_content(messages)
        assert content[0] == {"type": "text", "text": "Hello world"}
