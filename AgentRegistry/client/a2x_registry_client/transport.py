"""Thin httpx wrappers — the SDK's only network exit.

``HTTPTransport`` and ``AsyncHTTPTransport`` expose a single ``request`` entry
point. 2xx responses are returned as raw ``httpx.Response`` (so callers can
inspect Content-Type etc.); 4xx/5xx and transport exceptions are translated to
``A2XError`` subclasses via the shared ``_wrap_http_error`` / ``_wrap_transport_error``
helpers so sync and async paths stay in lockstep.
"""

from __future__ import annotations

from typing import Any, Literal

import httpx

from .errors import (
    A2XAuthenticationError,
    A2XAuthorizationError,
    A2XConnectionError,
    A2XHeartbeatNotSupportedError,
    A2XHTTPError,
    A2XTTLOutOfRangeError,
    A2XTTLRequiredError,
    NotFoundError,
    ServerError,
    UserConfigServiceImmutableError,
    ValidationError,
)


# 400 dispatch table: server-emitted ``code`` field → SDK error subclass.
# Codes here all come from a2x_registry/heartbeat/errors.py.
_HEARTBEAT_400_DISPATCH = {
    "heartbeat_not_supported": A2XHeartbeatNotSupportedError,
    "ttl_required":            A2XTTLRequiredError,
    "ttl_out_of_range":        A2XTTLOutOfRangeError,
}

HTTPMethod = Literal["GET", "POST", "PUT", "DELETE"]


def _parse_payload(resp: httpx.Response) -> dict[str, Any] | None:
    try:
        data = resp.json()
    except ValueError:
        return None
    return data if isinstance(data, dict) else {"detail": data}


def _wrap_http_error(resp: httpx.Response) -> A2XHTTPError:
    """Map a non-2xx response to the right ``A2XHTTPError`` subclass."""
    payload = _parse_payload(resp)
    # Server may emit the FastAPI default {"detail": "..."} OR a structured
    # body where ``detail`` itself is a dict (heartbeat errors). Extract a
    # display string + the structured info if present.
    raw_detail = (payload or {}).get("detail")
    detail_str = ""
    detail_obj: dict | None = None
    if isinstance(raw_detail, str):
        detail_str = raw_detail
    elif isinstance(raw_detail, dict):
        detail_obj = raw_detail
        detail_str = str(raw_detail.get("detail") or raw_detail.get("code") or "")
    status = resp.status_code
    message = f"HTTP {status}: {detail_str or resp.reason_phrase or 'request failed'}"

    if status == 401:
        return A2XAuthenticationError(message, status_code=status, payload=payload)
    if status == 403:
        return A2XAuthorizationError(message, status_code=status, payload=payload)
    if status == 404:
        return NotFoundError(message, status_code=status, payload=payload)
    if status in (400, 422):
        # Heartbeat-domain errors arrive with a structured detail dict
        # containing ``code``. Dispatch to the right subclass so callers
        # can ``except A2XTTLOutOfRangeError`` and read .min_ttl/.max_ttl.
        # ``payload`` for these subclasses is the inner detail dict so
        # the error's __init__ can read min_ttl/max_ttl directly.
        if detail_obj is not None:
            code = detail_obj.get("code")
            cls = _HEARTBEAT_400_DISPATCH.get(code)
            if cls is not None:
                return cls(message, status_code=status, payload=detail_obj)
        if "user_config" in detail_str:
            return UserConfigServiceImmutableError(
                message, status_code=status, payload=payload
            )
        return ValidationError(message, status_code=status, payload=payload)
    if 500 <= status < 600:
        return ServerError(message, status_code=status, payload=payload)
    return A2XHTTPError(message, status_code=status, payload=payload)


def _wrap_transport_error(exc: Exception) -> A2XConnectionError:
    return A2XConnectionError(f"{type(exc).__name__}: {exc}")


class HTTPTransport:
    """Synchronous HTTP transport backed by ``httpx.Client``."""

    def __init__(
        self,
        base_url: str,
        timeout: float = 30.0,
        headers: dict[str, str] | None = None,
    ) -> None:
        self._client = httpx.Client(base_url=base_url, timeout=timeout, headers=headers)

    def request(
        self,
        method: HTTPMethod,
        path: str,
        *,
        json: dict[str, Any] | None = None,
        params: dict[str, Any] | None = None,
    ) -> httpx.Response:
        try:
            resp = self._client.request(method, path, json=json, params=params)
        except httpx.HTTPError as exc:
            raise _wrap_transport_error(exc) from exc
        if resp.status_code >= 400:
            raise _wrap_http_error(resp)
        return resp

    def close(self) -> None:
        self._client.close()

    def __enter__(self) -> "HTTPTransport":
        return self

    def __exit__(self, *_exc: Any) -> None:
        self.close()


class AsyncHTTPTransport:
    """Asynchronous HTTP transport backed by ``httpx.AsyncClient``."""

    def __init__(
        self,
        base_url: str,
        timeout: float = 30.0,
        headers: dict[str, str] | None = None,
    ) -> None:
        self._client = httpx.AsyncClient(base_url=base_url, timeout=timeout, headers=headers)

    async def request(
        self,
        method: HTTPMethod,
        path: str,
        *,
        json: dict[str, Any] | None = None,
        params: dict[str, Any] | None = None,
    ) -> httpx.Response:
        try:
            resp = await self._client.request(method, path, json=json, params=params)
        except httpx.HTTPError as exc:
            raise _wrap_transport_error(exc) from exc
        if resp.status_code >= 400:
            raise _wrap_http_error(resp)
        return resp

    async def aclose(self) -> None:
        await self._client.aclose()

    async def __aenter__(self) -> "AsyncHTTPTransport":
        return self

    async def __aexit__(self, *_exc: Any) -> None:
        await self.aclose()
