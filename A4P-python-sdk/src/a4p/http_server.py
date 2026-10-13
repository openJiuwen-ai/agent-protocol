"""Minimal local HTTP server for A4P endpoints."""

from __future__ import annotations

import asyncio
import json
import logging
import os
from dataclasses import dataclass
from typing import Any

from a4p.errors import A4PProtocolError
from a4p.server import A4PServer
from a4p.types import to_payload


logger = logging.getLogger(__name__)

# Upper bound on a single request body; larger bodies are rejected with 413 so
# a malicious client cannot exhaust server memory.
MAX_BODY_SIZE = 1 * 1024 * 1024

# Request headers are read before the Content-Length cap can apply, so they
# need their own budget: without it a peer can allocate an unbounded amount of
# memory with well-formed header lines (the body cap only guards the body).
MAX_HEADER_LINE_SIZE = 8 * 1024
MAX_HEADER_SIZE = 64 * 1024
MAX_HEADER_COUNT = 100

# Request line: "METHOD SP TARGET SP VERSION" plus CRLF.
MAX_REQUEST_LINE_SIZE = 8 * 1024


class _PayloadTooLarge(Exception):
    """Raised when a request body exceeds :data:`MAX_BODY_SIZE`."""


class _BadRequest(Exception):
    """Raised when the peer sent a syntactically invalid or oversized request."""


@dataclass(frozen=True)
class _HTTPRequest:
    method: str
    path: str
    body: bytes


def a4p_http_host() -> str:
    return (os.getenv("A4P_SERVER_HOST") or "127.0.0.1").strip()


def a4p_http_port() -> int:
    raw = (os.getenv("A4P_SERVER_PORT") or "").strip()
    if not raw:
        return 8961
    try:
        port = int(raw)
    except ValueError:
        raise ValueError(f"A4P_SERVER_PORT must be an integer, got {raw!r}") from None
    if not 1 <= port <= 65535:
        raise ValueError(f"A4P_SERVER_PORT must be between 1 and 65535, got {port}")
    return port


