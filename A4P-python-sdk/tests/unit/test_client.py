from __future__ import annotations

import asyncio
import io
import json
import urllib.error
from typing import Any

import pytest

from a4p.client import A4PClient, default_a4p_base_url, default_a4p_timeout


_BOOLEAN_RESPONSES = [
    ("prepare_intent_authorization", "approved", False),
    ("complete_intent_authorization", "approved", False),
    ("complete_operation_authorization", "approved", False),
    ("verify_intent_token", "valid", False),
    ("prepare_intent_authorization", "valid", True),
    ("complete_intent_authorization", "valid", True),
    ("prepare_operation_authorization", "valid", True),
    ("complete_operation_authorization", "valid", True),
]


class _Response:
    def __init__(self, body: bytes) -> None:
        self.body = body

    def __enter__(self) -> "_Response":
        return self

    def __exit__(self, *args: object) -> None:
        return None

    def read(self) -> bytes:
        return self.body


@pytest.mark.parametrize(("method", "field", "nested"), _BOOLEAN_RESPONSES)
@pytest.mark.parametrize(
    "value",
    [True, False, "false", "true", "", 0, 1, 0.0, 1.0, None, [], {}, [False], {"valid": False}],
)
def test_client_requires_json_boolean_response_fields(
    monkeypatch: pytest.MonkeyPatch, method: str, field: str, nested: bool, value: Any
) -> None:
    """所有授权和验证接口只接受 JSON 布尔值，不按 Python 真值转换其他类型。"""
    payload = {"verificationResult": {field: value}} if nested else {field: value}
    monkeypatch.setattr(
        "urllib.request.urlopen", lambda *args, **kwargs: _Response(json.dumps(payload).encode("utf-8"))
    )
    request = getattr(A4PClient(base_url="http://a4p.example"), method)

    if isinstance(value, bool):
        result = asyncio.run(request({}))
        target = result.verificationResult if nested else result
        assert getattr(target, field) is value
    else:
        with pytest.raises(RuntimeError, match=rf"A4P HTTP response.*{field}.*boolean"):
            asyncio.run(request({}))


@pytest.mark.parametrize(("method", "field", "nested"), _BOOLEAN_RESPONSES)
def test_client_distinguishes_required_and_default_boolean_fields(
    monkeypatch: pytest.MonkeyPatch, method: str, field: str, nested: bool
) -> None:
    """valid 不可缺失；可选 approved 缺省时保留 False 默认值。"""
    payload = {"verificationResult": {}} if nested else {}
    monkeypatch.setattr(
        "urllib.request.urlopen", lambda *args, **kwargs: _Response(json.dumps(payload).encode("utf-8"))
    )
    request = getattr(A4PClient(base_url="http://a4p.example"), method)
    if field == "approved":
        result = asyncio.run(request({}))
        assert result.approved is False
        assert result.verificationResult is None
    else:
        with pytest.raises(RuntimeError, match="A4P HTTP response.*valid.*boolean"):
            asyncio.run(request({}))


@pytest.mark.parametrize(
    "method",
    [
        "prepare_intent_authorization", "complete_intent_authorization",
        "prepare_operation_authorization", "complete_operation_authorization",
    ],
)
@pytest.mark.parametrize("value", [None, "false", False, 1, []])
def test_client_checks_optional_verification_result_shape(
    monkeypatch: pytest.MonkeyPatch, method: str, value: Any
) -> None:
    """verificationResult 可以为 null，但非空值必须是可校验的对象。"""
    monkeypatch.setattr(
        "urllib.request.urlopen",
        lambda *args, **kwargs: _Response(json.dumps({"verificationResult": value}).encode("utf-8")),
    )
    request = getattr(A4PClient(base_url="http://a4p.example"), method)
    if value is None:
        assert asyncio.run(request({})).verificationResult is None
    else:
        with pytest.raises(RuntimeError, match="A4P HTTP response.*verificationResult.*object"):
            asyncio.run(request({}))


def test_client_defaults_are_normalized(monkeypatch: pytest.MonkeyPatch) -> None:
    """客户端从环境变量读取基础地址和超时时间时，应去除多余路径分隔符并转换为规范类型。"""
    monkeypatch.setenv("A4P_SERVER_BASE_URL", "http://a4p.example/")
    monkeypatch.setenv("A4P_HTTP_TIMEOUT_S", "0.5")

    assert default_a4p_base_url() == "http://a4p.example"
    assert default_a4p_timeout() == 1.0

    monkeypatch.setenv("A4P_HTTP_TIMEOUT_S", "invalid")
    assert default_a4p_timeout() == 300.0


@pytest.mark.parametrize(
    ("body", "expected"),
    [
        (b'{"error":"invalid_request"}', "invalid_request"),
        (b"not-json", "not-json"),
    ],
)
def test_client_maps_http_errors(
    monkeypatch: pytest.MonkeyPatch,
    body: bytes,
    expected: str,
) -> None:
    """客户端收到包含或不包含 JSON 错误体的 HTTP 异常时，应生成包含状态和原因的 RuntimeError。"""
    def raise_http_error(*args: object, **kwargs: object) -> None:
        raise urllib.error.HTTPError(
            url="http://a4p.example/test",
            code=422,
            msg="Unprocessable Entity",
            hdrs=None,
            fp=io.BytesIO(body),
        )

    monkeypatch.setattr("urllib.request.urlopen", raise_http_error)
    client = A4PClient(base_url="http://a4p.example")

    with pytest.raises(RuntimeError, match=rf"A4P HTTP 422: .*{expected}"):
        asyncio.run(client.webauthn_registration_options({"userId": "user-1"}))


def test_client_maps_transport_errors(monkeypatch: pytest.MonkeyPatch) -> None:
    """客户端遇到网络传输异常时，应将底层错误映射为稳定的 RuntimeError。"""
    def raise_url_error(*args: object, **kwargs: object) -> None:
        raise urllib.error.URLError("connection refused")

    monkeypatch.setattr("urllib.request.urlopen", raise_url_error)
    client = A4PClient(base_url="http://a4p.example")

    with pytest.raises(
        RuntimeError, match="A4P HTTP request failed:.*connection refused"
    ):
        asyncio.run(client.webauthn_registration_options({"userId": "user-1"}))


@pytest.mark.parametrize("body", [b"", b"[]"])
def test_client_normalizes_empty_and_non_object_responses(
    monkeypatch: pytest.MonkeyPatch,
    body: bytes,
) -> None:
    """客户端收到空响应或非对象 JSON 响应时，应将结果规范化为可用的字典结构。"""
    monkeypatch.setattr(
        "urllib.request.urlopen",
        lambda *args, **kwargs: _Response(body),
    )

    result = asyncio.run(
        A4PClient(base_url="http://a4p.example").webauthn_registration_options(
            {"userId": "user-1"}
        )
    )

    assert result == {}


@pytest.mark.parametrize("body", [b"not-json", b"<html>proxy error</html>"])
def test_client_reports_non_json_success_responses(
    monkeypatch: pytest.MonkeyPatch,
    body: bytes,
) -> None:
    """2xx 非 JSON 响应（代理 HTML、错端口等）应转换为客户端的 RuntimeError 契约，而不是泄漏 JSONDecodeError。"""
    monkeypatch.setattr(
        "urllib.request.urlopen",
        lambda *args, **kwargs: _Response(body),
    )

    with pytest.raises(RuntimeError, match="A4P HTTP response is not valid JSON"):
        asyncio.run(
            A4PClient(base_url="http://a4p.example").webauthn_registration_options(
                {"userId": "user-1"}
            )
        )
