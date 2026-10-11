from __future__ import annotations

import asyncio

from a4p.http_server import (
    MAX_HEADER_COUNT,
    MAX_HEADER_LINE_SIZE,
    MAX_HEADER_SIZE,
    MAX_REQUEST_LINE_SIZE,
    A4PHTTPServer,
)


async def _raw_http_request(port: int, request: bytes) -> bytes:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(request)
    await writer.drain()
    response = await reader.read()
    writer.close()
    await writer.wait_closed()
    return response


async def _exchange(request: bytes) -> bytes:
    server = A4PHTTPServer(object(), host="127.0.0.1", port=0)  # type: ignore[arg-type]
    await server.start()
    try:
        return await _raw_http_request(server.port, request)
    finally:
        await server.stop()


def test_http_malformed_request_line_returns_400() -> None:
    """请求行不足 3 段应返回 400，而不是 500 并回吐内部异常信息。"""

    async def run() -> None:
        for request in (
            b"GET\r\n\r\n",
            b"POST /missing\r\n\r\n",
            b"\r\n",
        ):
            response = await _exchange(request)
            assert response.startswith(b"HTTP/1.1 400 Bad Request"), request
            assert b"bad_request" in response
            assert b"internal_error" not in response
            assert b"not enough values to unpack" not in response

    asyncio.run(run())


def test_http_rejects_oversized_request_line() -> None:
    """超过 MAX_REQUEST_LINE_SIZE 的请求行应返回 400。"""

    async def run() -> None:
        target = b"/" + b"a" * (MAX_REQUEST_LINE_SIZE + 1)
        response = await _exchange(
            b"POST " + target + b" HTTP/1.1\r\nHost: localhost\r\n\r\n"
        )
        assert response.startswith(b"HTTP/1.1 400 Bad Request"), response[:80]
        assert b"bad_request" in response

    asyncio.run(run())


def test_http_rejects_too_many_headers() -> None:
    """超过 MAX_HEADER_COUNT 的请求头应返回 400，避免无界内存占用。"""

    async def run() -> None:
        header_block = b"".join(
            f"X-Pad-{i}: v\r\n".encode("ascii") for i in range(MAX_HEADER_COUNT + 1)
        )
        response = await _exchange(
            b"POST /missing HTTP/1.1\r\n" + header_block + b"\r\n"
        )
        assert response.startswith(b"HTTP/1.1 400 Bad Request"), response[:80]
        assert b"bad_request" in response

    asyncio.run(run())


def test_http_rejects_oversized_header_block() -> None:
    """单行未超限但总量超过 MAX_HEADER_SIZE 的请求头应返回 400。"""

    async def run() -> None:
        line = b"X-Pad: " + b"a" * (MAX_HEADER_LINE_SIZE - 16) + b"\r\n"
        repeat = MAX_HEADER_SIZE // len(line) + 1
        response = await _exchange(
            b"POST /missing HTTP/1.1\r\n" + line * repeat + b"\r\n"
        )
        assert response.startswith(b"HTTP/1.1 400 Bad Request"), response[:80]
        assert b"bad_request" in response

    asyncio.run(run())


def test_http_normal_request_still_dispatched() -> None:
    """限制放宽不影响正常请求（未知路径仍返回 404）。"""

    async def run() -> None:
        response = await _exchange(
            b"POST /missing HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Content-Length: 2\r\n\r\n{}"
        )
        assert response.startswith(b"HTTP/1.1 404 Not Found"), response[:80]

    asyncio.run(run())
