"""Run the A4P authorization server with a local web approval UI."""

from __future__ import annotations

import asyncio
import argparse
import html
import json
import webbrowser
from dataclasses import dataclass
from typing import Any
from urllib.parse import parse_qs, urlencode, urlsplit

from a4p import A4PServer, UserAuthorizationRequest, UserAuthorizationResponse, sign_user_mandate_for_request
from a4p.http_server import A4PHTTPServer


@dataclass
class _PendingAuthorization:
    request: UserAuthorizationRequest
    future: asyncio.Future[UserAuthorizationResponse]


class LocalWebA4PUserAuthorizer:
    """A tiny localhost UI for approving or rejecting A4P mandates."""

    def __init__(self, *, host: str = "127.0.0.1", port: int = 8970, open_browser: bool = True) -> None:
        self.host = host
        self.port = port
        self.open_browser = open_browser
        self._pending: dict[str, _PendingAuthorization] = {}
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

    async def authorize(self, request: UserAuthorizationRequest) -> UserAuthorizationResponse:
        loop = asyncio.get_running_loop()
        future: asyncio.Future[UserAuthorizationResponse] = loop.create_future()
        self._pending[request.requestId] = _PendingAuthorization(request=request, future=future)
        url = self._request_url(request.requestId)
        print(f"[A4P UI] pending authorization: {url}")
        if self.open_browser:
            asyncio.create_task(asyncio.to_thread(webbrowser.open, url, 2))
        return await future

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            method, path, query, body = await self._read_request(reader)
            if method == "GET" and path in {"/", "/authorize"}:
                selected_id = (query.get("requestId") or [""])[0] or None
                await self._send_html(writer, self._render_index(selected_id))
                return
            if method == "POST" and path in {"/approve", "/reject"}:
                fields = parse_qs(body.decode("utf-8"))
                request_id = (fields.get("requestId") or [""])[0]
                reason = (fields.get("reason") or ["Rejected in local web UI"])[0]
                payload = self._resolve(request_id, approved=(path == "/approve"), reject_reason=reason)
                await self._send_html(writer, self._render_result(payload))
                return
            await self._send_html(writer, self._render_not_found(), status=404, reason="Not Found")
        except Exception as exc:
            await self._send_html(
                writer,
                f"<h1>Internal error</h1><pre>{html.escape(str(exc))}</pre>",
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

    def _request_url(self, request_id: str | None = None) -> str:
        base = f"http://{self.host}:{self.port}/authorize"
        if not request_id:
            return base
        return f"{base}?{urlencode({'requestId': request_id})}"

    def _resolve(self, request_id: str, *, approved: bool, reject_reason: str) -> dict[str, Any]:
        pending = self._pending.pop(request_id, None)
        if pending is None:
            return {"ok": False, "message": f"No pending request: {request_id}"}
        if pending.future.done():
            return {"ok": False, "message": f"Request already resolved: {request_id}"}

        if approved:
            signed = sign_user_mandate_for_request(
                pending.request.mandate,
                request_id=pending.request.requestId,
                approval_method="local.web_ui",
            )
            pending.future.set_result(
                UserAuthorizationResponse(
                    requestId=pending.request.requestId,
                    approved=True,
                    signedMandate=signed,
                )
            )
            return {"ok": True, "message": f"Approved {request_id}"}

        pending.future.set_result(
            UserAuthorizationResponse(
                requestId=pending.request.requestId,
                approved=False,
                rejectReason=reject_reason or "Rejected in local web UI",
            )
        )
        return {"ok": True, "message": f"Rejected {request_id}"}

    def _render_index(self, selected_request_id: str | None = None) -> str:
        if selected_request_id:
            selected = self._pending.get(selected_request_id)
            if selected is None:
                body = self._render_empty(f"No pending request: {selected_request_id}")
            else:
                body = self._render_pending(selected)
        else:
            pending = list(self._pending.values())
            if not pending:
                body = self._render_empty("No pending A4P authorization requests.")
            else:
                body = "\n".join(self._render_pending(item) for item in pending)
        return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>A4P Local Authorization</title>
  <style>
    :root {{
      color-scheme: light;
      --bg: #f7f8fb;
      --panel: #ffffff;
      --text: #1f2328;
      --muted: #5f6b7a;
      --border: #d8dee8;
      --green: #1f7a4d;
      --red: #b42318;
      --blue: #315f96;
      --code: #f2f4f8;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      line-height: 1.5;
    }}
    main {{
      max-width: 980px;
      margin: 0 auto;
      padding: 32px 20px 48px;
    }}
    header {{
      margin-bottom: 20px;
    }}
    h1 {{
      margin: 0 0 6px;
      font-size: 28px;
      font-weight: 700;
      letter-spacing: 0;
    }}
    .subtitle {{
      margin: 0;
      color: var(--muted);
      font-size: 15px;
    }}
    article {{
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 22px;
      margin: 16px 0;
      box-shadow: 0 10px 24px rgba(31, 35, 40, 0.06);
    }}
    .field {{
      display: grid;
      grid-template-columns: 170px minmax(0, 1fr);
      gap: 16px;
      padding: 14px 0;
      border-bottom: 1px solid var(--border);
    }}
    .field:first-of-type {{
      padding-top: 0;
    }}
    .field label {{
      color: var(--muted);
      font-size: 13px;
      font-weight: 700;
      text-transform: uppercase;
    }}
    .field code {{
      overflow-wrap: anywhere;
      color: var(--blue);
      font-size: 14px;
    }}
    .instruction {{
      font-size: 18px;
      font-weight: 650;
    }}
    details {{
      margin-top: 18px;
      border: 1px solid var(--border);
      border-radius: 8px;
      background: var(--code);
    }}
    summary {{
      cursor: pointer;
      padding: 12px 14px;
      font-weight: 700;
    }}
    pre {{
      margin: 0;
      border-top: 1px solid var(--border);
      padding: 14px;
      overflow: auto;
      font-size: 13px;
      line-height: 1.45;
    }}
    .actions {{
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      margin-top: 20px;
    }}
    button {{
      min-height: 40px;
      padding: 0 16px;
      border: 0;
      border-radius: 6px;
      color: white;
      font-weight: 700;
      cursor: pointer;
    }}
    .approve {{ background: var(--green); }}
    .reject {{ background: var(--red); }}
    .empty {{
      background: var(--panel);
      border: 1px dashed var(--border);
      border-radius: 8px;
      padding: 24px;
      color: var(--muted);
    }}
    @media (max-width: 680px) {{
      main {{ padding: 24px 14px 36px; }}
      .field {{ grid-template-columns: 1fr; gap: 4px; }}
    }}
  </style>
</head>
<body>
  <main>
    <header>
      <h1>A4P Local Authorization</h1>
      <p class="subtitle">Review the request, inspect the full object if needed, then approve or reject.</p>
    </header>
    {body}
  </main>
</body>
</html>"""

    def _render_pending(self, pending: _PendingAuthorization) -> str:
        request = pending.request
        mandate = request.mandate
        display_text = html.escape(str(mandate.get("displayText") or "A4P authorization request"))
        authorization_object = {
            "requestId": request.requestId,
            "uiContext": request.uiContext,
            "mandate": mandate,
        }
        authorization_json = html.escape(json.dumps(authorization_object, ensure_ascii=False, indent=2, sort_keys=True))
        request_id = html.escape(request.requestId)
        return f"""<article>
  <div class="field">
    <label>Request ID</label>
    <code>{request_id}</code>
  </div>
  <div class="field">
    <label>Instruction</label>
    <div class="instruction">{display_text}</div>
  </div>
  <details>
    <summary>完整授权对象</summary>
    <pre>{authorization_json}</pre>
  </details>
  <div class="actions">
    <form method="post" action="/approve">
      <input type="hidden" name="requestId" value="{request_id}">
      <button class="approve" type="submit">Approve</button>
    </form>
    <form method="post" action="/reject">
      <input type="hidden" name="requestId" value="{request_id}">
      <button class="reject" type="submit">Reject</button>
    </form>
  </div>
</article>"""

    @staticmethod
    def _render_empty(message: str) -> str:
        return f"""<section class="empty">
  {html.escape(message)}
</section>"""

    @staticmethod
    def _render_result(payload: dict[str, Any]) -> str:
        message = html.escape(str(payload.get("message") or "Done"))
        return f"""<!doctype html><html><body>
<h1>{message}</h1>
<p><a href="/">Back to pending requests</a></p>
</body></html>"""

    @staticmethod
    def _render_not_found() -> str:
        return "<!doctype html><html><body><h1>Not Found</h1></body></html>"

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


async def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-open-browser", action="store_true", help="Print authorization URLs without opening a browser.")
    args = parser.parse_args()

    authorizer = LocalWebA4PUserAuthorizer(open_browser=not args.no_open_browser)
    await authorizer.start()

    a4p_http = A4PHTTPServer(
        A4PServer(user_authorizer=authorizer, server_id="local://note-a4p-demo"),
        host="127.0.0.1",
        port=8961,
    )
    await a4p_http.start()

    print("[A4P] HTTP server: http://127.0.0.1:8961")
    print(f"[A4P UI] Open http://{authorizer.host}:{authorizer.port}/ to approve requests.")
    try:
        while True:
            await asyncio.sleep(3600)
    finally:
        await a4p_http.stop()
        await authorizer.stop()


if __name__ == "__main__":
    asyncio.run(main())
