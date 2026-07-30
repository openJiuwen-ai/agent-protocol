"""Run the standalone A4P authorization server for the note demo."""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path

from a4p import A4PServer, JsonFileCredentialStore
from a4p.user_signature.webauthn import WebAuthnSignatureMethod
from a4p.http_server import A4PHTTPServer


async def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="A4P server host.")
    parser.add_argument("--port", type=int, default=8961, help="A4P server port.")
    parser.add_argument(
        "--trusted-keys-output",
        default=".a4p/trusted_server_keys.json",
        help="Write the demo Server's public trust configuration to this JSON file.",
    )
    args = parser.parse_args()

    credential_store = JsonFileCredentialStore(".a4p/webauthn_credentials.json")
    signature_method = WebAuthnSignatureMethod(
        credential_store,
        rp_id="localhost",
        rp_name="A4P Note Demo",
        expected_origin="http://localhost:8970",
    )
    a4p_server = A4PServer(
        server_id="local://note-a4p-demo",
        user_signature_method=signature_method,
    )
    trusted_keys_path = Path(args.trusted_keys_output)
    trusted_keys_path.parent.mkdir(parents=True, exist_ok=True)
    trusted_keys_path.write_text(
        json.dumps(a4p_server.server_trust_config(), ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    a4p_http = A4PHTTPServer(
        a4p_server,
        host=args.host,
        port=args.port,
    )
    await a4p_http.start()

    print(f"[A4P Server] HTTP server: http://{a4p_http.host}:{a4p_http.port}")
    print(f"[A4P Server] Local trust configuration: {trusted_keys_path}")
    print("[A4P Server] User Authorizer is external; use prepare/complete authorization flow.")
    try:
        while True:
            await asyncio.sleep(3600)
    finally:
        await a4p_http.stop()


if __name__ == "__main__":
    asyncio.run(main())
