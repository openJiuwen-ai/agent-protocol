"""Decision and safe update validation."""

from __future__ import annotations

import json
import math
import re
from typing import Any

VALID_DECISIONS = {"offer_help", "no_intervention", "defer"}
OFFER_STYLES = {"text_assistance": "inline_card", "update_suggestion": "inline_card"}


def offer_style(offer_type: object) -> str:
    """Host-owned presentation style for an offer.

    `uiStyle` is a display hint, not a safety boundary: the real boundary is the
    `updateSuggestion` value allowlist. Asking the model to infer it added a field whose only
    effect was a two-valued show/hide gate, and a single invented word (e.g. `inline_hint`)
    voided an otherwise correct decision. Derive it from `offerType` instead, so presentation
    can never invalidate a decision.
    """
    return OFFER_STYLES.get(str(offer_type), "none")
VALID_TOPICS = {"compare_options", "explain_rules", "fix_block", "save_progress"}


def default_no_intervention() -> dict[str, Any]:
    return {"decision": "no_intervention", "reason": "invalid_or_insufficient_iiap_decision", "offerType": "none", "uiStyle": "none", "message": ""}


def _extract_json_object(text: str) -> dict[str, Any] | None:
    """Pull the first complete JSON object out of model output.

    Models routinely wrap the decision in prose ("The user switched options…\\n{...}") or bold
    labels. Requiring a bare object turns those answers into a silent `no_intervention`, which is
    indistinguishable from a genuine negative and quietly disables the whole capability. Extraction
    is strictly structural: a candidate must parse on its own, so prose containing braces cannot
    fabricate a decision, and the wire contract stays unchanged.
    """
    decoder = json.JSONDecoder()
    for start, char in enumerate(text):
        if char != "{":
            continue
        prefix = text[:start]
        if any(character in prefix for character in "{}`"):
            continue
        try:
            parsed, end = decoder.raw_decode(text, start)
        except (json.JSONDecodeError, ValueError):
            continue
        if isinstance(parsed, dict) and not text[end:].strip():
            return parsed
    return None


def _fenced_body(text: str) -> str | None:
    match = re.fullmatch(r"```(?:json)?[ \t]*\r?\n(?P<body>.*?)\r?\n```[ \t]*", text, re.IGNORECASE | re.DOTALL)
    return match.group("body").strip() if match else None


def _parse(value: object) -> dict[str, Any] | None:
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        fenced = _fenced_body(text)
        if fenced is not None:
            try:
                value = json.loads(fenced)
            except json.JSONDecodeError:
                return None
            return value if isinstance(value, dict) else None
        try:
            value = json.loads(text)
        except json.JSONDecodeError:
            value = _extract_json_object(text)
    return value if isinstance(value, dict) else None


def describe_decision_parse_status(value: object) -> str:
    """Stable, content-free classification for diagnostics (never logs model text)."""
    if isinstance(value, dict):
        return "object"
    if not isinstance(value, str):
        return "not_text"
    text = value.strip()
    if not text:
        return "empty"
    if _parse(value) is None:
        return "no_json_object"
    return "prose_wrapped" if not text.startswith("{") else "recovered"


def describe_decision_rejection(value: object) -> str:
    """Why a parsed decision was discarded, without revealing model text.

    A rejected decision and a genuine negative are the same envelope downstream, so the only
    way to tell "the model offered help and we threw it away" from "the model chose not to help"
    is to classify the rejection at its source. Field names and observed *categories* only —
    never the offending value itself, which can be user-derived.
    """
    parsed = _parse(value)
    if parsed is None:
        return "not_a_json_object"
    decision = parsed.get("decision")
    if decision not in VALID_DECISIONS:
        return "invalid_decision_value" if isinstance(decision, str) else "missing_decision"
    reason = parsed.get("reason")
    if not isinstance(reason, str):
        return "missing_reason" if reason is None else "invalid_reason_type"
    if len(reason) > 1024:
        return "reason_too_long"
    message = parsed.get("message")
    if not isinstance(message, str):
        return "missing_message" if message is None else "invalid_message_type"
    if len(message) > 2048:
        return "message_too_long"
    if decision != "offer_help":
        return "none"
    offer = parsed.get("offerType")
    if not isinstance(offer, str):
        return "missing_offer_type"
    if offer not in OFFER_STYLES:
        return "invalid_offer_type"
    if offer == "update_suggestion":
        return "invalid_update_payload" if parsed.get("updateSuggestion") is not None else "missing_update_payload"
    return "other"


def _safe_updates(value: object, packet: dict[str, Any]) -> list[dict[str, Any]]:
    if not isinstance(value, dict) or value.get("kind") != "data_model_update":
        return []
    targets = packet.get("allowedOperations", {}).get("updateTargets", [])
    updates = value.get("updates")
    if not isinstance(updates, list) or not 1 <= len(updates) <= 8:
        return []
    safe: list[dict[str, Any]] = []
    seen_targets: set[tuple[str, str]] = set()
    for update in updates:
        if not isinstance(update, dict) or set(update) - {"surfaceId", "surfaceInstanceId", "path", "value"}:
            return []
        surface_id, path = update.get("surfaceId"), update.get("path")
        target = next((candidate for candidate in targets if isinstance(candidate, dict)
                       and candidate.get("originalSurfaceId") == surface_id
                       and candidate.get("bindingPath") == path), None)
        if not isinstance(surface_id, str) or ":" in surface_id or not target:
            return []
        if not isinstance(path, str):
            return []
        target_key = (surface_id, path)
        if target_key in seen_targets:
            return []
        if update.get("surfaceInstanceId") not in {None, packet.get("surfaceInstanceId")}:
            return []
        update_value = update.get("value")
        allowed_values = target.get("allowedValues")
        if not isinstance(allowed_values, list) or not _safe_update_value(update_value, target, allowed_values):
            return []
        seen_targets.add(target_key)
        safe.append(update)
    return safe


