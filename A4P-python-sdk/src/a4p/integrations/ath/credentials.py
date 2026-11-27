from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol

from .models import Authorized


@dataclass(frozen=True)
class RegistrationKey:
    gateway_url: str
    gateway_user_id: str
    agent_id: str


@dataclass(frozen=True)
class RegistrationCredentials:
    client_id: str
    client_secret: str = field(repr=False)


class RegistrationStore(Protocol):
    """Store ATH client credentials securely, keyed separately from A4P credentials."""

    async def get(self, key: RegistrationKey) -> RegistrationCredentials | None: ...

    async def put(self, key: RegistrationKey, credentials: RegistrationCredentials) -> None: ...


class MemoryRegistrationStore:
    def __init__(self) -> None:
        self._registrations: dict[RegistrationKey, RegistrationCredentials] = {}

    async def get(self, key: RegistrationKey) -> RegistrationCredentials | None:
        return self._registrations.get(key)

    async def put(self, key: RegistrationKey, credentials: RegistrationCredentials) -> None:
        self._registrations[key] = credentials


@dataclass(frozen=True)
class AccessGrant:
    authorization: Authorized
    token: str = field(repr=False)


@dataclass(frozen=True)
class PendingRevocation:
    credentials: RegistrationCredentials
    token: str = field(repr=False)
