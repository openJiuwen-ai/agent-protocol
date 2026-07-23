"""Run a complete one-request Ed25519 enrollment and authorization flow."""

from __future__ import annotations

import asyncio
import json

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from a4p import (
    A4PClient,
    A4PServer,
    InMemoryCredentialStore,
    sign_user_mandate_with_signer,
)
from a4p.user_signature.ed25519 import (
    Ed25519UserSigner,
    RegisteredEd25519Method,
    ed25519_public_jwk,
)
from a4p.http_server import A4PHTTPServer


async def main() -> None:
    signature_method = RegisteredEd25519Method(InMemoryCredentialStore())
    http_server = A4PHTTPServer(
        A4PServer(
            server_id="local://ed25519-example",
            user_signature_method=signature_method,
        ),
        host="127.0.0.1",
        port=0,
    )
    await http_server.start()
    client = A4PClient(base_url=f"http://127.0.0.1:{http_server.port}")
    try:
        # The caller owns this private key. The SDK neither persists nor unlocks it.
        private_key = Ed25519PrivateKey.generate()
        registration = await client.register_ed25519_credential(
            {
                "userId": "user-1",
                "publicKey": ed25519_public_jwk(private_key),
                "metadata": {"label": "temporary CLI example key"},
            }
        )
        signer = Ed25519UserSigner(
            credential_id=registration["credential"]["credentialId"],
            private_key=private_key,
        )

        operation = {
            "action": "delete_note",
            "params": {"note_id": "note-1"},
        }
        prepared = await client.prepare_operation_authorization(
            {
                "agentId": "agent-1",
                "userId": "user-1",
                "operation": operation,
                "validitySeconds": 60,
            }
        )
        if prepared.mandate is None:
            raise RuntimeError(prepared.rejectReason or "prepare failed")
        signed_mandate = sign_user_mandate_with_signer(
            prepared.mandate,
            user_signer=signer,
        )
        completed = await client.complete_operation_authorization(
            {
                "signedMandate": signed_mandate,
                "operation": operation,
            }
        )
        print(
            json.dumps(
                {
                    "credentialId": signer.credential_id,
                    "approved": completed.approved,
                    "operationId": completed.operationId,
                },
                ensure_ascii=False,
                indent=2,
            )
        )
    finally:
        await http_server.stop()


if __name__ == "__main__":
    asyncio.run(main())
