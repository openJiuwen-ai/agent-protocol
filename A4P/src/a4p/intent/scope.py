"""Intent action scope normalization and matching."""

from __future__ import annotations

import fnmatch
from copy import deepcopy
from typing import Any


def normalize_params_constraint(value: Any) -> dict[str, Any] | str:
    if value == "*":
        return "*"
    if value is None:
        return {}
    if isinstance(value, dict):
        return deepcopy(value)
    raise ValueError("Action params must be an object or '*'")


def normalize_action_spec(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError("Intent actions must be objects with name and params")
    name = str(value.get("name") or "").strip()
    if not name:
        raise ValueError("Intent action name missing")
    allow_extra_raw = value.get("allowExtraParams", False)
    if not isinstance(allow_extra_raw, bool):
        raise ValueError("Intent action allowExtraParams must be a boolean")
    return {
        "name": name,
        "params": normalize_params_constraint(value.get("params")),
        "allowExtraParams": allow_extra_raw,
    }


def normalize_action_specs(actions: Any) -> list[dict[str, Any]]:
    if not isinstance(actions, list):
        raise ValueError("Intent actions must be a list")
    normalized = [normalize_action_spec(item) for item in actions]
    if not normalized:
        raise ValueError("Intent actions must not be empty")
    return normalized


def _positive_int(value: Any, *, field_name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{field_name} must be a positive integer")
    return value


def normalize_execution_policy(policy: Any) -> dict[str, Any] | None:
    if policy is None:
        return None
    if not isinstance(policy, dict):
        raise ValueError("Intent executionPolicy must be an object")
    if policy.get("maxExecutions") is None:
        raise ValueError("executionPolicy must include maxExecutions")
    return {
        "maxExecutions": _positive_int(
            policy.get("maxExecutions"),
            field_name="executionPolicy.maxExecutions",
        )
    }


def normalize_intent_scope(intent: Any) -> dict[str, Any]:
    raw = intent if isinstance(intent, dict) else {}
    normalized: dict[str, Any] = {"actions": normalize_action_specs(raw.get("actions"))}
    if "executionPolicy" in raw:
        policy = normalize_execution_policy(raw.get("executionPolicy"))
        if policy is None:
            raise ValueError("executionPolicy must include maxExecutions")
        normalized["executionPolicy"] = policy
    return normalized


def _param_value_matches(actual_value: Any, expected_value: Any) -> bool:
    if expected_value == "*":
        return True
    if isinstance(expected_value, str) and isinstance(actual_value, str):
        return fnmatch.fnmatchcase(actual_value, expected_value)
    return actual_value == expected_value


def params_match_intent_scope(
    intent: dict[str, Any],
    *,
    action: str,
    params: dict[str, Any] | None = None,
) -> tuple[bool, str]:
    expected_action = action.strip()
    if not expected_action:
        return False, "Expected action missing"
    actual_params = dict(params or {})
    try:
        actions = normalize_action_specs(intent.get("actions"))
    except ValueError as exc:
        return False, str(exc)

    first_mismatch_reason: str | None = None
    for allowed in actions:
        if allowed["name"] != expected_action:
            continue
        constraint = allowed["params"]
        if constraint == "*":
            return True, ""
        if not isinstance(constraint, dict):
            if first_mismatch_reason is None:
                first_mismatch_reason = "Intent params constraint invalid"
            continue
        allow_extra = bool(allowed.get("allowExtraParams", False))
        mismatch_reason: str | None = None
        if not allow_extra:
            extra = sorted(set(actual_params) - set(constraint))
            if extra:
                mismatch_reason = (
                    f"Unexpected params for action '{expected_action}': {extra}"
                )
        if mismatch_reason is None:
            for key, expected_value in constraint.items():
                if key not in actual_params:
                    mismatch_reason = (
                        f"Required param '{key}' missing for action '{expected_action}'"
                    )
                    break
                if not _param_value_matches(actual_params[key], expected_value):
                    mismatch_reason = (
                        f"Param '{key}' mismatch for action '{expected_action}'"
                    )
                    break
        if mismatch_reason is None:
            return True, ""
        if first_mismatch_reason is None:
            first_mismatch_reason = mismatch_reason

    if first_mismatch_reason is not None:
        return False, first_mismatch_reason
    return False, (
        f"Action '{expected_action}' not in token actions: "
        f"{[item['name'] for item in actions]}"
    )


__all__ = [
    "normalize_action_spec",
    "normalize_action_specs",
    "normalize_execution_policy",
    "normalize_intent_scope",
    "normalize_params_constraint",
    "params_match_intent_scope",
]
