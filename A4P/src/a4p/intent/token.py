"""Intent-token issuance, verification, and scope matching."""

from __future__ import annotations

import calendar
import secrets
import time
from collections.abc import Mapping
from copy import deepcopy
from typing import Any

from a4p.intent.mandate import (
    normalize_intent_mandate,
    verify_intent_mandate,
)
from a4p.intent.scope import normalize_intent_scope, params_match_intent_scope
from a4p.intent.signing import intent_server_signing_key
from a4p.mandate_security import canonical_json
from a4p.security import ed25519_sign_text, ed25519_verify_text
from a4p.types import IntentToken
from a4p.user_signature import A4PUserSignatureMethod


_TOKEN_SIGN_ALG = "EdDSA"
_TOKEN_KEY_ID = "server#intent-token-v1"


def _intent_token_key_id(mandate_id: str) -> str:
    normalized_mandate_id = mandate_id.strip()
    if not normalized_mandate_id:
        return _TOKEN_KEY_ID
    return f"{_TOKEN_KEY_ID}:{normalized_mandate_id}"


def _intent_token_core_payload(token: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "type": token.get("type", ""),
        "tokenId": token.get("tokenId", ""),
        "mandateId": token.get("mandateId", ""),
        "subject": token.get("subject", {}),
        "user": token.get("user", {}),
        "intent": normalize_intent_scope(token.get("intent")),
        "issuedAt": token.get("issuedAt", ""),
        "expireAt": token.get("expireAt", ""),
        "nonce": token.get("nonce", ""),
    }


def issue_intent_token(
    mandate: Mapping[str, Any],
    *,
    user_id: str,
    verified_mandate: bool = False,
    require_user_signature: bool = True,
    user_signature_method: A4PUserSignatureMethod | None = None,
) -> IntentToken:
    if not verified_mandate:
        valid, err = verify_intent_mandate(
            dict(mandate),
            require_user_signature=require_user_signature,
            user_signature_method=user_signature_method,
        )
        if not valid:
            raise ValueError(f"invalid intent mandate: {err}")

    normalized = normalize_intent_mandate(mandate)
    mandate_id = str(normalized.get("mandateId") or "").strip()
    if not mandate_id:
        raise ValueError("mandateId missing")
    subject = normalized.get("subject") if isinstance(normalized.get("subject"), dict) else {}
    intent = normalized.get("intent") if isinstance(normalized.get("intent"), dict) else {}
    valid_time = normalized.get("validTime") if isinstance(normalized.get("validTime"), dict) else {}

    issued_at = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    expire_at = str(valid_time.get("end") or "").strip()
    if not expire_at:
        raise ValueError("mandate validTime.end missing")

    token: IntentToken = {
        "type": "a4p/v1/intent-token",
        "tokenId": secrets.token_hex(16),
        "mandateId": mandate_id,
        "subject": {
            "type": str(subject.get("type") or "agent").strip() or "agent",
            "id": str(subject.get("id") or "").strip(),
        },
        "user": {"id": user_id.strip() or "user:unknown"},
        "intent": normalize_intent_scope(intent),
        "issuedAt": issued_at,
        "expireAt": expire_at,
        "nonce": secrets.token_urlsafe(16),
        "signature": "",
        "alg": _TOKEN_SIGN_ALG,
        "keyId": _intent_token_key_id(mandate_id),
    }
    if isinstance(subject.get("agentKey"), dict):
        token["subject"]["agentKey"] = deepcopy(subject["agentKey"])
    token["signature"] = ed25519_sign_text(
        canonical_json({"scope": "server.intent_token", "token": _intent_token_core_payload(token)}),
        intent_server_signing_key(),
    )
    return token


def params_match_intent_token(
    token: dict[str, Any],
    *,
    action: str,
    params: dict[str, Any] | None = None,
) -> tuple[bool, str]:
    intent_raw = token.get("intent")
    intent = intent_raw if isinstance(intent_raw, dict) else {}
    return params_match_intent_scope(intent, action=action, params=params)


def verify_intent_token(
    token: dict[str, Any],
    *,
    action: str,
    params: dict[str, Any] | None = None,
    expected_agent_id: str | None = None,
    expected_user_id: str | None = None,
    expected_agent_key_id: str | None = None,
) -> tuple[bool, str]:
    if token.get("type") != "a4p/v1/intent-token":
        return False, "Invalid token type"
    token_alg = str(token.get("alg") or "").strip()
    if token_alg != _TOKEN_SIGN_ALG:
        return False, f"Token alg mismatch: expected '{_TOKEN_SIGN_ALG}', got '{token_alg}'"
    signature = str(token.get("signature") or "").strip()
    if not signature:
        return False, "Token signature missing"
    mandate_id = str(token.get("mandateId") or "").strip()
    if not mandate_id:
        return False, "Token mandateId missing"
    key_id = str(token.get("keyId") or "").strip()
    expected_key_id = _intent_token_key_id(mandate_id)
    if key_id != expected_key_id:
        return False, f"Token keyId mismatch: expected '{expected_key_id}', got '{key_id}'"
    try:
        token_payload = canonical_json(
            {"scope": "server.intent_token", "token": _intent_token_core_payload(token)}
        )
    except ValueError as exc:
        return False, str(exc)
    if not ed25519_verify_text(
        token_payload,
        signature,
        intent_server_signing_key().public_key(),
    ):
        return False, "Token signature invalid"
    expire_at = str(token.get("expireAt") or "").strip()
    if not expire_at:
        return False, "Token expireAt missing"
    try:
        expire_ts = calendar.timegm(time.strptime(expire_at, "%Y-%m-%dT%H:%M:%SZ"))
    except ValueError:
        return False, "Token expireAt format invalid"
    if time.time() > expire_ts:
        return False, "Token expired"
    subject = token.get("subject") if isinstance(token.get("subject"), dict) else {}
    user = token.get("user") if isinstance(token.get("user"), dict) else {}
    if expected_agent_id:
        actual_agent_id = str(subject.get("id") or "").strip()
        if actual_agent_id != expected_agent_id:
            return False, f"Token subject mismatch: expected '{expected_agent_id}', got '{actual_agent_id}'"
    if expected_agent_key_id:
        agent_key = subject.get("agentKey") if isinstance(subject.get("agentKey"), dict) else {}
        actual_key_id = str(agent_key.get("kid") or agent_key.get("keyId") or "").strip()
        if actual_key_id != expected_agent_key_id:
            return False, f"Token agent key mismatch: expected '{expected_agent_key_id}', got '{actual_key_id}'"
    if expected_user_id:
        actual_user_id = str(user.get("id") or "").strip()
        if actual_user_id != expected_user_id:
            return False, f"Token user mismatch: expected '{expected_user_id}', got '{actual_user_id}'"
    return params_match_intent_token(token, action=action, params=params)


__all__ = [
    "issue_intent_token",
    "params_match_intent_token",
    "verify_intent_token",
]
