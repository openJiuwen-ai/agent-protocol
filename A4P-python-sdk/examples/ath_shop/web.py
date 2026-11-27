"""Serve the ATH Shop workflow on the local browser."""

import asyncio
import json
import secrets
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from run import GATEWAY_USERNAME, ATHError, DemoView, argument_parser, run


class RestartRequested(Exception):
    pass


class BrowserView(DemoView):
    def __init__(self):
        self.condition = threading.Condition()
        self.reset()

    def reset(self):
        self.restarting = False
        self.events = []
        self.pending = None
        self.answer = None
        self.running = False
        self.username = GATEWAY_USERNAME
        self.error = ""
        self.run_id = secrets.token_urlsafe(16)

    def show(self, step, message, data=None):
        with self.condition:
            if self.restarting and step != 6:
                raise RestartRequested
            self.events.append({"step": step, "message": message, "data": data})

    def ask(self, kind, message):
        with self.condition:
            self.pending = {"kind": kind, "id": secrets.token_urlsafe(16)}
            self.answer = None
            if not self.condition.wait_for(lambda: self.restarting or self.answer is not None, timeout=600):
                self.pending = None
                raise ATHError("interaction_timeout", "等待操作超过十分钟，演示已结束")
            if self.restarting:
                self.pending = None
                raise RestartRequested
            return self.answer

    def execute(self, args, password):
        try:
            asyncio.run(run(args, self, password, username=self.username))
        except RestartRequested:
            pass
        except ATHError as exc:
            self.error = f"{exc.code} · {exc}"
        except (OSError, ValueError):
            self.error = "请检查网关地址、代理标识和私钥文件。"
        except Exception:
            self.error = "演示运行异常，请检查本地环境。"
            raise
        finally:
            with self.condition:
                self.running = False
                if self.restarting:
                    self.reset()


class Handler(BaseHTTPRequestHandler):
    def reply(self, code, body, mime="application/json"):
        if mime == "application/json":
            body = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", mime + "; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_request(self, *args):
        pass

    def do_GET(self):
        if self.headers.get("Host") != self.server.address:
            return self.reply(403, {})
        if self.path == "/":
            return self.reply(200, Path(__file__).with_name("index.html").read_bytes(), "text/html")
        if self.path != "/state":
            return self.reply(404, {})
        view = self.server.view
        with view.condition:
            config = self.server.config
            state = dict(restarting=view.restarting, events=list(view.events), running=view.running, error=view.error,
                         pending=view.pending, id=view.run_id, gateway=config.gateway,
                         user=view.username, agent=config.agent_id, signup=config.signup)
        self.reply(200, state)

    def do_POST(self):
        if (self.headers.get("Host") != self.server.address
                or self.headers.get("Origin") != "http://" + self.server.address
                or self.headers.get_content_type() != "application/json"):
            return self.reply(403, {})
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= 8192:
                return self.reply(400, {})
            data = json.loads(self.rfile.read(size))
            if not isinstance(data, dict):
                return self.reply(400, {})
        except ValueError:
            return self.reply(400, {})

        view = self.server.view
        with view.condition:
            if self.path == "/new":
                if data.get("id") != view.run_id:
                    return self.reply(409, {})
                if view.running:
                    view.restarting = True
                    view.pending = None
                    view.condition.notify_all()
                else:
                    view.reset()
            elif self.path == "/start":
                username = data.get("username")
                if not isinstance(username, str) or not username.strip():
                    return self.reply(400, {})
                password = data.get("password")
                if not isinstance(password, str) or not password:
                    return self.reply(400, {})
                if view.running or data.get("id") != view.run_id:
                    return self.reply(409, {})
                if view.events or view.error:
                    return self.reply(409, {})
                view.run_id = secrets.token_urlsafe(16)
                view.username = username.strip()
                view.running = True
                threading.Thread(target=view.execute, args=(self.server.config, password)).start()
            elif self.path == "/answer":
                if not view.pending or view.pending["id"] != data.get("id"):
                    return self.reply(409, {})
                choices = {"consent": ("continue", "q"), "checkout": ("yes", "no")}
                if data.get("answer") not in choices[view.pending["kind"]]:
                    return self.reply(400, {})
                view.answer = data["answer"]
                view.pending = None
                view.condition.notify_all()
            else:
                return self.reply(404, {})
        self.reply(200, {})


if __name__ == "__main__":
    parser = argument_parser()
    parser.description = __doc__
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    args.search = args.search.strip()
    if not args.search:
        parser.error("--search must contain a search term")
    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        server.config = args
        server.view = BrowserView()
        server.address = f"127.0.0.1:{server.server_port}"
        print("Starting A4P-ATH Interoperability Test.")
        print(f"Accessing ATH Shop via website: http://{server.address}", flush=True)
        server.serve_forever()
