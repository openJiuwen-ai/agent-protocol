"""Intent authorization A4P primitives."""

from __future__ import annotations

import base64
import calendar
import hashlib
import hmac
import json
import secrets
import time
import uuid
from copy import deepcopy
from datetime import datetime, timezone
from typing import Any

from a4p.security import signing_key_from_env


DEFAULT_INTENT_MANDATE_VALIDITY_SECONDS = 3600
_SERVER_SIGN_ALG = "HS256"
_USER_SIGN_ALG = "HS256"
_TOKEN_SIGN_ALG = "HS256"
_SERVER_KEY_ID = "server#intent-mandate-k1"
_USER_KEY_ID = "user#intent-mandate-v1"
_TOKEN_KEY_ID = "server#intent-token-v1"
_DEFAULT_SERVER_SIGNING_KEY = "jiuwenclaw-intent-gateway-signing-key-dev-v1"
_DEFAULT_USER_SIGNING_KEY = "jiuwenclaw-intent-user-signing-key-dev-v1"


def _server_signing_key() -> str:
    return signing_key_from_env(
        env_name="INTENT_SERVER_SIGNING_KEY",
        default=_DEFAULT_SERVER_SIGNING_KEY,
        purpose="intent mandate server signing key",
    )


def _user_signing_key() -> str:
    return signing_key_from_env(
        env_name="INTENT_USER_SIGNING_KEY",
        default=_DEFAULT_USER_SIGNING_KEY,
        purpose="intent mandate user signing key",
    )


def _intent_token_key_id(mandate_id: str) -> str:
    normalized_mandate_id = mandate_id.strip()
    if not normalized_mandate_id:
        return _TOKEN_KEY_ID
    return f"{_TOKEN_KEY_ID}:{normalized_mandate_id}"


def _intent_token_signing_key(mandate_id: str) -> str:
    base_key = _server_signing_key()
    normalized_mandate_id = mandate_id.strip()
    if not normalized_mandate_id:
        return base_key
    return _sign_text(
        _canonical_json({"scope": "server.intent_token.key", "mandateId": normalized_mandate_id}),
        base_key,
    )


def _canonical_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _sign_text(text: str, secret: str) -> str:
    digest = hmac.new(secret.encode("utf-8"), text.encode("utf-8"), hashlib.sha256).digest()
    return base64.urlsafe_b64encode(digest).decode("ascii").rstrip("=")


def _format_beijing_display_time(utc_iso: str) -> str:
    from zoneinfo import ZoneInfo

    beijing_tz = ZoneInfo("Asia/Shanghai")
    try:
        dt = datetime.strptime(utc_iso, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        beijing_dt = dt.astimezone(beijing_tz)
        return beijing_dt.strftime("%Y-%m-%d %H:%M:%S 北京时间")
    except ValueError:
        return utc_iso


def _normalize_params_constraint(value: Any) -> dict[str, Any] | str:
    if value == "*":
        return "*"
    if value is None:
        return {}
    if isinstance(value, dict):
        return deepcopy(value)
    raise ValueError("Action params must be an object or '*'")


def _normalize_action_spec(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError("Intent actions must be objects with name and params")
    name = str(value.get("name") or "").strip()
    if not name:
        raise ValueError("Intent action name missing")
    return {
        "name": name,
        "params": _normalize_params_constraint(value.get("params")),
    }


def _normalize_action_specs(actions: Any) -> list[dict[str, Any]]:
    if not isinstance(actions, list):
        raise ValueError("Intent actions must be a list")
    normalized = [_normalize_action_spec(item) for item in actions]
    if not normalized:
        raise ValueError("Intent actions must not be empty")
    return normalized


def _format_json_value(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def _format_params(params: dict[str, Any] | str) -> str:
    if params == "*":
        return "任意参数"
    if not params:
        return "无参数"
    return ", ".join(
        f"{key}=任意" if value == "*" else f"{key}={_format_json_value(value)}"
        for key, value in sorted(params.items())
    )


def _format_action_call(action: dict[str, Any]) -> str:
    return f"{action['name']}({_format_params(action['params'])})"


def _build_intent_display_text(
    agent_id: str,
    actions: list[dict[str, Any]],
    start_iso: str,
    end_iso: str,
) -> str:
    start_local = _format_beijing_display_time(start_iso)
    end_local = _format_beijing_display_time(end_iso)
    action_text = "；".join(_format_action_call(action) for action in actions)
    return f"授权 agent:{agent_id} 在 {start_local} 至 {end_local} 期间调用 {action_text}"


def normalize_intent_mandate(mandate: dict[str, Any]) -> dict[str, Any]:
    normalized = deepcopy(mandate)
    intent_raw = normalized.get("intent")
    intent = intent_raw if isinstance(intent_raw, dict) else {}
    signatures_raw = normalized.get("signatures")
    signatures = signatures_raw if isinstance(signatures_raw, dict) else {}
    normalized["type"] = "a4p/v1/intent-mandate"
    normalized["intent"] = {
        "actions": _normalize_action_specs(intent.get("actions")),
    }
    normalized["signatures"] = {
        "server": dict(signatures.get("server") or {}),
        "user": dict(signatures.get("user") or {}),
    }
    return normalized


def _mandate_core_payload(mandate: dict[str, Any]) -> dict[str, Any]:
    normalized = normalize_intent_mandate(mandate)
    return {
        "type": normalized.get("type", ""),
        "mandateId": normalized.get("mandateId", ""),
        "server": normalized.get("server", ""),
        "subject": normalized.get("subject", {}),
        "intent": normalized.get("intent", {}),
        "validTime": normalized.get("validTime", {}),
        "displayText": normalized.get("displayText", ""),
    }


def _intent_token_core_payload(token: dict[str, Any]) -> dict[str, Any]:
    intent_raw = token.get("intent", {})
    intent = intent_raw if isinstance(intent_raw, dict) else {}
    return {
        "type": token.get("type", ""),
        "tokenId": token.get("tokenId", ""),
        "mandateId": token.get("mandateId", ""),
        "subject": token.get("subject", {}),
        "user": token.get("user", {}),
        "intent": {"actions": _normalize_action_specs(intent.get("actions"))},
        "issuedAt": token.get("issuedAt", ""),
        "expireAt": token.get("expireAt", ""),
        "nonce": token.get("nonce", ""),
    }


def create_intent_mandate(
    *,
    server: str,
    agent_id: str,
    actions: list[dict[str, Any]],
    validity_seconds: int = DEFAULT_INTENT_MANDATE_VALIDITY_SECONDS,
    subject_type: str = "agent",
) -> dict[str, Any]:
    mandate_id = f"mdt_{uuid.uuid4().hex[:12]}"
    now_ts = time.time()
    start_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now_ts))
    end_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now_ts + validity_seconds))
    normalized_actions = _normalize_action_specs(actions)
    display_text = _build_intent_display_text(
        agent_id,
        normalized_actions,
        start_iso,
        end_iso,
    )
    mandate_dict = {
        "type": "a4p/v1/intent-mandate",
        "mandateId": mandate_id,
        "server": server,
        "subject": {"type": subject_type, "id": f"{subject_type}:{agent_id}"},
        "intent": {"actions": normalized_actions},
        "validTime": {"start": start_iso, "end": end_iso},
        "displayText": display_text,
        "signatures": {
            "server": {"alg": _SERVER_SIGN_ALG, "keyId": _SERVER_KEY_ID, "signature": ""},
            "user": {"alg": "", "keyId": "", "signature": ""},
        },
    }
    return sign_server_mandate(mandate_dict)


