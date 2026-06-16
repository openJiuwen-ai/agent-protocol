"""
System tests for intelli_router — full pipeline: router → provider adapter → HTTP → response.

Uses httpx.MockTransport (built-in, zero new dependencies) to mock the HTTP layer.
"""
import json
import time
import httpx
import pytest
from httpx import MockTransport, Request, Response
from typing import List, Dict, Any

from intelli_router.router.base_router import BaseRouter
from intelli_router.router.reliable_router import ReliableRouter
from intelli_router.core.deployment import Deployment, DeploymentStatus
from intelli_router.utils.exceptions import RouterError


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class RequestCapture:
    """Captures all requests made through MockTransport."""

    def __init__(self):
        self.requests: List[Request] = []

    def handler(self, request: Request) -> Response:
        self.requests.append(request)
        return self._respond(request)

    def _respond(self, request: Request) -> Response:
        msg = f"_respond() not implemented — use a subclass or set respond_fn"
        raise NotImplementedError(msg)


def make_handler(response: Response) -> RequestCapture:
    """Create a single-response handler."""
    capture = RequestCapture()
    orig_respond = capture._respond

    def respond(_request):
        return response

    capture._respond = respond
    return capture


def make_sequential_handler(responses: List[Response]) -> RequestCapture:
    """Create a handler that returns different responses in sequence."""
    capture = RequestCapture()
    idx = [0]  # mutable capture

    def respond(_request):
        resp = responses[idx[0] % len(responses)]
        idx[0] += 1
        return resp

    capture._respond = respond
    return capture


def make_streaming_handler(stream_lines: List[str]) -> RequestCapture:
    """Create a handler that returns SSE streaming content."""
    body = "\n".join(stream_lines)
    return make_handler(Response(200, content=body))


def attach_mock_transport(router, capture: RequestCapture):
    """Replace the router's httpx client with one using MockTransport."""
    router._client = httpx.AsyncClient(
        transport=MockTransport(capture.handler),
        timeout=httpx.Timeout(5.0),
    )


def openai_response(content: str = "hello") -> Response:
    return Response(200, json={
        "choices": [{"index": 0, "message": {"role": "assistant", "content": content},
                      "finish_reason": "stop"}],
        "usage": {"completion_tokens": 5, "total_tokens": 15},
    })


def server_error_response() -> Response:
    return Response(502, text="Bad Gateway")


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class TestBaseRouterOpenAI:
    """BaseRouter + OpenAI provider — full pipeline test."""

    @pytest.mark.asyncio
    async def test_completion_openai(self):
        dep = Deployment(
            id="test_openai",
            model_name="gpt-4",
            api_key="sk-test",
            api_base="https://api.openai.com",
            provider="openai",
        )
        router = BaseRouter(deployments=[dep])
        capture = make_handler(openai_response("hello world"))
        attach_mock_transport(router, capture)

        result = await router.completion(
            "gpt-4", [{"role": "user", "content": "hi"}]
        )

        assert result["choices"][0]["message"]["content"] == "hello world"
        assert len(capture.requests) == 1
        req = capture.requests[0]
        # URL from OpenAI adapter: {base}/v1/chat/completions
        assert str(req.url) == "https://api.openai.com/v1/chat/completions"
        # Headers from OpenAI adapter
        assert req.headers.get("authorization") == "Bearer sk-test"
        assert req.headers.get("content-type") == "application/json"
        # Request body — OpenAI uses passthrough format
        body = json.loads(req.content)
        assert body["model"] == "gpt-4"
        assert body["messages"] == [{"role": "user", "content": "hi"}]

    @pytest.mark.asyncio
    async def test_completion_openai_model_not_found(self):
        router = BaseRouter(deployments=[])
        with pytest.raises(Exception):
            await router.completion("unknown", [{"role": "user", "content": "hi"}])


