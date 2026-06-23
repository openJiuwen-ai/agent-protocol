# coding: utf-8
"""ACP evaluator."""
from __future__ import annotations

import json
from typing import Any, Callable, Dict, Optional

from .client import ACPClient
from .models import DecisionType, EvaluationResult
from .utils import extract_json_object, ensure_list, parse_agent_context_json


class ACPEvaluator:
    def __init__(
        self,
        acp_client: Optional[ACPClient] = None,
        model: Optional[str] = None,
        temperature: float = 0.0,
        max_retry: int = 2,
    ) -> None:
        self._client = acp_client
        self._model = model
        self._temperature = temperature
        self._max_retry = max_retry

    @staticmethod
    def _coerce_agent_context(agent_context: Dict[str, Any] | str) -> Dict[str, Any]:
        if isinstance(agent_context, dict):
            return agent_context
        if isinstance(agent_context, str):
            return parse_agent_context_json(agent_context)
        return {}

    @staticmethod
    def _build_eval_payload(agent_context: Dict[str, Any] | str) -> Dict[str, Any]:
        context = ACPEvaluator._coerce_agent_context(agent_context)
        return {
            "ItemstateUpdates": ensure_list(context.get("ItemstateUpdates")),
            "KeyInformation": ensure_list(context.get("KeyInformation")),
        }

    @staticmethod
    def _normalize_decision(value: str) -> DecisionType:
        normalized = value.strip().lower()
        if normalized == "pass":
            return "pass"
        if normalized == "force_pass":
            return "force_pass"
        return "retry"

    def evaluate(
        self,
        agent_context: Dict[str, Any] | str,
        retry_count: int,
        stream_handler: Optional[Callable[[str], None]] = None,
    ) -> Dict[str, Any]:
        if self._client is None:
            raise RuntimeError("ACPEvaluator requires ACPClient")

        eval_payload = self._build_eval_payload(agent_context)
        item_updates = ensure_list(eval_payload.get("ItemstateUpdates"))
        if any(isinstance(item, dict) and int(item.get("state", 0)) == 0 for item in item_updates):
            next_retry = retry_count + 1
            decision: DecisionType = "retry"
            if next_retry >= self._max_retry:
                decision = "force_pass"
            result = EvaluationResult(
                decision=decision,
                feedback="Some todo items are still unfinished. Complete all items before continuing.",
                retry_count=0 if decision == "force_pass" else next_retry,
                raw_eval="short_circuit_unfinished_items",
            )
            return result.to_dict()

        prompt = f"""
You are the evaluation module for the main agent. Judge completion from structured inputs.

Evaluation input:
{json.dumps(eval_payload, ensure_ascii=False, separators=(",", ":"))}

Rules:
1. Only use ItemstateUpdates and KeyInformation.
2. Do not assume any extra context.
3. decision must be one of pass / retry / force_pass.

Output strict JSON:
{{
  "decision": "pass" | "retry" | "force_pass",
  "feedback": "If retry is needed, provide short actionable advice, else empty."
}}
"""
        if stream_handler is not None:
            parts = []
            for chunk in self._client.stream_complete(
                prompt,
                model=self._model,
                temperature=self._temperature,
            ):
                parts.append(chunk)
                stream_handler(chunk)
            raw = "".join(parts).strip()
        else:
            raw = self._client.complete(prompt, model=self._model, temperature=self._temperature)
        parsed = extract_json_object(raw)

        raw_decision = str(parsed.get("decision", "")).strip().lower() if parsed else ""
        feedback = str(parsed.get("feedback", "")).strip() if parsed else raw

        decision = self._normalize_decision(raw_decision)
        if raw_decision not in {"pass", "retry", "force_pass"}:
            if not feedback:
                feedback = raw

        next_retry = retry_count
        if decision == "retry":
            next_retry = retry_count + 1
            if next_retry >= self._max_retry:
                decision = "force_pass"

        if decision == "pass":
            next_retry = 0

        result = EvaluationResult(
            decision=decision,
            feedback=feedback,
            retry_count=next_retry,
            raw_eval=raw,
        )
        return result.to_dict()
