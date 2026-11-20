"""Run the standalone A4P User Authorizer with a Browser/WebAuthn UI."""

from __future__ import annotations

import argparse
import asyncio
import html
import json
import webbrowser
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from string import Template
from typing import Any
from urllib.parse import parse_qs, urlencode, urlsplit

from a4p import (
    MandateSecurityError,
    StaticA4PServerTrustStore,
    UserAuthorizationRequest,
    mandate_identifier,
    verify_local_user_authorization_request,
)
from a4p.user_signature.webauthn import WebAuthnUserSigner
from a4p.user_authorizer import sign_user_mandate_with_signer
from a4p.user_signature import A4PUserSigner


_ASSET_DIR = Path(__file__).with_name("user_authorizer_assets")
_PUBLIC_ASSETS = {
    "authorizer.css": "text/css; charset=utf-8",
    "authorizer.js": "text/javascript; charset=utf-8",
    "registration.js": "text/javascript; charset=utf-8",
    "webauthn.js": "text/javascript; charset=utf-8",
}


@lru_cache(maxsize=None)
def _asset_text(name: str) -> str:
    return (_ASSET_DIR / name).read_text(encoding="utf-8")


def _render_template(name: str, **values: str) -> str:
    return Template(_asset_text(name)).substitute(values)


@dataclass
class _PendingAuthorization:
    request: UserAuthorizationRequest
    future: asyncio.Future[dict[str, Any]]


@dataclass
class _PendingRegistration:
    registration_request_id: str
    user_id: str
    creation_options: dict[str, Any]
    future: asyncio.Future[dict[str, Any]]


