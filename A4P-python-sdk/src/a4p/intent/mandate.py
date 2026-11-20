"""Intent authorization A4P primitives."""

from __future__ import annotations

import calendar
import json
import secrets
import time
from collections.abc import Mapping
from copy import deepcopy
from datetime import datetime, timezone
from typing import Any, Callable, cast

from a4p.mandate_security import canonical_json, server_signed_mandate_without_user_signature
from a4p.security import (
    ed25519_sign_text,
    ed25519_verify_text,
)
from a4p.intent.scope import (
    normalize_action_specs,
    normalize_execution_policy,
    normalize_intent_scope,
)
from a4p.intent.signing import (
    INTENT_MANDATE_SERVER_KEY_ID,
    INTENT_SERVER_SIGN_ALGORITHM,
    intent_server_signing_key,
    intent_server_trusted_key,
)
from a4p.types import IntentMandate, JsonDict
from a4p.user_signature import (
    A4PUserSignatureMethod,
    UserSignatureContext,
    verify_user_signature,
)


DEFAULT_INTENT_MANDATE_VALIDITY_SECONDS = 3600
IntentDisplayTextRenderer = Callable[[dict[str, Any]], str]


def _format_beijing_display_time(utc_iso: str) -> str:
    from zoneinfo import ZoneInfo

    beijing_tz = ZoneInfo("Asia/Shanghai")
    try:
        dt = datetime.strptime(utc_iso, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        beijing_dt = dt.astimezone(beijing_tz)
        return beijing_dt.strftime("%Y-%m-%d %H:%M:%S 北京时间")
    except ValueError:
        return utc_iso


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
    execution_policy: dict[str, Any] | None = None,
) -> str:
    start_local = _format_beijing_display_time(start_iso)
    end_local = _format_beijing_display_time(end_iso)
    action_text = "；".join(_format_action_call(action) for action in actions)
    policy_text = f"，最多 {execution_policy['maxExecutions']} 次" if execution_policy else ""
    return f"授权 agent:{agent_id} 在 {start_local} 至 {end_local} 期间调用 {action_text}{policy_text}"


def _render_custom_display_text(
    renderer: IntentDisplayTextRenderer,
    mandate: Mapping[str, Any],
) -> str:
    display_text = renderer(deepcopy(dict(mandate)))
    if not isinstance(display_text, str):
        raise ValueError("Intent display text renderer must return a string")
    return display_text


def normalize_intent_mandate(mandate: Mapping[str, Any]) -> IntentMandate:
    normalized = deepcopy(dict(mandate))
    signatures_raw = normalized.get("signatures")
    signatures = signatures_raw if isinstance(signatures_raw, dict) else {}
    normalized["type"] = "a4p/v1/intent-mandate"
    normalized["intent"] = normalize_intent_scope(normalized.get("intent"))
    normalized["signatures"] = {
        "server": dict(signatures.get("server") or {}),
        "user": dict(signatures.get("user") or {}),
    }
    user_authorization = normalized.get("userAuthorization")
    normalized["userAuthorization"] = dict(user_authorization) if isinstance(user_authorization, dict) else {}
    return cast(IntentMandate, normalized)


def intent_mandate_core_payload(mandate: Mapping[str, Any]) -> dict[str, Any]:
    normalized = normalize_intent_mandate(mandate)
    return {
        "type": normalized.get("type", ""),
        "mandateId": normalized.get("mandateId", ""),
        "server": normalized.get("server", ""),
        "subject": normalized.get("subject", {}),
        "intent": normalized.get("intent", {}),
        "validTime": normalized.get("validTime", {}),
        "userAuthorization": normalized.get("userAuthorization", {}),
        "displayText": normalized.get("displayText", ""),
    }


def intent_user_signature_context(
    mandate: Mapping[str, Any],
    *,
    expected_user_id: str | None = None,
) -> UserSignatureContext:
    normalized = normalize_intent_mandate(mandate)
    user_authorization = normalized.get("userAuthorization", {})
    required = user_authorization.get("required")
    if not isinstance(required, bool):
        raise ValueError("userAuthorization.required missing")
    signature_method = str(user_authorization.get("signatureMethod") or "").strip()
    if required and not signature_method:
        raise ValueError("userAuthorization.signatureMethod missing")
    return UserSignatureContext(
        mandate_type="a4p/v1/intent-mandate",
        server_signed_mandate=server_signed_mandate_without_user_signature(normalized),
        signature_method=signature_method,
        expected_user_id=expected_user_id,
    )


