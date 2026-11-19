from __future__ import annotations

import asyncio

import pytest

from a4p.http_server import MAX_BODY_SIZE, A4PHTTPServer
from tests.http_support import read_http_response


async def _raw_http_request(port: int, request: bytes) -> bytes:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(request)
    await writer.drain()
    response = await read_http_response(reader)
    writer.close()
    await writer.wait_closed()
    return response


def test_http_rejects_oversized_body() -> None:
    """声明超大 Content-Length 的请求应在读取 body 前被拒绝为 413。"""

    async def run() -> None:
        server = A4PHTTPServer(object(), host="127.0.0.1", port=0)  # type: ignore[arg-type]
        await server.start()
        try:
            response = await _raw_http_request(
                server.port,
                b"POST /a4p/v1/intent-authorizations/prepare HTTP/1.1\r\n"
                b"Host: localhost\r\n"
                + f"Content-Length: {MAX_BODY_SIZE + 1}\r\n\r\n".encode("ascii"),
            )
            assert response.startswith(b"HTTP/1.1 413 Payload Too Large")
            assert b"payload_too_large" in response
        finally:
            await server.stop()

    asyncio.run(run())


def test_http_accepts_body_within_limit() -> None:
    """不超过上限的请求体应正常进入分发（未知路径返回 404）。"""

    async def run() -> None:
        server = A4PHTTPServer(object(), host="127.0.0.1", port=0)  # type: ignore[arg-type]
        await server.start()
        try:
            response = await _raw_http_request(
                server.port,
                b"POST /missing HTTP/1.1\r\n"
                b"Host: localhost\r\n"
                b"Content-Length: 2\r\n\r\n{}",
            )
            assert response.startswith(b"HTTP/1.1 404 Not Found")
        finally:
            await server.stop()

    asyncio.run(run())


@pytest.mark.parametrize("value", [b"-1", b"abc", b"2, 2"])
def test_http_rejects_invalid_content_length(value: bytes) -> None:
    """负数、非数字及逗号合并的 Content-Length 均返回 400。"""

    async def run() -> None:
        server = A4PHTTPServer(object(), host="127.0.0.1", port=0)  # type: ignore[arg-type]
        await server.start()
        try:
            response = await _raw_http_request(
                server.port,
                b"POST /a4p/v1/intent-authorizations/prepare HTTP/1.1\r\n"
                b"Host: localhost\r\n"
                b"Content-Length: " + value + b"\r\n\r\n",
            )
            assert response.startswith(b"HTTP/1.1 400 Bad Request")
            assert b"bad_request" in response
        finally:
            await server.stop()

    asyncio.run(run())


@pytest.mark.parametrize(
    ("headers", "message"),
    [
        (b"Content-Length: 2\r\ncOnTeNt-LeNgTh: 3\r\n", b"duplicate Content-Length"),
        (b"Content-Length: 2\r\nContent-Length: 2\r\n", b"duplicate Content-Length"),
        (
            b"Transfer-Encoding: chunked\r\nContent-Length: 2\r\n",
            b"Transfer-Encoding is not supported",
        ),
        (b"Transfer-Encoding: chunked\r\n", b"Transfer-Encoding is not supported"),
        (b"Transfer-Encoding:\r\n", b"Transfer-Encoding is not supported"),
    ],
)
def test_http_rejects_unsupported_request_framing(headers: bytes, message: bytes) -> None:
    """请求体边界不受支持时，应在读取请求体前返回 400 并关闭连接。"""

    async def run() -> None:
        server = A4PHTTPServer(object(), host="127.0.0.1", port=0)  # type: ignore[arg-type]
        await server.start()
        try:
            response = await _raw_http_request(
                server.port,
                b"POST /missing HTTP/1.1\r\nHost: localhost\r\n" + headers + b"\r\n",
            )
            assert response.startswith(b"HTTP/1.1 400 Bad Request")
            assert b"bad_request" in response
            assert message in response
        finally:
            await server.stop()

    asyncio.run(run())
