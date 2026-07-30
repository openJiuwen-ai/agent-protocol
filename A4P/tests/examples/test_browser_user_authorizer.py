from __future__ import annotations

import asyncio
import importlib.util
import sys
from pathlib import Path


from a4p import (
    A4PServer,
    StaticA4PServerTrustStore,
)
from a4p.operation import mandate as operation_mandate


class _MemoryWriter:
    def __init__(self) -> None:
        self.data = bytearray()

    def write(self, data: bytes) -> None:
        self.data.extend(data)

    async def drain(self) -> None:
        return None


def _load_browser_user_authorizer_class():
    path = (
        Path(__file__).resolve().parents[2]
        / "examples"
        / "note_mcp_a4p"
        / "run_user_authorizer.py"
    )
    spec = importlib.util.spec_from_file_location("run_user_authorizer_for_test", path)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.BrowserWebA4PUserAuthorizer


def test_standalone_user_authorizer_returns_signed_mandate() -> None:
    async def run() -> None:
        browser_authorizer_cls = _load_browser_user_authorizer_class()
        trust_store = StaticA4PServerTrustStore(
            A4PServer(
                server_id="local://test",
                require_user_signature=False,
            ).server_trust_config()
        )
        authorizer = browser_authorizer_cls(trust_store=trust_store, open_browser=False)
        mandate = operation_mandate.create_operation_mandate(
            operation={"action": "delete_note", "params": {"note_id": "note-1"}},
            server_url="local://test",
            agent_id="agent-1",
            validity_seconds=60,
            user_signature_method="webauthn",
            user_signature_method_policy={"userVerification": "required"},
        )
        authorize_task = asyncio.create_task(
            authorizer._authorize(
                {
                    "mandate": mandate,
                    "signingOptions": {
                        "signatureMethod": "webauthn",
                        "methodOptions": {
                            "challenge": "agent-substituted-challenge",
                        },
                    },
                }
            )
        )
        await asyncio.sleep(0)
        resolved = authorizer._resolve(
            mandate["operationId"],
            approved=True,
            reject_reason="",
            webauthn_assertion={"id": "cred-1", "type": "public-key", "response": {}},
        )
        response = await authorize_task

        assert resolved["ok"] is True
        assert response["approved"] is True
        assert (
            response["signedMandate"]["signatures"]["user"]["signatureMethod"]
            == "webauthn"
        )
        assert (
            response["signedMandate"]["signatures"]["user"]["credentialId"] == "cred-1"
        )

    asyncio.run(run())


def test_standalone_user_authorizer_overwrites_options_before_queuing() -> None:
    async def run() -> None:
        browser_authorizer_cls = _load_browser_user_authorizer_class()
        server = A4PServer(
            server_id="local://test",
            require_user_signature=False,
        )
        trust_store = StaticA4PServerTrustStore(server.server_trust_config())
        authorizer = browser_authorizer_cls(trust_store=trust_store, open_browser=False)
        mandate = operation_mandate.create_operation_mandate(
            operation={"action": "delete_note", "params": {"note_id": "note-1"}},
            server_url="local://test",
            agent_id="agent-1",
            validity_seconds=60,
            user_signature_method="webauthn",
            user_signature_method_policy={"userVerification": "required"},
        )

        authorize_task = asyncio.create_task(
            authorizer._authorize(
                {
                    "mandate": mandate,
                    "signingOptions": {
                        "signatureMethod": "webauthn",
                        "methodOptions": {
                            "challenge": "agent-substituted-challenge",
                        },
                    },
                }
            )
        )
        await asyncio.sleep(0)

        pending = authorizer._pending[mandate["operationId"]]
        assert (
            pending.request.signingOptions["methodOptions"]["challenge"]
            != "agent-substituted-challenge"
        )
        assert (
            pending.request.signingOptions["methodOptions"]["userVerification"]
            == "required"
        )
        page = authorizer._render_index(mandate["operationId"])
        assert '<script src="/assets/webauthn.js"></script>' in page
        assert '<script src="/assets/authorizer.js"></script>' in page
        assert "function b64urlToBuffer" not in page
        writer = _MemoryWriter()
        await authorizer._send_asset(writer, "authorizer.js")
        assert b"Content-Type: text/javascript" in writer.data
        assert b"approveWithPasskey" in writer.data
        authorizer._resolve(
            mandate["operationId"], approved=False, reject_reason="test"
        )
        response = await authorize_task
        assert response["approved"] is False

    asyncio.run(run())


def test_standalone_user_authorizer_returns_registration_credential_to_agent() -> None:
    async def run() -> None:
        browser_authorizer_cls = _load_browser_user_authorizer_class()
        trust_store = StaticA4PServerTrustStore(
            A4PServer(require_user_signature=False).server_trust_config()
        )
        authorizer = browser_authorizer_cls(trust_store=trust_store, open_browser=False)
        register_task = asyncio.create_task(
            authorizer._register(
                {
                    "registrationRequestId": "registration-1",
                    "userId": "user-1",
                    "creationOptions": {"challenge": "registration-challenge"},
                }
            )
        )
        await asyncio.sleep(0)

        page = authorizer._render_registration("registration-1")
        assert '<script src="/assets/registration.js"></script>' in page
        assert "navigator.credentials.create" not in page

        resolved = authorizer._resolve_registration(
            {
                "registrationRequestId": "registration-1",
                "credential": {"id": "cred-1", "type": "public-key", "response": {}},
            }
        )
        response = await register_task

        assert resolved["ok"] is True
        assert response["userId"] == "user-1"
        assert response["credential"]["id"] == "cred-1"
        assert authorizer._registrations == {}

    asyncio.run(run())
