"""Operation authorization A4P primitives."""

from __future__ import annotations

import base64
import calendar
import hashlib
import hmac
import json
import time
import uuid
from copy import deepcopy
from datetime import datetime, timezone
from typing import Any
from zoneinfo import ZoneInfo

from a4p.security import signing_key_from_env


DEFAULT_MANDATE_VALIDITY_SECONDS = 300
_SERVER_SIGN_ALG = "HS256"
_USER_SIGN_ALG = "HS256"
_SERVER_KEY_ID = "server#operation-mandate-k1"
_USER_KEY_ID = "user#operation-mandate-v1"
_DEFAULT_SERVER_SIGNING_KEY = "jiuwenclaw-note-mcp-server-signing-key-dev-v1"
_DEFAULT_USER_SIGNING_KEY = "jiuwenclaw-note-mcp-user-signing-key-dev-v1"
_BEIJING_TIMEZONE = ZoneInfo("Asia/Shanghai")


def _server_signing_key() -> str:
    return signing_key_from_env(
        env_name="OPERATION_SERVER_SIGNING_KEY",
        default=_DEFAULT_SERVER_SIGNING_KEY,
        purpose="operation mandate server signing key",
    )


def _user_signing_key() -> str:
    return signing_key_from_env(
        env_name="OPERATION_USER_SIGNING_KEY",
        default=_DEFAULT_USER_SIGNING_KEY,
        purpose="operation mandate user signing key",
    )


def _canonical_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _sign_text(text: str, secret: str) -> str:
    digest = hmac.new(secret.encode("utf-8"), text.encode("utf-8"), hashlib.sha256).digest()
    return base64.urlsafe_b64encode(digest).decode("ascii").rstrip("=")


def _format_beijing_display_time(utc_iso: str) -> str:
    dt = datetime.strptime(utc_iso, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    beijing_dt = dt.astimezone(_BEIJING_TIMEZONE)
    return beijing_dt.strftime("%Y-%m-%d %H:%M:%S 北京时间")


def _format_json_value(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def _normalize_operation(operation: dict[str, Any]) -> dict[str, Any]:
    action = str(operation.get("action") or "").strip()
    if not action:
        raise ValueError("Operation action missing")
    params_raw = operation.get("params")
    if params_raw is None:
        params: dict[str, Any] = {}
    elif isinstance(params_raw, dict):
        params = deepcopy(params_raw)
    else:
        raise ValueError("Operation params must be an object")
    return {"action": action, "params": params}


def _operation_call_text(operation: dict[str, Any]) -> str:
    params = operation.get("params") if isinstance(operation.get("params"), dict) else {}
    if not params:
        return f"{operation['action']}(无参数)"
    params_text = ", ".join(f"{key}={_format_json_value(value)}" for key, value in sorted(params.items()))
    return f"{operation['action']}({params_text})"


def _operation_display_text(operation: dict[str, Any], until: str) -> str:
    local_until = _format_beijing_display_time(until)
    return f"授权执行 {_operation_call_text(operation)}（有效期至 {local_until}）"


def normalize_operation_mandate(mandate: dict[str, Any]) -> dict[str, Any]:
    normalized = deepcopy(mandate)
    operation = normalized.get("operation") if isinstance(normalized.get("operation"), dict) else {}
    normalized["operation"] = _normalize_operation(operation)
    signatures_raw = normalized.get("signatures") if isinstance(normalized.get("signatures"), dict) else {}
    normalized["signatures"] = {
        "server": dict(signatures_raw.get("server") or {}),
        "user": dict(signatures_raw.get("user") or {}),
    }
    normalized["type"] = "a4p/v1/operation-mandate"
    return normalized


def _mandate_core_payload(mandate: dict[str, Any]) -> dict[str, Any]:
    normalized = normalize_operation_mandate(mandate)
    return {
        "type": normalized.get("type", ""),
        "operationId": normalized.get("operationId", ""),
        "server": normalized.get("server", ""),
        "subject": normalized.get("subject", {}),
        "operation": normalized.get("operation", {}),
        "validTime": normalized.get("validTime", {}),
        "displayText": normalized.get("displayText", ""),
    }


def create_operation_mandate(
    *,
    operation: dict[str, Any],
    server_url: str,
    agent_id: str = "a4p-agent",
    validity_seconds: int = DEFAULT_MANDATE_VALIDITY_SECONDS,
) -> dict[str, Any]:
    operation_id = f"op_{uuid.uuid4().hex[:12]}"
    now = time.time()
    until_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now + validity_seconds))
    normalized_operation = _normalize_operation(operation)
    mandate = {
        "type": "a4p/v1/operation-mandate",
        "operationId": operation_id,
        "server": server_url,
        "subject": {"type": "agent", "id": f"agent:{agent_id}"},
        "operation": normalized_operation,
        "validTime": {
            "until": until_iso,
            "displayUntil": _format_beijing_display_time(until_iso),
            "timezone": "Asia/Shanghai",
        },
        "displayText": _operation_display_text(normalized_operation, until_iso),
        "signatures": {
            "server": {"alg": _SERVER_SIGN_ALG, "keyId": _SERVER_KEY_ID, "signature": ""},
            "user": {"alg": "", "keyId": "", "signature": ""},
        },
    }
    return sign_server_mandate(mandate)