def _safe_update_value(value: object, target: dict[str, Any], allowed_values: list[object]) -> bool:
    """选择基数只是值的形状约束，不是授权前置条件。

    已授权但未声明上限的多选字段仍然只接受白名单内的去重子集，只是不做集合大小比较；
    因此这里不要求 `maxAllowedSelections` 必须存在。
    """
    selection_mode = target.get("selectionMode")
    limit = target.get("maxAllowedSelections")
    if selection_mode is None and limit is not None:
        return False
    if limit is not None and (
        not isinstance(limit, int) or isinstance(limit, bool) or limit < 1
        or (selection_mode == "single" and limit != 1)
        or (selection_mode == "multiple" and limit < 2)
    ):
        return False
    if isinstance(value, list):
        if selection_mode != "multiple" or len(value) > 16:
            return False
        if limit is not None and len(value) > limit:
            return False
        if any(not _is_json_scalar(item) for item in value):
            return False
        if any(any(_same_json_scalar(item, previous) for previous in value[:index])
               for index, item in enumerate(value)):
            return False
        return all(any(_same_json_scalar(item, allowed) for allowed in allowed_values) for item in value)
    if target.get("selectionMode") == "multiple" or not _is_json_scalar(value):
        return False
    return any(_same_json_scalar(item, value) for item in allowed_values)


def _is_json_scalar(value: object) -> bool:
    return value is None or isinstance(value, (str, bool)) or (
        isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)
    )


def _same_json_scalar(left: object, right: object) -> bool:
    if left is None or right is None:
        return left is right
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    if isinstance(left, (int, float)) or isinstance(right, (int, float)):
        return (isinstance(left, (int, float)) and not isinstance(left, bool)
                and isinstance(right, (int, float)) and not isinstance(right, bool)
                and left == right)
    return isinstance(left, str) and isinstance(right, str) and left == right


def _infer_help_topic(packet: dict[str, Any]) -> str:
    pattern_types = {
        pattern.get("type")
        for pattern in packet.get("patterns", [])
        if isinstance(pattern, dict)
    }
    if "validation_failure_sequence" in pattern_types:
        return "fix_block"
    if "selection_reversal" in pattern_types:
        return "compare_options"
    return "explain_rules"


def validate_decision(value: object, *, packet: dict[str, Any]) -> dict[str, Any]:
    parsed = _parse(value)
    if not parsed or parsed.get("decision") not in VALID_DECISIONS:
        return default_no_intervention()
    reason = parsed.get("reason")
    message = parsed.get("message")
    if (not isinstance(reason, str) or len(reason) > 1024
            or not isinstance(message, str) or len(message) > 2048):
        return default_no_intervention()
    decision = parsed["decision"]
    offer = parsed.get("offerType", "none")
    if decision != "offer_help":
        return {
            "decision": decision, "reason": reason,
            "offerType": "none", "uiStyle": "none", "message": message,
        }
    if offer not in OFFER_STYLES:
        return default_no_intervention()
    result = {
        "decision": decision, "reason": reason,
        "offerType": offer, "uiStyle": offer_style(offer), "message": message,
    }
    declared_topic = parsed.get("helpTopic") if parsed.get("helpTopic") in VALID_TOPICS else None
    if offer == "update_suggestion":
        updates = _safe_updates(parsed.get("updateSuggestion"), packet)
        if updates:
            result["updateSuggestion"] = {"kind": "data_model_update", "updates": updates}
        else:
            result["offerType"] = "text_assistance"
    if result["offerType"] == "text_assistance":
        result["helpTopic"] = declared_topic or _infer_help_topic(packet)
    result["uiStyle"] = offer_style(result["offerType"])
    return result


def create_decision_envelope(
    value: object,
    *,
    packet: dict[str, Any],
    decision_id: str,
) -> dict[str, Any]:
    """Wrap a validated model payload with server-owned correlation metadata."""
    packet_id = packet.get("packetId")
    surface_instance_id = packet.get("surfaceInstanceId")
    if not isinstance(packet_id, str) or not packet_id:
        raise ValueError("packetId is required")
    if not isinstance(surface_instance_id, str) or not surface_instance_id:
        raise ValueError("surfaceInstanceId is required")
    if not decision_id:
        raise ValueError("decision_id is required")
    return {
        "type": "iiap.decision",
        "iiapVersion": "0.1",
        "decisionId": decision_id,
        "packetId": packet_id,
        "surfaceInstanceId": surface_instance_id,
        "payload": validate_decision(value, packet=packet),
    }