def create_intent_mandate(
    *,
    server: str,
    agent_id: str,
    actions: list[dict[str, Any]],
    execution_policy: dict[str, Any] | None = None,
    validity_seconds: int = DEFAULT_INTENT_MANDATE_VALIDITY_SECONDS,
    subject_type: str = "agent",
    agent_public_key: dict[str, Any] | None = None,
    require_user_signature: bool = True,
    user_signature_method: str | None = None,
    user_signature_method_policy: dict[str, Any] | None = None,
    display_text_renderer: IntentDisplayTextRenderer | None = None,
) -> IntentMandate:
    signature_method = str(user_signature_method or "").strip()
    if require_user_signature and not signature_method:
        raise ValueError("userSignatureMethod missing")
    mandate_id = f"mdt_{secrets.token_urlsafe(32)}"
    now_ts = time.time()
    start_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now_ts))
    end_iso = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now_ts + validity_seconds))
    normalized_actions = normalize_action_specs(actions)
    normalized_policy = normalize_execution_policy(execution_policy)
    subject: JsonDict = {"type": subject_type, "id": f"{subject_type}:{agent_id}"}
    if agent_public_key is not None:
        subject["agentKey"] = deepcopy(agent_public_key)
    intent_scope: dict[str, Any] = {"actions": normalized_actions}
    if normalized_policy is not None:
        intent_scope["executionPolicy"] = normalized_policy
    mandate_dict: IntentMandate = {
        "type": "a4p/v1/intent-mandate",
        "mandateId": mandate_id,
        "server": server,
        "subject": subject,
        "intent": intent_scope,
        "validTime": {"start": start_iso, "end": end_iso},
        "userAuthorization": (
            {
                "required": True,
                "signatureMethod": signature_method,
                "methodPolicy": deepcopy(user_signature_method_policy or {}),
            }
            if require_user_signature
            else {"required": False}
        ),
        "displayText": _build_intent_display_text(
            agent_id,
            normalized_actions,
            start_iso,
            end_iso,
            normalized_policy,
        ),
        "signatures": {
            "server": {
                "alg": INTENT_SERVER_SIGN_ALGORITHM,
                "keyId": INTENT_MANDATE_SERVER_KEY_ID,
                "signature": "",
            },
            "user": {},
        },
    }
    if display_text_renderer is not None:
        mandate_dict["displayText"] = _render_custom_display_text(display_text_renderer, mandate_dict)
    return sign_server_mandate(mandate_dict)


def sign_server_mandate(mandate: Mapping[str, Any]) -> IntentMandate:
    signed = normalize_intent_mandate(mandate)
    signed["signatures"]["server"] = {
        "alg": INTENT_SERVER_SIGN_ALGORITHM,
        "keyId": INTENT_MANDATE_SERVER_KEY_ID,
        "signature": ed25519_sign_text(
            canonical_json({"scope": "server", "mandate": intent_mandate_core_payload(signed)}),
            intent_server_signing_key(),
        ),
    }
    return signed


def verify_intent_mandate(
    mandate: dict[str, Any],
    *,
    expected_server: str | None = None,
    expected_user_id: str | None = None,
    require_user_signature: bool = True,
    user_signature_method: A4PUserSignatureMethod | None = None,
) -> tuple[bool, str]:
    if mandate.get("type") != "a4p/v1/intent-mandate":
        return False, "Invalid mandate type"
    try:
        normalized = normalize_intent_mandate(mandate)
    except ValueError as exc:
        return False, str(exc)
    server_sig = normalized.get("signatures", {}).get("server", {})
    server_alg = str(server_sig.get("alg") or "").strip()
    if server_alg != INTENT_SERVER_SIGN_ALGORITHM:
        return False, (
            "Server signature alg mismatch: "
            f"expected '{INTENT_SERVER_SIGN_ALGORITHM}', got '{server_alg}'"
        )
    server_signature = (server_sig.get("signature") or "").strip()
    if not server_signature:
        return False, "Server signature missing"
    server_payload = canonical_json(
        {"scope": "server", "mandate": intent_mandate_core_payload(normalized)}
    )
    if not ed25519_verify_text(
        server_payload,
        server_signature,
        intent_server_signing_key().public_key(),
    ):
        return False, "Server signature invalid"

    user_sig = normalized.get("signatures", {}).get("user", {})
    try:
        context = intent_user_signature_context(
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
    "IntentDisplayTextRenderer",
    "create_intent_mandate",
    "intent_mandate_core_payload",
    "intent_server_trusted_key",
    "intent_user_signature_context",
    "sign_server_mandate",
    "verify_intent_mandate",
    "normalize_intent_mandate",
    "normalize_execution_policy",
    "normalize_intent_scope",
]
