"""Explicit Ed25519 test setup used by authorization state-machine tests."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p.user_signature.ed25519 import (
    Ed25519UserSigner,
    RegisteredEd25519Method,
    ed25519_public_jwk,
)
from a4p.credential_store import InMemoryCredentialStore
from a4p.mandate_security import mandate_identifier
from a4p.server import A4PServer as ProductionA4PServer
from a4p.user_authorizer import sign_user_mandate_with_signer


_SIGNERS_BY_MANDATE_ID: dict[str, Ed25519UserSigner] = {}


class ExplicitTestEd25519Method(RegisteredEd25519Method):
    """Signature method that enrolls ephemeral keys for test request users."""

    def __init__(self) -> None:
        super().__init__(InMemoryCredentialStore())
        self._signers_by_user: dict[str, Ed25519UserSigner] = {}

    def _signer_for_user(self, user_id: str) -> Ed25519UserSigner:
        signer = self._signers_by_user.get(user_id)
        if signer is not None:
            return signer
        private_key = Ed25519PrivateKey.generate()
        registration = self.register(
            {
                "userId": user_id,
                "publicKey": ed25519_public_jwk(private_key),
                "metadata": {"purpose": "test-only"},
            }
        )
        signer = Ed25519UserSigner(
            credential_id=registration["credential"]["credentialId"],
            private_key=private_key,
        )
        self._signers_by_user[user_id] = signer
        return signer

    def signing_options(
        self,
        *,
        user_id: str,
        mandate: Mapping[str, Any],
    ) -> dict[str, Any]:
        signer = self._signer_for_user(user_id)
        _SIGNERS_BY_MANDATE_ID[mandate_identifier(mandate)] = signer
        return super().signing_options(user_id=user_id, mandate=mandate)


class ExplicitEd25519A4PServer(ProductionA4PServer):
    """Production server configured with an explicit ephemeral test method."""

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        require_user_signature = kwargs.get("require_user_signature", True)
        if require_user_signature and "user_signature_method" not in kwargs:
            kwargs["user_signature_method"] = ExplicitTestEd25519Method()
        super().__init__(*args, **kwargs)


def sign_user_mandate(mandate: Mapping[str, Any]) -> dict[str, Any]:
    """Sign a prepared mandate using the key enrolled during its prepare call."""
    mandate_id = mandate_identifier(mandate)
    signer = _SIGNERS_BY_MANDATE_ID.get(mandate_id)
    if signer is None:
        raise ValueError(f"No explicit test signer registered for mandate: {mandate_id}")
    return sign_user_mandate_with_signer(mandate, user_signer=signer)
