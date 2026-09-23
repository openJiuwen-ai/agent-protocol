from __future__ import annotations

import asyncio

from a4p.http_server import MAX_BODY_SIZE, A4PHTTPServer


async def _raw_http_request(port: int, request: bytes) -> bytes:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(request)
    await writer.drain()
    response = await reader.read()
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
