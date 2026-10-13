"""Shared helpers for the raw-socket HTTP integration tests.

The A4P HTTP server answers exactly one request per connection and closes it
(``Connection: close``), so a healthy response arrives immediately and is then
followed by EOF.  ``StreamReader.read()`` blocks until EOF, which means a
regression that leaves the socket open turns a failing test into a hung one:
the suite would sit there until the CI-wide timeout instead of reporting a
failure.  Every raw-socket helper therefore reads through :func:`read_http_response`,
which bounds the wait.
"""

from __future__ import annotations

import asyncio

# Generous for a loopback round-trip, small enough that a regression costs one
# bounded failure instead of a CI-wide hang.
RESPONSE_READ_TIMEOUT = 10.0


async def read_http_response(
    reader: asyncio.StreamReader,
    timeout: float = RESPONSE_READ_TIMEOUT,
) -> bytes:
    """Read the full response, failing fast instead of hanging on a stalled peer.

    ``asyncio.wait_for`` cancels the pending ``read()`` when the deadline passes
    and re-raises ``TimeoutError``; that is a test *failure*, which is what we
    want -- but the default message ("TimeoutError") says nothing about the
    server keeping the connection open, so translate it into an assertion.
    """
    try:
        return await asyncio.wait_for(reader.read(), timeout=timeout)
    except asyncio.TimeoutError:
        raise AssertionError(
            f"no response within {timeout:g}s; the server never closed the connection"
        ) from None
