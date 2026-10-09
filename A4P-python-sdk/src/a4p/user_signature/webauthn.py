"""WebAuthn method and user signer for A4P user authorization."""

from __future__ import annotations

import base64
import importlib
import json
import secrets
from collections.abc import Mapping
from dataclasses import replace
from functools import lru_cache
from typing import Any

from a4p.credential_store import A4PCredentialStore, UserCredentialRecord, utc_now_iso
from a4p.errors import UserCredentialNotRegisteredError
from a4p.mandate_security import derive_user_authorization_challenge
from a4p.user_signature.contracts import (
    UserSignature,
    UserSignatureContext,
    UserSigningInput,
)


def b64url_encode(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def b64url_decode(data: str) -> bytes:
    padding = "=" * (-len(data) % 4)
    return base64.urlsafe_b64decode((data + padding).encode("ascii"))


@lru_cache(maxsize=1)
def _load_webauthn() -> dict[str, Any]:
    exports = {
        "webauthn": (
            "generate_authentication_options",
            "generate_registration_options",
            "verify_authentication_response",
            "verify_registration_response",
        ),
        "webauthn.helpers": ("options_to_json",),
        "webauthn.helpers.structs": (
            "AuthenticatorSelectionCriteria",
            "PublicKeyCredentialDescriptor",
            "ResidentKeyRequirement",
            "UserVerificationRequirement",
        ),
    }
    loaded: dict[str, Any] = {}
    try:
        for module_name, attribute_names in exports.items():
            module = importlib.import_module(module_name)
            loaded.update({name: getattr(module, name) for name in attribute_names})
    except (ImportError, AttributeError) as exc:  # pragma: no cover - exercised only when dependency is absent
        raise RuntimeError(
            "Browser WebAuthn support requires the 'webauthn' package. "
            "Install the A4P SDK with its declared dependencies."
        ) from exc
    return loaded


def _options_to_payload(options: Any) -> dict[str, Any]:
    helpers = _load_webauthn()
    payload = json.loads(helpers["options_to_json"](options))
    return payload if isinstance(payload, dict) else {}


WEBAUTHN_SIGNATURE_METHOD = "webauthn"


class WebAuthnSignatureMethod:
    """A4P Server-side WebAuthn enrollment and signature method."""

    signature_method = WEBAUTHN_SIGNATURE_METHOD

    def __init__(
        self,
        credential_store: A4PCredentialStore,
        *,
        rp_id: str = "localhost",
        rp_name: str = "A4P",
        expected_origin: str = "http://localhost:8970",
    ) -> None:
        self.credential_store = credential_store
        self.rp_id = rp_id
        self.rp_name = rp_name
        self.expected_origin = expected_origin
        self._registration_challenges: dict[str, tuple[str, bytes]] = {}

    def method_policy(self) -> dict[str, Any]:
        return {"userVerification": "required"}

    def registration_options(
        self,
        *,
        user_id: str,
        user_name: str | None = None,
        user_display_name: str | None = None,
    ) -> dict[str, Any]:
        helpers = _load_webauthn()
        challenge = secrets.token_bytes(32)
        registration_request_id = f"webauthn_registration_{secrets.token_urlsafe(18)}"
        self._registration_challenges[registration_request_id] = (user_id, challenge)
        existing = [
            helpers["PublicKeyCredentialDescriptor"](id=b64url_decode(record.credentialId))
            for record in self.credential_store.list_for_user(user_id)
            if record.signatureMethod == self.signature_method
        ]
        options = helpers["generate_registration_options"](
            rp_id=self.rp_id,
            rp_name=self.rp_name,
            user_id=user_id.encode("utf-8"),
            user_name=user_name or user_id,
            user_display_name=user_display_name or user_name or user_id,
            challenge=challenge,
            authenticator_selection=helpers["AuthenticatorSelectionCriteria"](
                resident_key=helpers["ResidentKeyRequirement"].REQUIRED,
                user_verification=helpers["UserVerificationRequirement"].REQUIRED,
            ),
            exclude_credentials=existing or None,
        )
        return {
            "registrationRequestId": registration_request_id,
            "userId": user_id,
            "options": _options_to_payload(options),
        }

    def verify_registration(self, payload: dict[str, Any]) -> UserCredentialRecord:
        helpers = _load_webauthn()
        user_id = str(payload.get("userId") or "").strip()
        if not user_id:
            raise ValueError("userId missing")
        registration_request_id = str(payload.get("registrationRequestId") or "").strip()
        if not registration_request_id:
            raise ValueError("registrationRequestId missing")
        pending = self._registration_challenges.pop(registration_request_id, None)
        if pending is None:
            raise ValueError(f"No WebAuthn registration challenge: {registration_request_id}")
        pending_user_id, challenge = pending
        if pending_user_id != user_id:
            raise ValueError(
                "WebAuthn registration user mismatch: "
                f"expected '{pending_user_id}', got '{user_id}'"
            )
        credential = payload.get("credential")
        if not isinstance(credential, dict):
            raise ValueError("credential missing")
        verified = helpers["verify_registration_response"](
            credential=credential,
            expected_challenge=challenge,
            expected_rp_id=self.rp_id,
            expected_origin=self.expected_origin,
            require_user_presence=True,
            require_user_verification=True,
        )
        credential_id = b64url_encode(verified.credential_id)
        transports = []
        response = credential.get("response") if isinstance(credential.get("response"), dict) else {}
        raw_transports = response.get("transports") if isinstance(response, dict) else None
        if isinstance(raw_transports, list):
            transports = [str(item) for item in raw_transports]
        record = UserCredentialRecord(
            userId=user_id,
            credentialId=credential_id,
            signatureMethod=self.signature_method,
            publicKey={
                "format": "cose",
                "value": b64url_encode(verified.credential_public_key),
            },
            details={
                "signCount": int(verified.sign_count),
                "rpId": self.rp_id,
                "origin": self.expected_origin,
                "transports": transports,
                "aaguid": verified.aaguid,
                "fmt": str(
                    verified.fmt.value if hasattr(verified.fmt, "value") else verified.fmt
                ),
                "credentialDeviceType": str(
                    verified.credential_device_type.value
                    if hasattr(verified.credential_device_type, "value")
                    else verified.credential_device_type
                ),
                "credentialBackedUp": bool(verified.credential_backed_up),
            },
            createdAt=utc_now_iso(),
        )
        self.credential_store.save(record)
        return record

    def signing_options(
        self,
        *,
        user_id: str,
        mandate: Mapping[str, Any],
    ) -> dict[str, Any]:
        helpers = _load_webauthn()
        records = [
            record
            for record in self.credential_store.list_for_user(user_id)
            if record.signatureMethod == self.signature_method
        ]
        if not records:
            raise UserCredentialNotRegisteredError(
                user_id=user_id,
                signature_method=self.signature_method,
            )
        allow_credentials = [
            helpers["PublicKeyCredentialDescriptor"](id=b64url_decode(record.credentialId))
            for record in records
        ]
        options = helpers["generate_authentication_options"](
            rp_id=self.rp_id,
            challenge=derive_user_authorization_challenge(mandate),
            allow_credentials=allow_credentials,
            user_verification=helpers["UserVerificationRequirement"].REQUIRED,
        )
        return {
            "signatureMethod": self.signature_method,
            "methodOptions": _options_to_payload(options),
        }

    def verify(
        self,
        context: UserSignatureContext,
        signature: Mapping[str, Any],
    ) -> tuple[bool, str]:
        helpers = _load_webauthn()
        credential_id = str(signature.get("credentialId") or "").strip()
        if not credential_id:
            return False, "WebAuthn credentialId missing"
        record = self.credential_store.get(credential_id)
        if record is None:
            return False, f"WebAuthn credential not registered: {credential_id}"
        if record.signatureMethod != self.signature_method:
            return False, "WebAuthn credential signature method mismatch"
        if context.expected_user_id is not None and record.userId != context.expected_user_id:
            return False, (
                "WebAuthn credential user mismatch: "
                f"expected '{context.expected_user_id}', got '{record.userId}'"
            )
        record_rp_id = str(record.details.get("rpId") or "")
        record_origin = str(record.details.get("origin") or "")
        if record_rp_id and record_rp_id != self.rp_id:
            return False, (
                f"WebAuthn credential RP ID mismatch: expected '{self.rp_id}', "
                f"got '{record_rp_id}'"
            )
        if record_origin and record_origin != self.expected_origin:
            return False, (
                "WebAuthn credential origin mismatch: "
                f"expected '{self.expected_origin}', got '{record_origin}'"
            )
        proof = signature.get("proof")
        assertion = proof.get("assertion") if isinstance(proof, Mapping) else None
        if not isinstance(assertion, dict):
            return False, "WebAuthn assertion missing"
        try:
            verified = helpers["verify_authentication_response"](
                credential=assertion,
                expected_challenge=derive_user_authorization_challenge(
                    context.server_signed_mandate
                ),
                expected_rp_id=self.rp_id,
                expected_origin=self.expected_origin,
                credential_public_key=b64url_decode(
                    str(record.publicKey.get("value") or "")
                ),
                credential_current_sign_count=int(record.details.get("signCount") or 0),
                require_user_verification=True,
            )
        except Exception as exc:
            return False, f"WebAuthn signature invalid: {exc}"
        if not verified.user_verified:
            return False, "WebAuthn user verification missing"
        self.credential_store.save(
            replace(
                record,
                details={
                    **record.details,
                    "signCount": int(verified.new_sign_count),
                },
            )
        )
        return True, ""


class WebAuthnUserSigner:
    """User Authorizer-side signer that wraps a WebAuthn assertion."""

    signature_method = WEBAUTHN_SIGNATURE_METHOD

    def sign(
        self,
        context: UserSignatureContext,
        *,
        signing_input: UserSigningInput | None = None,
    ) -> UserSignature:
        assertion = signing_input.get("assertion") if isinstance(signing_input, dict) else None
        if not isinstance(assertion, dict):
            raise ValueError("WebAuthn assertion missing")
        credential_id = str(assertion.get("id") or "").strip()
        if not credential_id:
            raise ValueError("WebAuthn credentialId missing")
        return {
            "signatureMethod": self.signature_method,
            "credentialId": credential_id,
            "proof": {"assertion": assertion},
        }


__all__ = [
    "WebAuthnSignatureMethod",
    "WEBAUTHN_SIGNATURE_METHOD",
    "WebAuthnUserSigner",
    "b64url_decode",
    "b64url_encode",
]
