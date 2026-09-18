"""IIAP model-facing orchestration services."""

from __future__ import annotations

from typing import Any, Callable, Literal
from uuid import uuid4

from .assistance import validate_assistance_text
from .models import ModelAdapter, ModelRequest
from .packet import validate_intent_context_packet
from .privacy import validate_privacy
from .prompts import assistance_prompt, decision_prompt
from .validation import create_decision_envelope, default_no_intervention


class DecisionService:
    def __init__(
        self,
        model: ModelAdapter,
        *,
        profile: Literal["production", "test"] = "production",
        decision_id_factory: Callable[[], str] | None = None,
    ) -> None:
        self._model = model
        self._profile = profile
        self._decision_id_factory = decision_id_factory or (lambda: f"dec_{uuid4().hex}")

    async def decide(self, packet: dict[str, Any]) -> dict[str, Any]:
        validate_intent_context_packet(packet)
        observations = packet.get("observations", {})
        if not validate_privacy(packet) or not isinstance(observations, dict) or not observations.get("events"):
            output: object = default_no_intervention()
        else:
            request = ModelRequest(prompt=decision_prompt(packet, profile=self._profile), payload=packet, operation="decision")
            output = await self._model.generate_decision(request)
        return create_decision_envelope(
            output,
            packet=packet,
            decision_id=self._decision_id_factory(),
        )


class AssistanceService:
    def __init__(self, model: ModelAdapter) -> None:
        self._model = model

    async def assist(self, request: dict[str, Any]) -> dict[str, Any]:
        required_ids = ("requestId", "packetId", "decisionId", "surfaceInstanceId")
        if (request.get("type") != "iiap.assistance.request" or request.get("iiapVersion") != "0.1"
                or any(not isinstance(request.get(key), str) or not request[key] for key in required_ids)
                or request.get("topic") not in {"compare_options", "explain_rules", "fix_block", "save_progress"}
                or not isinstance(request.get("language"), str) or not request["language"]):
            raise ValueError("INVALID_ASSISTANCE_REQUEST")
        if not validate_privacy(request, max_bytes=4096):
            raise ValueError("PRIVACY_REJECTED")
        output = await self._model.generate_assistance(ModelRequest(prompt=assistance_prompt(request), payload=request, operation="assistance"))
        if isinstance(output, str):
            import json
            try:
                output = json.loads(output)
            except json.JSONDecodeError as exc:
                raise ValueError("INVALID_ASSISTANCE_RESPONSE") from exc
        expected_keys = {"type", "iiapVersion", "requestId", "message"}
        if (not isinstance(output, dict) or set(output) != expected_keys
                or output.get("type") != "iiap.assistance.response"
                or output.get("iiapVersion") != "0.1"
                or output.get("requestId") != request.get("requestId")):
            raise ValueError("INVALID_ASSISTANCE_RESPONSE")
        valid, _reason = validate_assistance_text(output.get("message"))
        if not valid:
            raise ValueError("ASSISTANCE_UNSAFE_OUTPUT")
        return {"type": "iiap.assistance.response", "iiapVersion": "0.1", "requestId": output["requestId"], "message": output["message"]}