def sign_server_mandate(mandate: dict[str, Any]) -> dict[str, Any]:
    signed = normalize_operation_mandate(mandate)
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


def sign_user_mandate(
    mandate: dict[str, Any],
    *,
    request_id: str,
    approval_method: str = "chat.user_answer",
    approved_at: str | None = None,
) -> dict[str, Any]:
    signed = normalize_operation_mandate(mandate)
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


def verify_operation_mandate(
    mandate: dict[str, Any],
    *,
    expected: dict[str, Any] | None = None,
) -> tuple[bool, str]:
    if mandate.get("type") != "a4p/v1/operation-mandate":
        return False, "Invalid mandate type"
    try:
        normalized = normalize_operation_mandate(mandate)
    except ValueError as exc:
        return False, str(exc)
    try:
        expected_operation = _normalize_operation(expected or {})
    except ValueError as exc:
        return False, str(exc)
    operation = normalized.get("operation") if isinstance(normalized.get("operation"), dict) else {}
    if operation.get("action") != expected_operation["action"]:
        return False, "Operation action mismatch"
    if operation.get("params") != expected_operation["params"]:
        return False, "Operation params mismatch"

    server_sig = normalized.get("signatures", {}).get("server", {})
    server_signature = (server_sig.get("signature") or "").strip()
    if not server_signature:
        return False, "Server signature missing"
    expected_server_signature = _sign_text(
        _canonical_json({"scope": "server", "mandate": _mandate_core_payload(normalized)}),
        _server_signing_key(),
    )
    if not hmac.compare_digest(server_signature, expected_server_signature):
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
    expected_user_signature = _sign_text(
        _canonical_json({"scope": "user", "mandate": _mandate_core_payload(normalized), "approval": approval_payload}),
        _user_signing_key(),
    )
    if not hmac.compare_digest(user_signature, expected_user_signature):
        return False, "User signature invalid"

    until_str = normalized.get("validTime", {}).get("until")
    if not until_str:
        return False, "Mandate has no validTime"
    try:
        until_ts = calendar.timegm(time.strptime(until_str, "%Y-%m-%dT%H:%M:%SZ"))
    except ValueError:
        return False, "Mandate validTime format invalid"
    if time.time() > until_ts:
        return False, f"Mandate has expired (expired at {until_str})"
    return True, ""


__all__ = [
    "DEFAULT_MANDATE_VALIDITY_SECONDS",
    "create_operation_mandate",
    "sign_server_mandate",
    "sign_user_mandate",
    "verify_operation_mandate",
    "normalize_operation_mandate",
]
