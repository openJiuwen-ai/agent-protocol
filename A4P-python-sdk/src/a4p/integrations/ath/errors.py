from __future__ import annotations


class ATHError(Exception):
    """A Gateway or integration error with a stable application-facing code."""

    def __init__(self, code: str, message: str, *, http_status: int | None = None) -> None:
        super().__init__(message)
        self.code = code
        self.http_status = http_status
