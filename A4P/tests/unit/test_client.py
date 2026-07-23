from __future__ import annotations

import asyncio
import io
import urllib.error

import pytest

from a4p.client import A4PClient, default_a4p_base_url, default_a4p_timeout


class _Response:
    def __init__(self, body: bytes) -> None:
        self.body = body

    def __enter__(self) -> "_Response":
        return self

    def __exit__(self, *args: object) -> None:
        return None

    def read(self) -> bytes:
        return self.body


def test_client_defaults_are_normalized(monkeypatch: pytest.MonkeyPatch) -> None:
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
