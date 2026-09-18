"""A2UI v0.8 decision compatibility over the stable IIAP contract."""

from __future__ import annotations

import json
from typing import Any

from .validation import validate_decision


def validate_v08_decision(
    value: str | dict[str, Any],
    *,
    packet: dict[str, Any] | None = None,
) -> dict[str, Any]:
    if isinstance(value, str):
        try:
            parsed = json.loads(value)
        except json.JSONDecodeError:
            parsed = value
    else:
        parsed = value
    if not isinstance(parsed, dict):
        return validate_decision(parsed, packet=packet or {})

    normalized = dict(parsed)
    legacy_topics = {"compare_options", "explain_rules", "fix_block", "save_progress"}
    if normalized.get("offerType") in legacy_topics:
        normalized["helpTopic"] = normalized["offerType"]
        normalized["offerType"] = "text_assistance"

    converted_updates: list[tuple[dict[str, Any], dict[str, Any]]] = []
    suggestion = normalized.get("updateSuggestion")
    if isinstance(suggestion, dict) and suggestion.get("kind") == "a2ui_update":
        targets = (packet or {}).get("allowedOperations", {}).get("updateTargets", [])
        target_map = {
            (item.get("originalSurfaceId"), item.get("bindingPath")): item
            for item in targets
            if isinstance(item, dict)
        }
        messages = suggestion.get("messages")
        updates = []
        conversion_failed = not isinstance(messages, list) or not 1 <= len(messages) <= 8
        for message in messages if isinstance(messages, list) else []:
            if not isinstance(message, dict) or set(message) != {"dataModelUpdate"}:
                conversion_failed = True
                break
            update = message.get("dataModelUpdate")
            if not isinstance(update, dict):
                conversion_failed = True
                break
            contents = update.get("contents")
            if (
                (update.get("surfaceId"), update.get("path")) not in target_map
                or ":" in str(update.get("surfaceId"))
            ):
                conversion_failed = True
                break
            if (
                not isinstance(contents, list)
                or len(contents) != 1
                or not isinstance(contents[0], dict)
                or contents[0].get("key") != "."
            ):
                conversion_failed = True
                break
            item = contents[0]
            value_fields = [key for key in ("valueString", "valueNumber", "valueBoolean", "valueNull") if key in item]
            if len(value_fields) != 1 or set(item) != {"key", value_fields[0]}:
                conversion_failed = True
                break
            value_field = value_fields[0]
            if value_field == "valueNull" and item[value_field] is not True:
                conversion_failed = True
                break
            value = None if value_field == "valueNull" else item[value_field]
            target = target_map[(update.get("surfaceId"), update.get("path"))]
            if target.get("componentType") == "MultipleChoice" and isinstance(value, str):
                try:
                    selections = json.loads(value)
                except json.JSONDecodeError:
                    selections = None
                if not isinstance(selections, list):
                    conversion_failed = True
                    break
                if target.get("selectionMode") == "multiple":
                    value = selections
                elif len(selections) == 1:
                    value = selections[0]
                else:
                    conversion_failed = True
                    break
            normalized_update = {
                "surfaceId": update["surfaceId"],
                "path": update["path"],
                "value": value,
            }
            updates.append(normalized_update)
            converted_updates.append((normalized_update, message))
        normalized["updateSuggestion"] = {
            "kind": "data_model_update",
            "updates": [] if conversion_failed else updates,
        }

    result = validate_decision(normalized, packet=packet or {})
    validated_updates = result.get("updateSuggestion", {}).get("updates", [])
    original_messages = [
        message
        for normalized_update, message in converted_updates
        if any(normalized_update is validated_update for validated_update in validated_updates)
    ]
    if result.get("offerType") == "update_suggestion" and original_messages:
        result["updateSuggestion"] = {
            "kind": "a2ui_update",
            "messages": original_messages,
        }
    return result
