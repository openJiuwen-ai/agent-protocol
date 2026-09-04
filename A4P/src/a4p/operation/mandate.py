"""Operation authorization A4P primitives."""

from __future__ import annotations

import calendar
import json
import secrets
import time
from collections.abc import Mapping
from copy import deepcopy
from datetime import datetime, timezone
from typing import Any, Callable, cast
from zoneinfo import ZoneInfo

from a4p.mandate_security import canonical_json, server_signed_mandate_without_user_signature
from a4p.operation.signing import (
    OPERATION_MANDATE_SERVER_KEY_ID,
    OPERATION_SERVER_SIGN_ALGORITHM,
    operation_server_signing_key,
    operation_server_trusted_key,
)
from a4p.security import (
    ed25519_sign_text,
    ed25519_verify_text,
)
from a4p.types import JsonDict, OperationMandate
from a4p.user_signature import (
    A4PUserSignatureMethod,
    UserSignatureContext,
    verify_user_signature,
)


DEFAULT_MANDATE_VALIDITY_SECONDS = 300
_BEIJING_TIMEZONE = ZoneInfo("Asia/Shanghai")
OperationDisplayTextRenderer = Callable[[dict[str, Any]], str]


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


def _render_custom_display_text(
    renderer: OperationDisplayTextRenderer,
    mandate: Mapping[str, Any],
) -> str:
    display_text = renderer(deepcopy(dict(mandate)))
    if not isinstance(display_text, str):
        raise ValueError("Operation display text renderer must return a string")
    return display_text


def normalize_operation_mandate(mandate: Mapping[str, Any]) -> OperationMandate:
    normalized = deepcopy(dict(mandate))
    operation = normalized.get("operation") if isinstance(normalized.get("operation"), dict) else {}
    normalized["operation"] = _normalize_operation(operation)
    signatures_raw = normalized.get("signatures") if isinstance(normalized.get("signatures"), dict) else {}
    normalized["signatures"] = {
        "server": dict(signatures_raw.get("server") or {}),
        "user": dict(signatures_raw.get("user") or {}),
    }
    user_authorization = normalized.get("userAuthorization")
    normalized["userAuthorization"] = dict(user_authorization) if isinstance(user_authorization, dict) else {}
    normalized["type"] = "a4p/v1/operation-mandate"
    return cast(OperationMandate, normalized)


def operation_mandate_core_payload(mandate: Mapping[str, Any]) -> dict[str, Any]:
    normalized = normalize_operation_mandate(mandate)
    return {
        "type": normalized.get("type", ""),
        "operationId": normalized.get("operationId", ""),
        "server": normalized.get("server", ""),
        "subject": normalized.get("subject", {}),
        "operation": normalized.get("operation", {}),
        "validTime": normalized.get("validTime", {}),
        "userAuthorization": normalized.get("userAuthorization", {}),
        "displayText": normalized.get("displayText", ""),
    }


def operation_user_signature_context(
    mandate: Mapping[str, Any],
    *,
    expected_user_id: str | None = None,
) -> UserSignatureContext:
    normalized = normalize_operation_mandate(mandate)
    user_authorization = normalized.get("userAuthorization", {})
    required = user_authorization.get("required")
    if not isinstance(required, bool):
        raise ValueError("userAuthorization.required missing")
    signature_method = str(user_authorization.get("signatureMethod") or "").strip()
    if required and not signature_method:
        raise ValueError("userAuthorization.signatureMethod missing")
    return UserSignatureContext(
        mandate_type="a4p/v1/operation-mandate",
        server_signed_mandate=server_signed_mandate_without_user_signature(normalized),
        signature_method=signature_method,
        expected_user_id=expected_user_id,
    )


