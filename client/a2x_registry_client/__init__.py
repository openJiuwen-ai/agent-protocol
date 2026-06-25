"""A2X Registry client SDK.

Public entry points:

- :class:`A2XRegistryClient` — synchronous client
- :class:`AsyncA2XRegistryClient` — asynchronous client (mirrors ``A2XRegistryClient``)

Response dataclasses and the exception hierarchy are re-exported for
``except``/``isinstance`` use.
"""

from .async_client import AsyncA2XRegistryClient
from .auth import (
    DEFAULT_CONFIG_PATH,
    read_cli_token,
    remove_cli_token,
    resolve_credentials,
    write_cli_token,
)
from .client import A2XRegistryClient
from .errors import (
    A2XAuthenticationError,
    A2XAuthorizationError,
    A2XConnectionError,
    A2XError,
    A2XHeartbeatNotSupportedError,
    A2XHTTPError,
    A2XTTLOutOfRangeError,
    A2XTTLRequiredError,
    NotFoundError,
    NotOwnedError,
    ServerError,
    UnexpectedServiceTypeError,
    UserConfigDeregisterForbiddenError,  # deprecated alias
    UserConfigServiceImmutableError,
    ValidationError,
)
from .models import (
    AgentDetail,
    DatasetCreateResponse,
    DatasetDeleteResponse,
    DeregisterResponse,
    PatchResponse,
    PrincipalCreateResponse,
    RegisterResponse,
    Reservation,
)

__all__ = [
    "A2XRegistryClient",
    "AsyncA2XRegistryClient",
    # Errors
    "A2XError",
    "A2XConnectionError",
    "A2XHTTPError",
    "A2XAuthenticationError",
    "A2XAuthorizationError",
    "A2XHeartbeatNotSupportedError",
    "A2XTTLRequiredError",
    "A2XTTLOutOfRangeError",
    "NotFoundError",
    "ValidationError",
    "UserConfigServiceImmutableError",
    "UserConfigDeregisterForbiddenError",
    "UnexpectedServiceTypeError",
    "ServerError",
    "NotOwnedError",
    # Models
    "DatasetCreateResponse",
    "DatasetDeleteResponse",
    "PrincipalCreateResponse",
    "RegisterResponse",
    "PatchResponse",
    "DeregisterResponse",
    "AgentDetail",
    "Reservation",
    # Credential helpers
    "DEFAULT_CONFIG_PATH",
    "resolve_credentials",
    "read_cli_token",
    "write_cli_token",
    "remove_cli_token",
]

__version__ = "0.3.3"
