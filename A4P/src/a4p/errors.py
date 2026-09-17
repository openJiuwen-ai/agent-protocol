"""Stable A4P protocol errors shared by services and transports."""

from __future__ import annotations


class A4PProtocolError(ValueError):
    """A fail-closed protocol error with a stable code and HTTP status."""

    def __init__(self, message: str, *, code: str, http_status: int = 400) -> None:
        super().__init__(message)
        self.code = code
        self.http_status = http_status


class SignatureMethodNotEnabledError(A4PProtocolError):
    def __init__(self, *, expected: str | None, requested: str) -> None:
        super().__init__(
            f"Signature method '{requested}' is not enabled; configured method is {expected!r}",
            code="SIGNATURE_METHOD_NOT_ENABLED",
            http_status=409,
        )


class CredentialKeyConflictError(A4PProtocolError):
    def __init__(self) -> None:
        super().__init__(
            "The public key is already registered to another user",
            code="CREDENTIAL_KEY_CONFLICT",
            http_status=409,
        )


class UserCredentialNotRegisteredError(A4PProtocolError):
    def __init__(self, *, user_id: str, signature_method: str) -> None:
        super().__init__(
            f"No '{signature_method}' credential is registered for user: {user_id}",
            code="USER_CREDENTIAL_NOT_REGISTERED",
        )


__all__ = [
    "A4PProtocolError",
    "CredentialKeyConflictError",
    "SignatureMethodNotEnabledError",
    "UserCredentialNotRegisteredError",
]
