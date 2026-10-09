"""Register a browser WebAuthn credential for the local A4P demo."""

from __future__ import annotations

import asyncio
import json
import os
import urllib.error
import urllib.request
from typing import Any

from a4p import A4PClient


USER_ID = "demo-user"
USER_AUTHORIZER_BASE_URL = (os.getenv("A4P_USER_AUTHORIZER_BASE_URL") or "http://localhost:8970").rstrip("/")


def _post_user_authorizer_sync(path: str, payload: dict[str, Any]) -> dict[str, Any]:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        f"{USER_AUTHORIZER_BASE_URL}{path}",
        data=data,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=300) as response:
            raw = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"User Authorizer HTTP {exc.code}: {raw}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"User Authorizer HTTP request failed: {exc}") from exc
    parsed = json.loads(raw) if raw else {}
    return parsed if isinstance(parsed, dict) else {}


async def register_browser_key() -> None:
    """Relay one registration ceremony between A4P Server and User Authorizer."""
    a4p_client = A4PClient()
    options_payload = await a4p_client.webauthn_registration_options(
        {
            "userId": USER_ID,
            "userName": USER_ID,
            "userDisplayName": "A4P Note Demo User",
        }
    )
    registration_request_id = str(options_payload.get("registrationRequestId") or "").strip()
    creation_options = (
        options_payload.get("options") if isinstance(options_payload.get("options"), dict) else {}
    )
    local_result = await asyncio.to_thread(
        _post_user_authorizer_sync,
        "/register",
        {
            "registrationRequestId": registration_request_id,
            "userId": USER_ID,
            "creationOptions": creation_options,
        },
    )
    credential = local_result.get("credential")
    if not isinstance(credential, dict):
        raise RuntimeError(f"Local browser key registration failed: {local_result}")
    verified = await a4p_client.verify_webauthn_registration(
        {
            "registrationRequestId": registration_request_id,
            "userId": USER_ID,
            "credential": credential,
        }
    )
    registered = verified.get("credential") if isinstance(verified.get("credential"), dict) else {}
    print(f"[enrollment] registered browser credential: {registered.get('credentialId')}")


if __name__ == "__main__":
    asyncio.run(register_browser_key())