class TestBaseRouterGemini:
    """BaseRouter + Gemini provider adapter."""

    @pytest.mark.asyncio
    async def test_completion_gemini(self):
        dep = Deployment(
            id="test_gemini",
            model_name="gemini-pro",
            api_key="ai-key",
            api_base="https://generativelanguage.googleapis.com",
            provider="google-gemini",
        )
        router = BaseRouter(deployments=[dep])
        gemini_raw = {
            "candidates": [{
                "content": {"parts": [{"text": "gemini reply"}], "role": "model"},
                "finishReason": "STOP",
            }],
            "usageMetadata": {"promptTokenCount": 10, "candidatesTokenCount": 20},
        }
        capture = make_handler(Response(200, json=gemini_raw))
        attach_mock_transport(router, capture)

        result = await router.completion(
            "gemini-pro", [{"role": "user", "content": "hello"}]
        )

        # Response transformed to OpenAI format
        assert result["choices"][0]["message"]["content"] == "gemini reply"
        assert result["choices"][0]["finish_reason"] == "stop"
        assert len(capture.requests) == 1
        req = capture.requests[0]
        # URL from Gemini adapter
        assert "generativelanguage.googleapis.com" in str(req.url)
        # Header from Gemini adapter
        assert req.headers.get("x-goog-api-key") == "ai-key"
        # Body in Gemini format (contents array)
        body = json.loads(req.content)
        assert "contents" in body
        assert body["contents"][0]["parts"][0]["text"] == "hello"

    @pytest.mark.asyncio
    async def test_completion_gemini_with_system_instruction(self):
        dep = Deployment(
            id="test_gemini_sys",
            model_name="gemini-pro",
            api_key="ai-key",
            api_base="https://generativelanguage.googleapis.com",
            provider="google-gemini",
        )
        router = BaseRouter(deployments=[dep])
        gemini_raw = {
            "candidates": [{
                "content": {"parts": [{"text": "ok"}], "role": "model"},
                "finishReason": "STOP",
            }],
        }
        capture = make_handler(Response(200, json=gemini_raw))
        attach_mock_transport(router, capture)

        await router.completion(
            "gemini-pro",
            [
                {"role": "system", "content": "Be concise."},
                {"role": "user", "content": "hello"},
            ],
        )

        body = json.loads(capture.requests[0].content)
        assert "system_instruction" in body
        assert body["system_instruction"]["parts"][0]["text"] == "Be concise."


class TestBaseRouterAnthropic:
    """BaseRouter + Anthropic provider adapter."""

    @pytest.mark.asyncio
    async def test_completion_anthropic(self):
        dep = Deployment(
            id="test_anthropic",
            model_name="claude-3-opus",
            api_key="sk-ant-key",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )
        router = BaseRouter(deployments=[dep])
        anthropic_raw = {
            "id": "msg_123",
            "content": [{"type": "text", "text": "Hello from Claude"}],
            "stop_reason": "end_turn",
            "usage": {"input_tokens": 10, "output_tokens": 5},
        }
        capture = make_handler(Response(200, json=anthropic_raw))
        attach_mock_transport(router, capture)

        result = await router.completion(
            "claude-3-opus", [{"role": "user", "content": "hi"}]
        )

        assert result["choices"][0]["message"]["content"] == "Hello from Claude"
        assert result["choices"][0]["finish_reason"] == "stop"
        req = capture.requests[0]
        # URL from Anthropic adapter: {base}/v1/messages
        assert str(req.url) == "https://api.anthropic.com/v1/messages"
        assert req.headers.get("x-api-key") == "sk-ant-key"
        assert req.headers.get("anthropic-version") == "2023-06-01"
        # Body in Anthropic format
        body = json.loads(req.content)
        assert body["model"] == "claude-3-opus"
        assert body["messages"][0]["content"][0]["text"] == "hi"

    @pytest.mark.asyncio
    async def test_completion_anthropic_tool_call(self):
        dep = Deployment(
            id="test_anthropic_tc",
            model_name="claude-3-opus",
            api_key="sk-ant-key",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )
        router = BaseRouter(deployments=[dep])
        anthropic_raw = {
            "id": "msg_456",
            "content": [{
                "type": "tool_use",
                "id": "tu_1",
                "name": "get_weather",
                "input": {"city": "Beijing"},
            }],
            "stop_reason": "tool_use",
            "usage": {"input_tokens": 20, "output_tokens": 10},
        }
        capture = make_handler(Response(200, json=anthropic_raw))
        attach_mock_transport(router, capture)

        result = await router.completion(
            "claude-3-opus", [{"role": "user", "content": "weather?"}]
        )

        msg = result["choices"][0]["message"]
        assert msg["content"] is None
        assert len(msg["tool_calls"]) == 1
        assert msg["tool_calls"][0]["function"]["name"] == "get_weather"
        assert result["choices"][0]["finish_reason"] == "tool_calls"


