from __future__ import annotations

import asyncio
import socket
import time

import pytest

from a4p.http_server import (
    MAX_HEADER_COUNT,
    MAX_HEADER_LINE_SIZE,
    MAX_HEADER_SIZE,
    MAX_REQUEST_LINE_SIZE,
    A4PHTTPServer,
)
from tests.http_support import read_http_response


async def _raw_http_request(port: int, request: bytes) -> bytes:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(request)
    await writer.drain()
    response = await read_http_response(reader)
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


async def _raw_http_request_then_eof(port: int, request: bytes) -> bytes:
    """发送请求后立即关闭写端，模拟「声明的 Content-Length 比实际发的多」。"""
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(request)
    await writer.drain()
    if writer.can_write_eof():
        writer.write_eof()
    response = await read_http_response(reader)
    writer.close()
    await writer.wait_closed()
    return response


async def _exchange_then_eof(request: bytes) -> bytes:
    server = A4PHTTPServer(object(), host="127.0.0.1", port=0)  # type: ignore[arg-type]
    await server.start()
    try:
        return await _raw_http_request_then_eof(server.port, request)
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


def test_http_rejects_oversized_single_header_line() -> None:
    """单行超过 MAX_HEADER_LINE_SIZE 的请求头应返回 400。

    ``test_http_rejects_oversized_header_block`` 刻意把每行控制在单行上限以内，
    只覆盖总量上限；这里补上「单行上限」这条此前从未被执行的分支。行长度需超过
    ``MAX_HEADER_LINE_SIZE`` 但低于 asyncio 流的默认上限（64 KiB），否则
    ``readline()`` 会先抛 ``ValueError`` 走 "request headers too long" 分支，
    单行上限路径仍然验证不到。
    """

    async def run() -> None:
        line = b"X-Long: " + b"a" * (MAX_HEADER_LINE_SIZE + 64) + b"\r\n"
        assert len(line) > MAX_HEADER_LINE_SIZE
        assert len(line) < 64 * 1024, "需低于 asyncio 流上限，避免走 ValueError 分支"
        response = await _exchange(
            b"POST /missing HTTP/1.1\r\n" + line + b"\r\n"
        )
        assert response.startswith(b"HTTP/1.1 400 Bad Request"), response[:80]
        assert b"bad_request" in response
        assert b"request header line too long" in response

    asyncio.run(run())


def test_http_incomplete_body_returns_400() -> None:
    """Content-Length 声明大于实际发送量时应返回 400，而不是 500。

    ``readexactly`` 在对端提前关闭时会抛 ``asyncio.IncompleteReadError``，它不是
    ``_BadRequest``/``_PayloadTooLarge``，会落到最外层通用分支变成 500，并把异常
    文本回吐给对端 —— 与「畸形输入返回 400 且不泄漏异常文本」的目标相反。
    """

    async def run() -> None:
        request = (
            b"POST /missing HTTP/1.1\r\n"
            b"Host: localhost\r\n"
            b"Content-Length: 100\r\n"
            b"\r\n"
            b"{}"
        )
        response = await _exchange_then_eof(request)
        assert response.startswith(b"HTTP/1.1 400 Bad Request"), response[:120]
        assert b"bad_request" in response
        assert b"incomplete body" in response
        assert b"internal_error" not in response
        assert b"IncompleteReadError" not in response

    asyncio.run(run())


def test_read_http_response_times_out_instead_of_hanging() -> None:
    """对端建立连接后一直不返回数据时，读取必须快速失败而不是挂死。

    服务端「一个连接只处理一个请求」并以 ``Connection: close`` 收尾，所以
    ``reader.read()`` 在正常情况下会立刻拿到响应后遇到 EOF。但若回归导致连接不被
    关闭，无保护的 ``read()`` 会永久阻塞，用例只会挂起直到 CI 全局超时。这里用一条
    真的 TCP 连接（accept 后一个字节都不写）验证超时会转化成失败。
    """

    async def run() -> None:
        loop = asyncio.get_running_loop()
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.setblocking(False)
        accepted: list[socket.socket] = []
        try:
            reader, writer = await asyncio.open_connection(
                "127.0.0.1", listener.getsockname()[1]
            )
            try:
                # 接受连接但保持静默：服务端「不回响应也不关连接」的回归现场。
                conn, _ = await loop.sock_accept(listener)
                accepted.append(conn)
                started = time.monotonic()
                with pytest.raises(AssertionError, match="never closed the connection"):
                    await read_http_response(reader, timeout=0.2)
                elapsed = time.monotonic() - started
                assert elapsed < 5.0, f"超时未生效，读取耗时 {elapsed:.1f}s"
            finally:
                writer.close()
                await writer.wait_closed()
        finally:
            for conn in accepted:
                conn.close()
            listener.close()

    asyncio.run(run())