def sign_server_mandate(mandate: dict[str, Any]) -> dict[str, Any]:
    signed = normalize_intent_mandate(mandate)
    signed.setdefault("signatures", {})
    signed["signatures"]["server"] = {
        "alg": _SERVER_SIGN_ALG,
        "keyId": _SERVER_KEY_ID,
        "signature": _sign_text(
            _canonical_json({"scope": "server", "mandate": _mandate_core_payload(signed)}),
            _server_signing_key(),
        ),
    }
    signed.setdefault("signatures", {}).setdefault("user", {"alg": "", "keyId": "", "signature": ""})
    return signed


def sign_user_intent_mandate(
    mandate: dict[str, Any],
    *,
    request_id: str,
    approval_method: str = "chat.user_answer",
    approved_at: str | None = None,
) -> dict[str, Any]:
    signed = normalize_intent_mandate(mandate)
    approved_at = approved_at or time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    approval_payload = {
        "requestId": request_id,
        "approvalMethod": approval_method,
        "approvedAt": approved_at,
    }
    signature = _sign_text(
        _canonical_json({"scope": "user", "mandate": _mandate_core_payload(signed), "approval": approval_payload}),
        _user_signing_key(),
    )
    signed.setdefault("signatures", {})
    signed["signatures"]["user"] = {
        "alg": _USER_SIGN_ALG,
        "keyId": _USER_KEY_ID,
        "requestId": request_id,
        "approvalMethod": approval_method,
        "approvedAt": approved_at,
        "signature": signature,
    }
    return signed


def issue_intent_token(
    mandate: dict[str, Any],
    *,
    user_id: str,
) -> dict[str, Any]:
    valid, err = verify_intent_mandate(mandate)
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

    token = {
        "type": "a4p/v1/intent-token",
        "tokenId": secrets.token_hex(16),
        "mandateId": mandate_id,
        "subject": {
            "type": str(subject.get("type") or "agent").strip() or "agent",
            "id": str(subject.get("id") or "").strip(),
        },
        "user": {"id": user_id.strip() or "user:unknown"},
        "intent": {"actions": _normalize_action_specs(intent.get("actions"))},
        "issuedAt": issued_at,
        "expireAt": expire_at,
        "nonce": secrets.token_urlsafe(16),
        "signature": "",
        "alg": _TOKEN_SIGN_ALG,
        "keyId": _intent_token_key_id(mandate_id),
    }
    token["signature"] = _sign_text(
        _canonical_json({"scope": "server.intent_token", "token": _intent_token_core_payload(token)}),
        _intent_token_signing_key(mandate_id),
    )
    return token