class TestBaseRouterStreaming:
    """Streaming completion through provider adapters."""

    @pytest.mark.asyncio
    async def test_streaming_openai(self):
        dep = Deployment(
            id="test_stream_openai",
            model_name="gpt-4",
            api_key="sk-test",
            api_base="https://api.openai.com",
            provider="openai",
        )
        router = BaseRouter(deployments=[dep])
        # OpenAI streaming — SSE lines
        chunks = [
            'data: {"choices":[{"delta":{"role":"assistant"},"finish_reason":null}]}',
            'data: {"choices":[{"delta":{"content":"hello"},"finish_reason":null}]}',
            'data: {"choices":[{"delta":{"content":" world"},"finish_reason":null}]}',
            'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}',
            "data: [DONE]",
        ]
        capture = make_streaming_handler(chunks)
        attach_mock_transport(router, capture)

        collected = []
        async for chunk in router.acompletion_stream(
            "gpt-4", [{"role": "user", "content": "hi"}]
        ):
            collected.append(chunk)

        # OpenAI uses passthrough streaming — chunks returned as-is
        assert len(collected) == 4
        assert collected[0]["choices"][0]["delta"]["role"] == "assistant"
        assert collected[1]["choices"][0]["delta"]["content"] == "hello"
        assert collected[2]["choices"][0]["delta"]["content"] == " world"

    @pytest.mark.asyncio
    async def test_streaming_gemini(self):
        dep = Deployment(
            id="test_stream_gemini",
            model_name="gemini-pro",
            api_key="ai-key",
            api_base="https://generativelanguage.googleapis.com",
            provider="google-gemini",
        )
        router = BaseRouter(deployments=[dep])
        # Gemini streaming SSE — each line must start with "data: " prefix
        chunks = [
            "data: " + json.dumps({"candidates": [{"content": {"parts": [{"text": "hi"}]}}]}),
            "data: " + json.dumps({"candidates": [{"content": {"parts": [{"text": " there"}]}, "finishReason": "STOP"}]}),
        ]
        capture = make_streaming_handler(chunks)
        attach_mock_transport(router, capture)

        collected = []
        async for chunk in router.acompletion_stream(
            "gemini-pro", [{"role": "user", "content": "hello"}]
        ):
            collected.append(chunk)

        assert len(collected) == 2
        # Gemini adapter transforms to OpenAI format
        assert collected[0]["choices"][0]["delta"]["content"] == "hi"
        assert collected[1]["choices"][0]["delta"]["content"] == " there"
        assert collected[1]["choices"][0]["finish_reason"] == "stop"

    @pytest.mark.asyncio
    async def test_streaming_anthropic(self):
        dep = Deployment(
            id="test_stream_anthropic",
            model_name="claude-3-haiku",
            api_key="sk-ant-key",
            api_base="https://api.anthropic.com",
            provider="anthropic",
        )
        router = BaseRouter(deployments=[dep])
        # Anthropic streaming events — each line must start with "data: " prefix
        chunks = [
            "data: " + json.dumps({"type": "message_start", "message": {"id": "msg_1"}}),
            "data: " + json.dumps({"type": "content_block_delta", "delta": {"type": "text_delta", "text": "Hello"}}),
            "data: " + json.dumps({"type": "content_block_delta", "delta": {"type": "text_delta", "text": " world"}}),
            "data: " + json.dumps({"type": "message_delta", "delta": {"stop_reason": "end_turn"}, "usage": {"output_tokens": 5}}),
        ]
        capture = make_streaming_handler(chunks)
        attach_mock_transport(router, capture)

        collected = []
        async for chunk in router.acompletion_stream(
            "claude-3-haiku", [{"role": "user", "content": "hi"}]
        ):
            collected.append(chunk)

        assert len(collected) == 4
        # message_start → role delta
        assert collected[0]["choices"][0]["delta"]["role"] == "assistant"
        # content_block_delta → content delta
        assert collected[1]["choices"][0]["delta"]["content"] == "Hello"
        assert collected[2]["choices"][0]["delta"]["content"] == " world"
        # message_delta → finish_reason
        assert collected[3]["choices"][0]["finish_reason"] == "stop"


class TestFallback:
    """BaseRouter fallback — HTTP-level end-to-end."""

    @pytest.mark.asyncio
    async def test_fallback_primary_fails_fallback_succeeds(self):
        dep1 = Deployment(
            id="primary",
            model_name="gpt-4",
            api_key="sk-1",
            api_base="https://primary.example.com",
            provider="openai",
        )
        dep2 = Deployment(
            id="fallback",
            model_name="gpt-3.5-turbo",
            api_key="sk-2",
            api_base="https://fallback.example.com",
            provider="openai",
        )
        router = BaseRouter(deployments=[dep1, dep2])
        # First request returns 502, second returns 200
        responses = [server_error_response(), openai_response("fallback ok")]
        capture = make_sequential_handler(responses)
        attach_mock_transport(router, capture)

        result = await router.completion_with_fallback(
            "gpt-4",
            [{"role": "user", "content": "hi"}],
            fallback={"gpt-4": "gpt-3.5-turbo"},
        )

        assert result["choices"][0]["message"]["content"] == "fallback ok"
        assert len(capture.requests) >= 1

    @pytest.mark.asyncio
    async def test_fallback_all_fail(self):
        dep1 = Deployment(
            id="primary",
            model_name="gpt-4",
            api_key="sk-1",
            api_base="https://primary.example.com",
            provider="openai",
        )
        dep2 = Deployment(
            id="fallback",
            model_name="gpt-3.5-turbo",
            api_key="sk-2",
            api_base="https://fallback.example.com",
            provider="openai",
        )
        router = BaseRouter(deployments=[dep1, dep2])
        capture = make_handler(server_error_response())
        attach_mock_transport(router, capture)

        with pytest.raises(RouterError):
            await router.completion_with_fallback(
                "gpt-4",
                [{"role": "user", "content": "hi"}],
                fallback={"gpt-4": "gpt-3.5-turbo"},
            )


