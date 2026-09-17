"""HTTP A4P client."""

from __future__ import annotations

import asyncio
import json
import os
import urllib.error
import urllib.request
from dataclasses import is_dataclass
from typing import Any, cast

from a4p.types import (
    IntentAuthorizationRequest,
    IntentAuthorizationResponse,
    IntentMandate,
    IntentToken,
    OperationAuthorizationChallenge,
    OperationAuthorizationCompletionRequest,
    OperationAuthorizationRequest,
    OperationAuthorizationResult,
    OperationMandate,
    TokenVerificationRequest,
    TokenVerificationResponse,
    VerificationResult,
    to_payload,
)


def default_a4p_base_url() -> str:
    return (os.getenv("A4P_SERVER_BASE_URL") or "http://127.0.0.1:8961").rstrip("/")


def default_a4p_timeout() -> float:
    raw = (os.getenv("A4P_HTTP_TIMEOUT_S") or "300").strip()
    try:
        return max(1.0, float(raw))
    except ValueError:
        return 300.0


def _coerce_payload(value: Any) -> dict[str, Any]:
    payload = to_payload(value) if is_dataclass(value) else value
    return payload if isinstance(payload, dict) else {}


def _verification_result_from_payload(payload: Any) -> VerificationResult | None:
    if not isinstance(payload, dict):
        return None
    return VerificationResult(
        valid=bool(payload.get("valid")),
        reason=payload.get("reason"),
        code=payload.get("code"),
        matchedScope=payload.get("matchedScope") if isinstance(payload.get("matchedScope"), dict) else None,
    )


def _intent_mandate_from_payload(payload: Any) -> IntentMandate | None:
    return cast(IntentMandate, payload) if isinstance(payload, dict) else None


def _intent_token_from_payload(payload: Any) -> IntentToken | None:
    return cast(IntentToken, payload) if isinstance(payload, dict) else None


def _operation_mandate_from_payload(payload: Any) -> OperationMandate | None:
    return cast(OperationMandate, payload) if isinstance(payload, dict) else None


class A4PClient:
    """Small async HTTP client for A4P server endpoints."""

    def __init__(self, *, base_url: str | None = None, timeout: float | None = None) -> None:
        self.base_url = (base_url or default_a4p_base_url()).rstrip("/")
        self.timeout = timeout if timeout is not None else default_a4p_timeout()

    async def prepare_intent_authorization(
        self,
        request: IntentAuthorizationRequest | dict[str, Any],
    ) -> IntentAuthorizationResponse:
        payload = await self._post("/a4p/v1/intent-authorizations/prepare", _coerce_payload(request))
        return IntentAuthorizationResponse(
            mandate=_intent_mandate_from_payload(payload.get("mandate")),
            signingOptions=(
                payload.get("signingOptions") if isinstance(payload.get("signingOptions"), dict) else {}
            ),
            verificationResult=_verification_result_from_payload(payload.get("verificationResult")),
            approved=bool(payload.get("approved")),
            rejectReason=payload.get("rejectReason"),
        )

    async def complete_intent_authorization(
        self,
        request: dict[str, Any],
    ) -> IntentAuthorizationResponse:
        payload = await self._post("/a4p/v1/intent-authorizations/complete", request)
        return IntentAuthorizationResponse(
            mandate=_intent_mandate_from_payload(payload.get("mandate")),
            intentToken=_intent_token_from_payload(payload.get("intentToken")),
            verificationResult=_verification_result_from_payload(payload.get("verificationResult")),
            approved=bool(payload.get("approved")),
            rejectReason=payload.get("rejectReason"),
        )

    async def verify_intent_token(
        self,
        request: TokenVerificationRequest | dict[str, Any],
    ) -> TokenVerificationResponse:
        payload = await self._post("/a4p/v1/intent-tokens/verify", _coerce_payload(request))
        return TokenVerificationResponse(
            valid=bool(payload.get("valid")),
            reason=payload.get("reason"),
            code=payload.get("code"),
            matchedScope=payload.get("matchedScope") if isinstance(payload.get("matchedScope"), dict) else None,
        )

    async def prepare_operation_authorization(
        self,
        request: OperationAuthorizationRequest | dict[str, Any],
    ) -> OperationAuthorizationChallenge:
        payload = await self._post("/a4p/v1/operation-authorizations/prepare", _coerce_payload(request))
        return OperationAuthorizationChallenge(
            mandate=_operation_mandate_from_payload(payload.get("mandate")),
            signingOptions=(
                payload.get("signingOptions") if isinstance(payload.get("signingOptions"), dict) else {}
            ),
            verificationResult=_verification_result_from_payload(payload.get("verificationResult")),
            rejectReason=payload.get("rejectReason"),
        )

    async def complete_operation_authorization(
        self,
        request: OperationAuthorizationCompletionRequest | dict[str, Any],
    ) -> OperationAuthorizationResult:
        payload = await self._post("/a4p/v1/operation-authorizations/complete", _coerce_payload(request))
        return OperationAuthorizationResult(
            operationId=str(payload.get("operationId") or "") or None,
            verificationResult=_verification_result_from_payload(payload.get("verificationResult")),
            approved=bool(payload.get("approved")),
            rejectReason=payload.get("rejectReason"),
        )

    async def register_ed25519_credential(self, request: dict[str, Any]) -> dict[str, Any]:
        return await self._post(
            "/a4p/v1/user-credentials/ed25519/register",
            request,
        )

    async def webauthn_registration_options(self, request: dict[str, Any]) -> dict[str, Any]:
        return await self._post("/a4p/v1/user-credentials/webauthn/register/options", request)

    async def verify_webauthn_registration(self, request: dict[str, Any]) -> dict[str, Any]:
        return await self._post("/a4p/v1/user-credentials/webauthn/register/verify", request)

    async def _post(self, path: str, payload: dict[str, Any]) -> dict[str, Any]:
        return await asyncio.to_thread(self._post_sync, path, payload)

    def _post_sync(self, path: str, payload: dict[str, Any]) -> dict[str, Any]:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        req = urllib.request.Request(
            f"{self.base_url}{path}",
            data=data,
            headers={"Content-Type": "application/json", "Accept": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            try:
                error_payload = json.loads(raw)
            except json.JSONDecodeError:
                error_payload = {"error": raw}
            raise RuntimeError(f"A4P HTTP {exc.code}: {error_payload}") from exc
        except urllib.error.URLError as exc:
            raise RuntimeError(f"A4P HTTP request failed: {exc}") from exc
        parsed = json.loads(raw) if raw else {}
        return parsed if isinstance(parsed, dict) else {}


__all__ = ["A4PClient", "default_a4p_base_url", "default_a4p_timeout"]
