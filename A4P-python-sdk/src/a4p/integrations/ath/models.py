from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from datetime import datetime
from types import MappingProxyType
from typing import ClassVar, Mapping
from urllib.parse import urlsplit

_SEGMENT = r"[A-Za-z0-9_~-][A-Za-z0-9._~-]*"


def _http_url(value: str, *, allow_http: bool, origin_only: bool = False) -> bool:
    if not value.isascii() or any(character.isspace() or ord(character) < 32 for character in value):
        return False
    if "\\" in value or (origin_only and ("?" in value or "#" in value)):
        return False
    try:
        parts = urlsplit(value)
        port = parts.port
        return (
            parts.scheme in ({"https", "http"} if allow_http else {"https"})
            and bool(parts.hostname)
            and parts.username is None
            and (port is None or port > 0)
            and (not origin_only or parts.path in {"", "/"})
        )
    except ValueError:
        return False


@dataclass(frozen=True)
class ATHOperation:
    """A fixed Gateway proxy route and the scopes required to call it."""

    method: str
    path: str
    scopes: frozenset[str]

    def __post_init__(self) -> None:
        method = self.method.upper()
        scopes = frozenset(self.scopes)
        if method not in {"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE"}:
            raise ValueError("Unsupported ATH operation method")
        if not re.fullmatch(rf"(?:/{_SEGMENT})+", self.path):
            raise ValueError("ATH operation path must be a fixed relative API path such as /products")
        if isinstance(self.scopes, str) or not scopes or any(
            not isinstance(scope, str) or not re.fullmatch(r"[!-~]+", scope) for scope in scopes
        ):
            raise ValueError("ATH operations require non-empty scope names")
        object.__setattr__(self, "method", method)
        object.__setattr__(self, "scopes", scopes)


@dataclass(frozen=True)
class ATHConfig:
    """Configuration for one Gateway, Agent, provider, and set of operations."""

    gateway_url: str
    agent_id: str
    private_key: str | bytes = field(repr=False)
    developer_id: str
    developer_name: str
    purpose: str
    provider_id: str
    operations: Mapping[str, ATHOperation]
    key_id: str = "default"
    timeout: float = 30.0
    authorization_ttl: float = 600.0
    allow_insecure_http: bool = False

    def __post_init__(self) -> None:
        if not _http_url(self.gateway_url, allow_http=self.allow_insecure_http, origin_only=True):
            raise ValueError("ATH Gateway URL must be an HTTPS origin (HTTP requires explicit opt-in)")
        if not self.agent_id or not self.agent_id.isascii() or not re.fullmatch(r"[!-~]+", self.agent_id):
            raise ValueError("ATH agent_id must be a non-empty canonical URI")
        if not urlsplit(self.agent_id).scheme:
            raise ValueError("ATH agent_id must be a non-empty canonical URI")
        if not re.fullmatch(_SEGMENT, self.provider_id):
            raise ValueError("Invalid ATH provider ID")
        if not all((self.private_key, self.developer_id, self.developer_name, self.purpose, self.key_id)):
            raise ValueError("ATH signing key, developer, purpose, and key ID are required")
        if not all(math.isfinite(value) and value > 0 for value in (self.timeout, self.authorization_ttl)):
            raise ValueError("ATH timeouts must be finite and positive")
        operations = dict(self.operations)
        if not operations or any(
            not name or not isinstance(operation, ATHOperation) for name, operation in operations.items()
        ):
            raise ValueError("At least one named ATH operation is required")
        object.__setattr__(self, "gateway_url", self.gateway_url.rstrip("/"))
        object.__setattr__(self, "operations", MappingProxyType(operations))

    @property
    def scopes(self) -> frozenset[str]:
        return frozenset().union(*(operation.scopes for operation in self.operations.values()))


@dataclass(frozen=True)
class GatewaySession:
    """A Gateway login token and the expected user ID from the trusted host."""

    user_id: str
    token: str = field(repr=False)

    def __post_init__(self) -> None:
        if not self.user_id or not re.fullmatch(r"[!-~]+", self.token):
            raise ValueError("A Gateway user ID and login token are required")


@dataclass(frozen=True)
class Registration:
    client_id: str
    approved_scopes: frozenset[str]
    expires_at: datetime


@dataclass(frozen=True)
class AuthorizationRequired:
    status: ClassVar[str] = "interaction_required"
    transaction_id: str
    authorization_url: str
    expires_at: datetime


@dataclass(frozen=True)
class Authorized:
    status: ClassVar[str] = "authorized"
    scopes: frozenset[str]
    expires_at: datetime


AuthorizationResult = AuthorizationRequired | Authorized