class TestReliableRouterRetry:
    """ReliableRouter retry logic — end-to-end."""

    @pytest.mark.asyncio
    async def test_retry_first_fails_second_succeeds(self):
        dep1 = Deployment(
            id="r1", model_name="gpt-4", api_key="sk-1",
            api_base="https://api1.example.com", provider="openai",
        )
        dep2 = Deployment(
            id="r2", model_name="gpt-4", api_key="sk-2",
            api_base="https://api2.example.com", provider="openai",
        )
        router = ReliableRouter(
            deployments=[dep1, dep2],
            num_retries=1,
            strategy="simple-shuffle",
        )
        responses = [server_error_response(), openai_response("retry ok")]
        capture = make_sequential_handler(responses)
        attach_mock_transport(router, capture)

        result = await router.completion("gpt-4", [{"role": "user", "content": "hi"}])

        assert result["choices"][0]["message"]["content"] == "retry ok"
        # Two HTTP calls should have been made
        assert len(capture.requests) == 2

    @pytest.mark.asyncio
    async def test_retry_all_fail_raises(self):
        dep1 = Deployment(
            id="r1", model_name="gpt-4", api_key="sk-1",
            api_base="https://api1.example.com", provider="openai",
        )
        dep2 = Deployment(
            id="r2", model_name="gpt-4", api_key="sk-2",
            api_base="https://api2.example.com", provider="openai",
        )
        router = ReliableRouter(
            deployments=[dep1, dep2],
            num_retries=0,
            strategy="simple-shuffle",
        )
        capture = make_handler(server_error_response())
        attach_mock_transport(router, capture)

        with pytest.raises(RouterError):
            await router.completion("gpt-4", [{"role": "user", "content": "hi"}])

    @pytest.mark.asyncio
    async def test_reliable_router_honors_cooldown_state(self):
        """Deployment in cooldown in state is excluded from selection."""
        dep1 = Deployment(
            id="r1", model_name="gpt-4", api_key="sk-1",
            api_base="https://api1.example.com", provider="openai",
        )
        router = ReliableRouter(
            deployments=[dep1],
            num_retries=0,
            strategy="simple-shuffle",
        )
        # Manually mark as cooldown in router state
        router.state.deployment_status["r1"] = DeploymentStatus.COOLDOWN
        router.state.cooldown_until["r1"] = time.time() + 3600

        capture = make_handler(openai_response())
        attach_mock_transport(router, capture)

        with pytest.raises(RouterError, match="No available deployment"):
            await router.completion("gpt-4", [{"role": "user", "content": "hi"}])


class TestReliableRouterAdaptive:
    """ReliableRouter with AdaptiveStrategy — cooldown via state.on_failure."""

    @pytest.mark.asyncio
    async def test_adaptive_strategy_triggers_cooldown(self):
        """AdaptiveStrategy.on_failure() calls state.on_failure() → cooldown."""
        dep = Deployment(
            id="adep", model_name="gpt-4", api_key="sk-1",
            api_base="https://api.example.com", provider="openai",
        )
        from intelli_router.core.state import LocalRouterState
        from intelli_router.strategy.adaptive import AdaptiveStrategy

        state = LocalRouterState()
        strategy = AdaptiveStrategy(state=state)
        router = ReliableRouter(
            deployments=[dep],
            num_retries=0,
            strategy=strategy,
        )
        capture = make_handler(server_error_response())
        attach_mock_transport(router, capture)

        with pytest.raises(RouterError):
            await router.completion("gpt-4", [{"role": "user", "content": "hi"}])

        # The deployment should be in cooldown after failure
        assert state.deployment_status.get("adep") == DeploymentStatus.COOLDOWN
        assert state.cooldown_until.get("adep", 0) > time.time()
        assert state.consecutive_failures.get("adep", 0) == 1