class A4PHTTPServer:
    def __init__(self, a4p_server: A4PServer, *, host: str | None = None, port: int | None = None) -> None:
        self.a4p_server = a4p_server
        self.host = host or a4p_http_host()
        self.port = a4p_http_port() if port is None else port
        self._server: asyncio.AbstractServer | None = None

    async def start(self) -> None:
        if self._server is not None:
            return
        server = await asyncio.start_server(
            client_connected_cb=self._handle_client,
            host=self.host,
            port=self.port,
        )
        self._server = server
        sockets = server.sockets or []
        if sockets:
            self.port = int(sockets[0].getsockname()[1])
        logger.info("[A4PHTTPServer] started: http://%s:%s", self.host, self.port)

    async def stop(self) -> None:
        server = self._server
        if server is None:
            return
        self._server = None
        server.close()
        await server.wait_closed()
        logger.info("[A4PHTTPServer] stopped")

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            request = await self._read_request(reader)
            if request is None:
                return
            if request.method != "POST":
                await self._send_json(writer, 405, {"error": "method_not_allowed"})
                return
            try:
                payload = self._json_payload(request.body)
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                await self._send_json(
                    writer,
                    400,
                    {"error": "bad_request", "message": str(exc)},
                )
                return
            status, response = await self._dispatch(request.path, payload)
            await self._send_json(writer, status, response)
        except _PayloadTooLarge:
            await self._send_json(writer, 413, {"error": "payload_too_large"})
        except _BadRequest as exc:
            await self._send_json(writer, 400, {"error": "bad_request", "message": str(exc)})
        except Exception as exc:
            logger.exception("[A4PHTTPServer] request failed: %s", exc)
            await self._send_json(writer, 500, {"error": "internal_error", "message": str(exc)})
        finally:
            writer.close()
            await writer.wait_closed()

    async def _read_request(self, reader: asyncio.StreamReader) -> _HTTPRequest | None:
        try:
            first_line = await reader.readline()
        except ValueError:  # request line longer than the stream limit
            raise _BadRequest("request line too long") from None
        if not first_line:
            return None
        parts = first_line.decode("iso-8859-1").strip().split(maxsplit=2)
        if len(parts) != 3:
            # "GET\r\n" / "GET /" / an empty line from a proxy must be a 400,
            # not an unpacking ValueError that surfaces as a 500 leaking the
            # exception text back to the peer.
            raise _BadRequest("malformed request line")
        if len(first_line) > MAX_REQUEST_LINE_SIZE:
            raise _BadRequest("request line too long")
        method, path, _version = parts
        headers = await self._read_headers(reader)
        raw_length = headers.get("content-length")
        try:
            body_size = int(raw_length) if raw_length is not None else 0
        except ValueError:
            raise _BadRequest("invalid Content-Length") from None
        if body_size < 0:
            raise _BadRequest("invalid Content-Length")
        if body_size > MAX_BODY_SIZE:
            raise _PayloadTooLarge
        if body_size:
            try:
                body = await reader.readexactly(body_size)
            except asyncio.IncompleteReadError:
                # A peer that declares Content-Length: N and then sends fewer
                # bytes before closing makes readexactly raise
                # IncompleteReadError.  That is neither _PayloadTooLarge nor
                # _BadRequest, so it used to fall through to the generic
                # handler below: a 500 that echoed the exception text back to
                # the peer.  Report it as a malformed request instead.
                raise _BadRequest("incomplete body") from None
        else:
            body = b"{}"
        return _HTTPRequest(method=method.upper(), path=path, body=body)

    @staticmethod
    async def _read_headers(reader: asyncio.StreamReader) -> dict[str, str]:
        headers: dict[str, str] = {}
        total = 0
        count = 0
        while True:
            try:
                raw = await reader.readline()
            except ValueError:  # header line longer than the stream limit
                raise _BadRequest("request headers too long") from None
            if raw in {b"", b"\n", b"\r\n"}:
                return headers
            if len(raw) > MAX_HEADER_LINE_SIZE:
                raise _BadRequest("request header line too long")
            total += len(raw)
            if total > MAX_HEADER_SIZE:
                raise _BadRequest("request headers too large")
            count += 1
            if count > MAX_HEADER_COUNT:
                raise _BadRequest("too many request headers")
            name, has_separator, value = raw.decode("iso-8859-1").partition(":")
            if has_separator:
                headers[name.strip().lower()] = value.strip()

    @staticmethod
    def _json_payload(body: bytes) -> dict[str, Any]:
        decoded = json.loads(body.decode("utf-8") or "{}")
        return decoded if isinstance(decoded, dict) else {}

    async def _dispatch(self, path: str, payload: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        try:
            return await self._dispatch_checked(path, payload)
        except A4PProtocolError as exc:
            return exc.http_status, {
                "error": exc.code,
                "message": str(exc),
            }
        except ValueError as exc:
            return 400, {"error": "bad_request", "message": str(exc)}

    async def _dispatch_checked(self, path: str, payload: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        if path == "/a4p/v1/user-credentials/ed25519/register":
            return 200, self.a4p_server.register_ed25519_credential(payload)
        if path == "/a4p/v1/user-credentials/webauthn/register/options":
            return 200, self.a4p_server.webauthn_registration_options(payload)
        if path == "/a4p/v1/user-credentials/webauthn/register/verify":
            return 200, self.a4p_server.verify_webauthn_registration(payload)
        if path == "/a4p/v1/intent-authorizations/prepare":
            return 200, to_payload(await self.a4p_server.prepare_intent_authorization(payload))
        if path == "/a4p/v1/intent-authorizations/complete":
            return 200, to_payload(await self.a4p_server.complete_intent_authorization(payload))
        if path == "/a4p/v1/intent-tokens/verify":
            return 200, to_payload(await self.a4p_server.verify_intent_token(payload))
        if path == "/a4p/v1/operation-authorizations/prepare":
            return 200, to_payload(await self.a4p_server.prepare_operation_authorization(payload))
        if path == "/a4p/v1/operation-authorizations/complete":
            return 200, to_payload(await self.a4p_server.complete_operation_authorization(payload))
        return 404, {"error": "not_found"}

    @staticmethod
    async def _send_json(writer: asyncio.StreamWriter, status: int, payload: dict[str, Any]) -> None:
        reason = {
            200: "OK",
            400: "Bad Request",
            404: "Not Found",
            405: "Method Not Allowed",
            409: "Conflict",
            413: "Payload Too Large",
            500: "Internal Server Error",
        }.get(status, "OK")
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        writer.write(
            (
                f"HTTP/1.1 {status} {reason}\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                f"Content-Length: {len(body)}\r\n"
                "Connection: close\r\n\r\n"
            ).encode("ascii")
            + body
        )
        await writer.drain()


__all__ = ["A4PHTTPServer", "a4p_http_host", "a4p_http_port"]