def params_match_intent_token(
    token: dict[str, Any],
    *,
    action: str,
    params: dict[str, Any] | None = None,
) -> tuple[bool, str]:
    expected_action = action.strip()
    if not expected_action:
        return False, "Expected action missing"
    actual_params = dict(params or {})
    intent_raw = token.get("intent")
    intent = intent_raw if isinstance(intent_raw, dict) else {}
    try:
        actions = _normalize_action_specs(intent.get("actions"))
    except ValueError as exc:
        return False, str(exc)

    for allowed in actions:
        if allowed["name"] != expected_action:
            continue
        constraint = allowed["params"]
        if constraint == "*":
            return True, ""
        if not isinstance(constraint, dict):
            return False, "Intent params constraint invalid"
        extra = sorted(set(actual_params) - set(constraint))
        if extra:
            return False, f"Unexpected params for action '{expected_action}': {extra}"
        for key, expected_value in constraint.items():
            if key not in actual_params:
                return False, f"Required param '{key}' missing for action '{expected_action}'"
            if expected_value != "*" and actual_params[key] != expected_value:
                return False, f"Param '{key}' mismatch for action '{expected_action}'"
        return True, ""

    return False, f"Action '{expected_action}' not in token actions: {[item['name'] for item in actions]}"


def verify_intent_token(
    token: dict[str, Any],
    *,
    action: str,
    params: dict[str, Any] | None = None,
    expected_agent_id: str | None = None,
    expected_user_id: str | None = None,
) -> tuple[bool, str]:
    if token.get("type") != "a4p/v1/intent-token":
        return False, "Invalid token type"
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
        expected_signature = _sign_text(
            _canonical_json({"scope": "server.intent_token", "token": _intent_token_core_payload(token)}),
            _intent_token_signing_key(mandate_id),
        )
    except ValueError as exc:
        return False, str(exc)
    if not hmac.compare_digest(signature, expected_signature):
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
    if expected_user_id:
        actual_user_id = str(user.get("id") or "").strip()
        if actual_user_id != expected_user_id:
            return False, f"Token user mismatch: expected '{expected_user_id}', got '{actual_user_id}'"
    return params_match_intent_token(token, action=action, params=params)


def verify_intent_mandate(mandate: dict[str, Any], *, expected_server: str | None = None) -> tuple[bool, str]:
    if mandate.get("type") != "a4p/v1/intent-mandate":
        return False, "Invalid mandate type"
    try:
        normalized = normalize_intent_mandate(mandate)
    except ValueError as exc:
        return False, str(exc)
    server_sig = normalized.get("signatures", {}).get("server", {})
    server_signature = (server_sig.get("signature") or "").strip()
    if not server_signature:
        return False, "Server signature missing"
    expected_server_sig = _sign_text(
        _canonical_json({"scope": "server", "mandate": _mandate_core_payload(normalized)}),
        _server_signing_key(),
    )
    if not hmac.compare_digest(server_signature, expected_server_sig):
        return False, "Server signature invalid"

    user_sig = normalized.get("signatures", {}).get("user", {})
    user_signature = (user_sig.get("signature") or "").strip()
    if not user_signature:
        return False, "User signature missing"
    approval_payload = {
        "requestId": user_sig.get("requestId", ""),
        "approvalMethod": user_sig.get("approvalMethod", ""),
        "approvedAt": user_sig.get("approvedAt", ""),
    }
    if not approval_payload["requestId"] or not approval_payload["approvedAt"]:
        return False, "User approval metadata missing"
    expected_user_sig = _sign_text(
        _canonical_json({"scope": "user", "mandate": _mandate_core_payload(normalized), "approval": approval_payload}),
        _user_signing_key(),
    )
    if not hmac.compare_digest(user_signature, expected_user_sig):
        return False, "User signature invalid"

    valid_time = normalized.get("validTime", {})
    start_str = valid_time.get("start", "")
    end_str = valid_time.get("end", "")
    if not start_str or not end_str:
        return False, "Mandate has no validTime"
    try:
        start_ts = calendar.timegm(time.strptime(start_str, "%Y-%m-%dT%H:%M:%SZ"))
        end_ts = calendar.timegm(time.strptime(end_str, "%Y-%m-%dT%H:%M:%SZ"))
    except ValueError:
        return False, "Mandate validTime format invalid"
    now = time.time()
    if now < start_ts:
        return False, "Mandate not yet valid"
    if now > end_ts:
        return False, "Mandate has expired"
    if expected_server is not None:
        mandate_server = str(normalized.get("server", "") or "").strip()
        normalized_expected = expected_server.strip()
        if mandate_server != normalized_expected:
            return False, f"Mandate server mismatch: expected '{normalized_expected}', got '{mandate_server}'"
    return True, ""


__all__ = [
    "DEFAULT_INTENT_MANDATE_VALIDITY_SECONDS",
    "create_intent_mandate",
    "sign_server_mandate",
    "sign_user_intent_mandate",
    "issue_intent_token",
    "verify_intent_token",
    "params_match_intent_token",
    "verify_intent_mandate",
    "normalize_intent_mandate",
]
