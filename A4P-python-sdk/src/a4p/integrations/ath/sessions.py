from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone

from .models import AuthorizationRequired


def utcnow() -> datetime:
    return datetime.now(timezone.utc)


@dataclass(frozen=True)
class AuthorizationTransaction:
    interaction: AuthorizationRequired
    application_session: str
    gateway_session_id: str = field(repr=False)
    registration_id: str


class AuthorizationSessions:
    """Process-local transaction state. The owning client serializes access."""

    def __init__(self) -> None:
        self._pending: dict[str, AuthorizationTransaction] = {}

    def add(self, transaction: AuthorizationTransaction) -> None:
        now = utcnow()
        self._pending = {
            identifier: item for identifier, item in self._pending.items() if item.interaction.expires_at > now
        }
        self._pending[transaction.interaction.transaction_id] = transaction

    def for_session(self, application_session: str) -> AuthorizationTransaction | None:
        now = utcnow()
        return next(
            (
                transaction for transaction in self._pending.values()
                if transaction.application_session == application_session and transaction.interaction.expires_at > now
            ),
            None,
        )

    def get(self, identifier: str) -> AuthorizationTransaction | None:
        return self._pending.get(identifier)

    def discard(self, identifier: str) -> None:
        self._pending.pop(identifier, None)

    def clear(self) -> None:
        self._pending.clear()