class BrowserWebA4PUserAuthorizer:
    """Standalone Browser/WebAuthn User Authorizer service."""

    def __init__(
        self,
        *,
        trust_store: StaticA4PServerTrustStore,
        user_signer: A4PUserSigner | None = None,
        host: str = "localhost",
        port: int = 8970,
        open_browser: bool = True,
    ) -> None:
        self.host = host
        self.port = port
        self.open_browser = open_browser
        self.trust_store = trust_store
        self.user_signer = user_signer or WebAuthnUserSigner()
        self._pending: dict[str, _PendingAuthorization] = {}
        self._registrations: dict[str, _PendingRegistration] = {}
        self._server: asyncio.AbstractServer | None = None

    async def start(self) -> None:
        if self._server is not None:
            return
        self._server = await asyncio.start_server(self._handle_client, self.host, self.port)
        sockets = self._server.sockets or []
        if sockets:
            self.port = int(sockets[0].getsockname()[1])

    async def stop(self) -> None:
        if self._server is None:
            return
        self._server.close()
        await self._server.wait_closed()
        self._server = None

    async def _authorize(self, payload: dict[str, Any]) -> dict[str, Any]:
        mandate = payload.get("mandate") if isinstance(payload.get("mandate"), dict) else None
        signing_options = (
            payload.get("signingOptions") if isinstance(payload.get("signingOptions"), dict) else {}
        )
        if mandate is None:
            return {"approved": False, "rejectReason": "mandate missing"}
        try:
            authorization_id = mandate_identifier(mandate)
        except ValueError as exc:
            return {"approved": False, "rejectReason": str(exc), "errorCode": "MANDATE_INVALID"}
        if authorization_id in self._pending:
            return {
                "approved": False,
                "rejectReason": f"Authorization already pending: {authorization_id}",
                "errorCode": "MANDATE_ID_CONFLICT",
            }

        loop = asyncio.get_running_loop()
        future: asyncio.Future[dict[str, Any]] = loop.create_future()
        request = UserAuthorizationRequest(
            mandate=mandate,
            signingOptions=signing_options,
        )
        try:
            safe_signing_options = verify_local_user_authorization_request(
                request,
                trust_store=self.trust_store,
                expected_signature_method=self.user_signer.signature_method,
            )
        except MandateSecurityError as exc:
            return {
                "approved": False,
                "rejectReason": str(exc),
                "errorCode": exc.code,
            }
        request = UserAuthorizationRequest(
            mandate=mandate,
            signingOptions=safe_signing_options,
        )
        self._pending[authorization_id] = _PendingAuthorization(request=request, future=future)
        url = self._request_url(authorization_id)
        print(f"[A4P User Authorizer] pending authorization: {url}")
        if self.open_browser:
            asyncio.create_task(asyncio.to_thread(webbrowser.open, url, 2))
        return await future

    async def _register(self, payload: dict[str, Any]) -> dict[str, Any]:
        registration_request_id = str(payload.get("registrationRequestId") or "").strip()
        user_id = str(payload.get("userId") or "").strip()
        creation_options = (
            payload.get("creationOptions") if isinstance(payload.get("creationOptions"), dict) else None
        )
        if not registration_request_id:
            return {"ok": False, "message": "registrationRequestId missing"}
        if not user_id:
            return {"ok": False, "message": "userId missing"}
        if creation_options is None or not str(creation_options.get("challenge") or "").strip():
            return {"ok": False, "message": "WebAuthn creationOptions missing"}
        if registration_request_id in self._registrations:
            return {
                "ok": False,
                "message": f"Browser key registration already pending: {registration_request_id}",
            }
        loop = asyncio.get_running_loop()
        future: asyncio.Future[dict[str, Any]] = loop.create_future()
        pending = _PendingRegistration(
            registration_request_id=registration_request_id,
            user_id=user_id,
            creation_options=creation_options,
            future=future,
        )
        self._registrations[registration_request_id] = pending
        url = self._registration_url(registration_request_id)
        print(f"[A4P User Authorizer] pending browser key registration: {url}")
        if self.open_browser:
            asyncio.create_task(asyncio.to_thread(webbrowser.open, url, 2))
        return await future

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            method, path, query, body = await self._read_request(reader)
            if method == "GET" and path.startswith("/assets/"):
                asset_name = path.removeprefix("/assets/")
                if asset_name in _PUBLIC_ASSETS:
                    await self._send_asset(writer, asset_name)
                    return
            if method == "GET" and path in {"/", "/authorize"}:
                selected_id = (query.get("authorizationId") or [""])[0] or None
                await self._send_html(writer, self._render_index(selected_id))
                return
            if method == "POST" and path == "/authorize":
                await self._send_json(writer, await self._authorize(self._json_body(body)))
                return
            if method == "GET" and path == "/register":
                selected_id = (query.get("requestId") or [""])[0] or None
                await self._send_html(writer, self._render_registration(selected_id))
                return
            if method == "POST" and path == "/register":
                await self._send_json(writer, await self._register(self._json_body(body)))
                return
            if method == "POST" and path == "/register/complete":
                await self._send_json(writer, self._resolve_registration(self._json_body(body)))
                return
            if method == "POST" and path == "/approve":
                await self._send_json(writer, self._resolve_json_approval(self._json_body(body)))
                return
            if method == "POST" and path == "/reject":
                fields = parse_qs(body.decode("utf-8"))
                authorization_id = (fields.get("authorizationId") or [""])[0]
                reason = (fields.get("reason") or ["Rejected in browser User Authorizer"])[0]
                payload = self._resolve(authorization_id, approved=False, reject_reason=reason)
                await self._send_html(writer, self._render_result(payload))
                return
            await self._send_html(writer, self._render_not_found(), status=404, reason="Not Found")
        except Exception as exc:
            await self._send_html(
                writer,
                self._render_error(str(exc)),
                status=500,
                reason="Internal Server Error",
            )
        finally:
            writer.close()
            await writer.wait_closed()

    async def _read_request(self, reader: asyncio.StreamReader) -> tuple[str, str, dict[str, list[str]], bytes]:
        request_line = await reader.readline()
        if not request_line:
            return "", "/", {}, b""
        method, raw_path, _version = request_line.decode("iso-8859-1").strip().split(" ", 2)
        headers: dict[str, str] = {}
        while True:
            line = await reader.readline()
            if line in {b"\r\n", b"\n", b""}:
                break
            key, _, value = line.decode("iso-8859-1").partition(":")
            headers[key.strip().lower()] = value.strip()
        content_length = int(headers.get("content-length") or "0")
        body = await reader.readexactly(content_length) if content_length else b""
        parsed = urlsplit(raw_path)
        return method.upper(), parsed.path, parse_qs(parsed.query), body

    def _request_url(self, authorization_id: str | None = None) -> str:
        base = f"http://{self.host}:{self.port}/authorize"
        if not authorization_id:
            return base
        return f"{base}?{urlencode({'authorizationId': authorization_id})}"

    def _registration_url(self, request_id: str) -> str:
        return (
            f"http://{self.host}:{self.port}/register?"
            f"{urlencode({'requestId': request_id})}"
        )

    @staticmethod
    def _json_body(body: bytes) -> dict[str, Any]:
        try:
            payload = json.loads(body.decode("utf-8") or "{}")
        except json.JSONDecodeError as exc:
            raise ValueError(f"Invalid JSON body: {exc}") from exc
        if not isinstance(payload, dict):
            raise ValueError("JSON body must be an object")
        return payload

    def _resolve_json_approval(self, payload: dict[str, Any]) -> dict[str, Any]:
        authorization_id = str(payload.get("authorizationId") or "")
        assertion = payload.get("assertion") if isinstance(payload.get("assertion"), dict) else None
        if assertion is None:
            return {"ok": False, "message": "WebAuthn assertion missing"}
        return self._resolve(
            authorization_id,
            approved=True,
            reject_reason="",
            webauthn_assertion=assertion,
        )

    def _resolve_registration(self, payload: dict[str, Any]) -> dict[str, Any]:
        registration_request_id = str(payload.get("registrationRequestId") or "").strip()
        credential = payload.get("credential") if isinstance(payload.get("credential"), dict) else None
        pending = self._registrations.get(registration_request_id)
        if pending is None:
            return {
                "ok": False,
                "message": f"No pending registration: {registration_request_id}",
            }
        if pending.future.done():
            return {
                "ok": False,
                "message": f"Registration already resolved: {registration_request_id}",
            }
        if credential is None:
            return {"ok": False, "message": "WebAuthn registration credential missing"}
        pending.future.set_result(
            {
                "registrationRequestId": registration_request_id,
                "userId": pending.user_id,
                "credential": credential,
            }
        )
        self._registrations.pop(registration_request_id, None)
        return {"ok": True, "message": "Browser key registration response returned to Agent"}

    def _resolve(
        self,
        authorization_id: str,
        *,
        approved: bool,
        reject_reason: str,
        webauthn_assertion: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        pending = self._pending.get(authorization_id)
        if pending is None:
            return {"ok": False, "message": f"No pending authorization: {authorization_id}"}
        if pending.future.done():
            return {"ok": False, "message": f"Authorization already resolved: {authorization_id}"}

        if approved:
            if webauthn_assertion is None:
                return {"ok": False, "message": "WebAuthn assertion missing"}
            signed = sign_user_mandate_with_signer(
                pending.request.mandate,
                user_signer=self.user_signer,
                signing_input={"assertion": webauthn_assertion},
            )
            result = {
                "approved": True,
                "signedMandate": signed,
            }
            pending.future.set_result(result)
            self._pending.pop(authorization_id, None)
            return {"ok": True, "message": f"Approved {authorization_id}"}

        result = {
            "approved": False,
            "rejectReason": reject_reason or "Rejected in browser User Authorizer",
        }
        pending.future.set_result(result)
        self._pending.pop(authorization_id, None)
        return {"ok": True, "message": f"Rejected {authorization_id}"}

    def _render_index(self, selected_authorization_id: str | None = None) -> str:
        if selected_authorization_id:
            selected = self._pending.get(selected_authorization_id)
            if selected is None:
                body = self._render_empty(
                    f"No pending authorization: {selected_authorization_id}"
                )
            else:
                body = self._render_pending(selected)
        else:
            pending = list(self._pending.values())
            if not pending:
                body = self._render_empty("No pending A4P authorization requests.")
            else:
                body = "\n".join(self._render_pending(item) for item in pending)
        return _render_template("authorizer.html", body=body)

    def _render_pending(self, pending: _PendingAuthorization) -> str:
        request = pending.request
        mandate = request.mandate
        display_text = html.escape(
            str(mandate.get("displayText") or "A4P authorization request")
        )
        authorization_object = {
            "signingOptions": request.signingOptions,
            "mandate": mandate,
        }
        authorization_json = html.escape(
            json.dumps(
                authorization_object,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
        )
        authorization_id = html.escape(mandate_identifier(mandate), quote=True)
        signing_options = html.escape(
            json.dumps(
                request.signingOptions,
                ensure_ascii=False,
                separators=(",", ":"),
            ),
            quote=True,
        )
        return _render_template(
            "authorization_card.html",
            authorization_id=authorization_id,
            signing_options=signing_options,
            display_text=display_text,
            authorization_json=authorization_json,
        )

    def _render_registration(self, registration_request_id: str | None) -> str:
        pending = self._registrations.get(registration_request_id or "")
        if pending is None:
            return self._render_result(
                {"message": "No pending browser key registration"}
            )
        return _render_template(
            "registration.html",
            request_id=html.escape(pending.registration_request_id, quote=True),
            user_id=html.escape(pending.user_id),
            options=html.escape(
                json.dumps(
                    pending.creation_options,
                    ensure_ascii=False,
                    separators=(",", ":"),
                ),
                quote=True,
            ),
        )

    @staticmethod
    def _render_empty(message: str) -> str:
        return _render_template("empty.html", message=html.escape(message))

    @staticmethod
    def _render_result(payload: dict[str, Any]) -> str:
        message = html.escape(str(payload.get("message") or "Done"))
        return _render_template("result.html", message=message)

    @staticmethod
    def _render_not_found() -> str:
        return _render_template("not_found.html")

    @staticmethod
    def _render_error(message: str) -> str:
        return _render_template("error.html", message=html.escape(message))

    @staticmethod
    async def _send_asset(writer: asyncio.StreamWriter, asset_name: str) -> None:
        raw = _asset_text(asset_name).encode("utf-8")
        writer.write(
            (
                "HTTP/1.1 200 OK\r\n"
                f"Content-Type: {_PUBLIC_ASSETS[asset_name]}\r\n"
                f"Content-Length: {len(raw)}\r\n"
                "Connection: close\r\n\r\n"
            ).encode("ascii")
            + raw
        )
        await writer.drain()

    @staticmethod
    async def _send_html(
        writer: asyncio.StreamWriter,
        body: str,
        *,
        status: int = 200,
        reason: str = "OK",
    ) -> None:
        raw = body.encode("utf-8")
        writer.write(
            (
                f"HTTP/1.1 {status} {reason}\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                f"Content-Length: {len(raw)}\r\n"
                "Connection: close\r\n\r\n"
            ).encode("ascii")
            + raw
        )
        await writer.drain()

    @staticmethod
    async def _send_json(
        writer: asyncio.StreamWriter,
        payload: dict[str, Any],
        *,
        status: int = 200,
        reason: str = "OK",
    ) -> None:
        raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        writer.write(
            (
                f"HTTP/1.1 {status} {reason}\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                f"Content-Length: {len(raw)}\r\n"
                "Connection: close\r\n\r\n"
            ).encode("ascii")
            + raw
        )
        await writer.drain()


async def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-open-browser", action="store_true", help="Print authorization URLs without opening a browser.")
    parser.add_argument(
        "--trusted-server-keys",
        default=".a4p/trusted_server_keys.json",
        help="JSON file containing locally trusted A4P Server Ed25519 public keys.",
    )
    args = parser.parse_args()

    authorizer = BrowserWebA4PUserAuthorizer(
        trust_store=StaticA4PServerTrustStore.from_json_file(args.trusted_server_keys),
        open_browser=not args.no_open_browser,
    )
    await authorizer.start()

    print(f"[A4P User Authorizer] HTTP server: http://{authorizer.host}:{authorizer.port}")
    print("[A4P User Authorizer] A4P Server calls are relayed by the Agent; no remote client is configured here.")
    try:
        while True:
            await asyncio.sleep(3600)
    finally:
        await authorizer.stop()


if __name__ == "__main__":
    asyncio.run(main())