def create_operation_mandate(
    *,
    operation: dict[str, Any],
    server_url: str,
    agent_id: str = "a4p-agent",
    validity_seconds: int = DEFAULT_MANDATE_VALIDITY_SECONDS,
    agent_public_key: dict[str, Any] | None = None,
    require_user_signature: bool = True,
    user_signature_method: str | None = None,
    user_signature_method_policy: dict[str, Any] | None = None,
    display_text_renderer: OperationDisplayTextRenderer | None = None,
) -> OperationMandate:
    signature_method = str(user_signature_method or "").strip()
    if require_user_signature and not signature_method:
        raise ValueError("userSignatureMethod missing")
    operation_id = f"op_{secrets.token_urlsafe(32)}"
    now = time.time()
    until_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now + validity_seconds))
    normalized_operation = _normalize_operation(operation)
    subject: JsonDict = {"type": "agent", "id": f"agent:{agent_id}"}
    if agent_public_key is not None:
        subject["agentKey"] = deepcopy(agent_public_key)
    mandate: OperationMandate = {
        "type": "a4p/v1/operation-mandate",
        "operationId": operation_id,
        "server": server_url,
        "subject": subject,
        "operation": normalized_operation,
        "validTime": {
            "until": until_iso,
            "displayUntil": _format_beijing_display_time(until_iso),
            "timezone": "Asia/Shanghai",
        },
        "userAuthorization": (
            {
                "required": True,
                "signatureMethod": signature_method,
                "methodPolicy": deepcopy(user_signature_method_policy or {}),
            }
            if require_user_signature
            else {"required": False}
        ),
        "displayText": _operation_display_text(normalized_operation, until_iso),
        "signatures": {
            "server": {
                "alg": OPERATION_SERVER_SIGN_ALGORITHM,
                "keyId": OPERATION_MANDATE_SERVER_KEY_ID,
                "signature": "",
            },
            "user": {},
        },
    }
    if display_text_renderer is not None:
        mandate["displayText"] = _render_custom_display_text(display_text_renderer, mandate)
    return sign_server_mandate(mandate)


def sign_server_mandate(mandate: Mapping[str, Any]) -> OperationMandate:
    signed = normalize_operation_mandate(mandate)
    signed["signatures"]["server"] = {
        "alg": OPERATION_SERVER_SIGN_ALGORITHM,
        "keyId": OPERATION_MANDATE_SERVER_KEY_ID,
        "signature": ed25519_sign_text(
            canonical_json({"scope": "server", "mandate": operation_mandate_core_payload(signed)}),
            operation_server_signing_key(),
        ),
    }
    return signed


def verify_operation_mandate_for_completion(
    mandate: dict[str, Any],
    *,
    expected: dict[str, Any] | None = None,
    expected_user_id: str | None = None,
    require_user_signature: bool = True,
    user_signature_method: A4PUserSignatureMethod | None = None,
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
    server_alg = str(server_sig.get("alg") or "").strip()
    if server_alg != OPERATION_SERVER_SIGN_ALGORITHM:
        return False, (
            "Server signature alg mismatch: "
            f"expected '{OPERATION_SERVER_SIGN_ALGORITHM}', got '{server_alg}'"
        )
    server_signature = (server_sig.get("signature") or "").strip()
    if not server_signature:
        return False, "Server signature missing"
    server_payload = canonical_json(
        {"scope": "server", "mandate": operation_mandate_core_payload(normalized)}
    )
    if not ed25519_verify_text(
        server_payload,
        server_signature,
        operation_server_signing_key().public_key(),
    ):
        return False, "Server signature invalid"

    user_sig = normalized.get("signatures", {}).get("user", {})
    try:
        context = operation_user_signature_context(
            normalized,
            expected_user_id=expected_user_id,
        )
    except ValueError as exc:
        return False, str(exc)
    valid, reason = verify_user_signature(
        context,
        user_sig,
        method=user_signature_method,
        require_user_signature=require_user_signature,
    )
    if not valid:
        return False, reason

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
    "OperationDisplayTextRenderer",
    "create_operation_mandate",
    "operation_mandate_core_payload",
    "operation_server_trusted_key",
    "sign_server_mandate",
    "normalize_operation_mandate",
    "operation_user_signature_context",
    "verify_operation_mandate_for_completion",
]
