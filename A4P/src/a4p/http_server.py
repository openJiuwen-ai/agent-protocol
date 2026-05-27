"""Minimal local HTTP server for A4P endpoints."""

from __future__ import annotations

import asyncio
import json
import logging
import os
from typing import Any

from a4p.server import A4PServer
from a4p.types import to_payload


logger = logging.getLogger(__name__)


def is_a4p_http_server_enabled() -> bool:
    value = (os.getenv("A4P_SERVER_ENABLED") or "0").strip().lower()
    return value in {"1", "true", "yes", "on"}


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
        self.port = port or a4p_http_port()
        self._server: asyncio.AbstractServer | None = None

    async def start(self) -> None:
        if self._server is not None:
            return
        self._server = await asyncio.start_server(self._handle_client, self.host, self.port)
        sockets = self._server.sockets or []
        if sockets:
            self.port = int(sockets[0].getsockname()[1])
        logger.info("[A4PHTTPServer] started: http://%s:%s", self.host, self.port)

    async def stop(self) -> None:
        if self._server is None:
            return
        self._server.close()
        await self._server.wait_closed()
        self._server = None
        logger.info("[A4PHTTPServer] stopped")

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            request_line = await reader.readline()
            if not request_line:
                return
            method, path, _version = request_line.decode("iso-8859-1").strip().split(" ", 2)
            headers: dict[str, str] = {}
            while True:
                line = await reader.readline()
                if line in {b"\r\n", b"\n", b""}:
                    break
                key, _, value = line.decode("iso-8859-1").partition(":")
                headers[key.strip().lower()] = value.strip()
            content_length = int(headers.get("content-length") or "0")
            body = await reader.readexactly(content_length) if content_length else b"{}"
            payload = json.loads(body.decode("utf-8") or "{}")
            if method.upper() != "POST":
                await self._send_json(writer, 405, {"error": "method_not_allowed"})
                return
            status, response = await self._dispatch(path, payload if isinstance(payload, dict) else {})
            await self._send_json(writer, status, response)
        except Exception as exc:
            logger.exception("[A4PHTTPServer] request failed: %s", exc)
            await self._send_json(writer, 500, {"error": "internal_error", "message": str(exc)})
        finally:
            writer.close()
            await writer.wait_closed()

    async def _dispatch(self, path: str, payload: dict[str, Any]) -> tuple[int, dict[str, Any]]:
        if path == "/a4p/v1/intent-authorizations":
            return 200, to_payload(await self.a4p_server.authorize_intent(payload))
        if path == "/a4p/v1/intent-tokens/verify":
            return 200, to_payload(await self.a4p_server.verify_intent_token(payload))
        if path == "/a4p/v1/operation-authorizations":
            return 200, to_payload(await self.a4p_server.authorize_operation(payload))
        return 404, {"error": "not_found"}

    @staticmethod
    async def _send_json(writer: asyncio.StreamWriter, status: int, payload: dict[str, Any]) -> None:
        reason = {
            200: "OK",
            404: "Not Found",
            405: "Method Not Allowed",
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


__all__ = ["A4PHTTPServer", "a4p_http_host", "a4p_http_port", "is_a4p_http_server_enabled"]
