# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

"""YuanrongSandboxClient — openYuanRong Frontend 沙箱管理 HTTP 客户端.

从 jiuwenswarm 仓 ``jiuwenswarm/extensions/yuanrong_frontend_client.py``
（YuanrongFrontendAgentClient）复制裁剪而来，仅保留注册中心直连元戎所需的
沙箱管理子集；两仓无共享依赖，故为受控复制（升级时以来源文件为基准比对）。

保留的能力（与来源实现行为一致）：
- ``create_sandbox``   POST   /api/agent                常驻实例创建（inline 模式）
- ``delete_sandbox``   DELETE /api/agent/:instanceId    实例销毁（404 幂等视作成功）
- ``get_agent_info``   GET    /api/agent/:instanceId    实例信息（node_ip / sandbox_ip / status）
- ``wait_until_running``      轮询 GET 直到显式 status=running（探针通过）

裁剪掉的能力（gateway 专属，注册中心不需要）：
- faas invocation（send_request / send_request_stream / SSE / E2A envelope）
- 实例文件接口（upload / download / list / mkdir）
- 会话并发控制（X-Instance-Session / X-Session-Context）

差异适配（有意为之）：
- 无连接态（connect/disconnected）：注册中心启动时构造一次、endpoint 非空校验在装配处完成
- ``wait_until_running`` 超时抛 ``YuanrongAgentTimeoutError``（来源为 ApiError），
  便于调用方区分「等待超时」与「元戎失败」两种 HTTP 语义（504 vs 502）
- 阻塞的 urllib 调用一律包在 ``asyncio.to_thread``，不阻塞事件循环
"""

from __future__ import annotations

import asyncio
import json
import logging
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any, Mapping, TypedDict

from a2x_registry.instance.constants import (
    AGENT_FAILED_STATUSES,
    AGENT_RUNNING_STATUS,
    YUANRONG_NAMESPACE_DEFAULT,
    YUANRONG_TIMEOUT_S_DEFAULT,
    YUANRONG_WAIT_INTERVAL_S_DEFAULT,
    YUANRONG_WAIT_RUNNING_S_DEFAULT,
)

logger = logging.getLogger(__name__)


class AgentMount(TypedDict, total=False):
    """Bind mount for POST /api/agent ``mounts``."""

    source: str
    target: str
    readonly: bool


class AgentRootfsSpec(TypedDict, total=False):
    """Inline ``runtime_spec.rootfs`` for POST /api/agent."""

    imageurl: str
    user: str
    ports: list[str]


class AgentRuntimeSpec(TypedDict, total=False):
    """Inline ``runtime_spec`` for POST /api/agent (bypass meta_service)."""

    runtime: str
    sandbox_type: str
    rootfs: AgentRootfsSpec
    cpu: int
    memory: int
    code_path: str
    cmds: list[list[str]]
    probes: dict[str, Any]


@dataclass
class SandboxInfo:
    """YuanRong agent instance lifecycle record returned by /api/agent."""

    sandbox_id: str
    status: str = "ready"
    metadata: dict[str, Any] = None  # type: ignore[assignment]

    def __post_init__(self) -> None:
        if self.metadata is None:
            self.metadata = {}


class YuanrongAgentApiError(RuntimeError):
    """Raised when YuanRong /api/agent returns a non-success response."""


class YuanrongAgentTimeoutError(YuanrongAgentApiError):
    """请求已发出但等待响应超时（请求可能已在服务端生效，调用方需按幂等处理）。"""


def _normalize_port_probe(value: Any) -> dict[str, Any] | None:
    """Normalize a TCP port label to ``{"port": int, "protocol": "tcp"}``."""
    if value is None or value is False:
        return None
    if isinstance(value, Mapping):
        raw_port = value.get("port")
        if raw_port is None and isinstance(value.get("tcpSocket"), Mapping):
            raw_port = value["tcpSocket"].get("port")
        protocol = str(value.get("protocol") or "tcp").strip().lower() or "tcp"
        try:
            port = int(raw_port)
        except (TypeError, ValueError):
            return None
        if port <= 0:
            return None
        return {"port": port, "protocol": protocol}
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        if value <= 0:
            return None
        return {"port": int(value), "protocol": "tcp"}
    text = str(value).strip()
    if not text:
        return None
    protocol = "tcp"
    port_str = text
    if ":" in text:
        proto, port_str = text.split(":", 1)
        protocol = proto.strip().lower() or "tcp"
        port_str = port_str.strip()
    try:
        port = int(port_str)
    except ValueError:
        return None
    if port <= 0:
        return None
    return {"port": port, "protocol": protocol}


def _port_probe_from_ports(ports: Any) -> dict[str, Any] | None:
    """Build a port label from ``rootfs.ports`` such as ``tcp:18092``."""
    if not isinstance(ports, list) or not ports:
        return None
    return _normalize_port_probe(ports[0])


def _normalize_probes(value: Any) -> dict[str, Any] | None:
    """Keep ``startup`` / ``liveness`` / ``readiness`` objects if present."""
    if not isinstance(value, Mapping) or not value:
        return None
    probes: dict[str, Any] = {}
    for key in ("startup", "liveness", "readiness"):
        item = value.get(key)
        if isinstance(item, Mapping) and item:
            probes[key] = dict(item)
    return probes or None


def _startup_tcp_probe(port: int) -> dict[str, Any]:
    return {
        "tcpSocket": {"port": int(port)},
        "initialDelaySeconds": 3,
        "periodSeconds": 3,
        "timeoutSeconds": 2,
        "failureThreshold": 6,
    }


def _probes_from_port(port: int) -> dict[str, Any]:
    return {"startup": _startup_tcp_probe(port)}


def _instance_status(instance: Mapping[str, Any] | None) -> str:
    if not isinstance(instance, Mapping):
        return ""
    return str(instance.get("status") or instance.get("state") or "").strip().lower()


def _is_agent_running(instance: Mapping[str, Any] | None) -> bool:
    """True only when GET explicitly reports ``status=running``.

    A non-empty instance without ``status`` is **not** ready: keep polling
    until status is running, a failed status, or timeout.
    """
    if not isinstance(instance, Mapping) or not instance:
        return False
    return _instance_status(instance) == AGENT_RUNNING_STATUS


class YuanrongSandboxClient:
    """openYuanRong Frontend 沙箱管理客户端（注册中心专用子集）.

    经 POST/DELETE/GET /api/agent 管理常驻 agent 实例；
    阻塞 HTTP（urllib）全部经 ``asyncio.to_thread`` 执行。
    """

    def __init__(
        self,
        *,
        frontend_endpoint: str,
        agent_timeout_s: float = YUANRONG_TIMEOUT_S_DEFAULT,
        agent_namespace: str = YUANRONG_NAMESPACE_DEFAULT,
        wait_running_timeout_s: float = YUANRONG_WAIT_RUNNING_S_DEFAULT,
        wait_running_interval_s: float = YUANRONG_WAIT_INTERVAL_S_DEFAULT,
    ) -> None:
        endpoint = (frontend_endpoint or "").strip().rstrip("/")
        if not endpoint:
            raise ValueError("frontend_endpoint cannot be empty")
        self._frontend_endpoint = endpoint
        self._agent_timeout_s = float(agent_timeout_s)
        self._agent_namespace = (
            str(agent_namespace or YUANRONG_NAMESPACE_DEFAULT).strip()
            or YUANRONG_NAMESPACE_DEFAULT
        )
        self._wait_running_timeout_s = float(wait_running_timeout_s)
        self._wait_running_interval_s = float(wait_running_interval_s)

    @property
    def agent_namespace(self) -> str:
        return self._agent_namespace

    @property
    def frontend_endpoint(self) -> str:
        return self._frontend_endpoint

    @property
    def wait_running_timeout_s(self) -> float:
        return self._wait_running_timeout_s

    # ── 沙箱管理接口 ────────────────────────────────────────────────

    def _normalize_runtime_spec(
        self,
        runtime_spec: AgentRuntimeSpec | Mapping[str, Any] | None,
    ) -> dict[str, Any]:
        """Normalize inline ``runtime_spec`` for POST /api/agent."""
        if not isinstance(runtime_spec, Mapping):
            raise ValueError("runtime_spec is required to create sandbox")
        runtime = str(runtime_spec.get("runtime") or "").strip()
        rootfs_raw = runtime_spec.get("rootfs")
        if not isinstance(rootfs_raw, Mapping):
            raise ValueError("runtime_spec.rootfs is required to create sandbox")
        imageurl = str(
            rootfs_raw.get("imageurl") or rootfs_raw.get("image_url") or ""
        ).strip()
        if not runtime:
            raise ValueError("runtime_spec.runtime is required to create sandbox")
        if not imageurl:
            raise ValueError(
                "runtime_spec.rootfs.imageurl is required to create sandbox"
            )

        rootfs: dict[str, Any] = {"imageurl": imageurl}
        user = str(rootfs_raw.get("user") or "").strip()
        if user:
            rootfs["user"] = user
        ports = rootfs_raw.get("ports")
        if isinstance(ports, list) and ports:
            rootfs["ports"] = [str(port) for port in ports]

        normalized: dict[str, Any] = {"runtime": runtime, "rootfs": rootfs}
        sandbox_type = str(runtime_spec.get("sandbox_type") or "").strip()
        if sandbox_type:
            normalized["sandbox_type"] = sandbox_type
        if runtime_spec.get("cpu") is not None:
            normalized["cpu"] = int(runtime_spec["cpu"])
        if runtime_spec.get("memory") is not None:
            normalized["memory"] = int(runtime_spec["memory"])
        cmds = runtime_spec.get("cmds")
        if isinstance(cmds, list) and cmds:
            normalized["cmds"] = cmds
        probes = _normalize_probes(runtime_spec.get("probes"))
        if probes is None:
            legacy = _normalize_port_probe(
                runtime_spec.get("port_probe")
            ) or _port_probe_from_ports(rootfs.get("ports"))
            if legacy is not None:
                probes = _probes_from_port(int(legacy["port"]))
        if probes is not None:
            normalized["probes"] = probes
        return normalized

    async def create_sandbox(
        self,
        *,
        namespace: str,
        name: str,
        workspace: str,
        runtime_spec: AgentRuntimeSpec | Mapping[str, Any],
        env_vars: dict[str, str] | None = None,
        mounts: list[AgentMount] | None = None,
    ) -> SandboxInfo:
        """Create a detached agent instance via POST /api/agent (inline mode).

        - ``namespace`` / ``name`` / ``workspace`` / ``runtime_spec``: required
        - ``runtime_spec.runtime`` + ``runtime_spec.rootfs.imageurl``: required
        - ``env_vars`` / ``mounts``: optional
        - ``runtime_spec.probes``: optional startup / liveness probes
          (Frontend marks ``running`` after startup succeeds)
        """
        normalized_namespace = str(namespace or "").strip()
        normalized_name = str(name or "").strip()
        normalized_workspace = str(workspace or "").strip()
        if not normalized_namespace:
            raise ValueError("namespace is required to create sandbox")
        if not normalized_name:
            raise ValueError("name is required to create sandbox")
        if not normalized_workspace:
            raise ValueError("workspace is required to create sandbox")
        if not normalized_workspace.startswith("/"):
            raise ValueError("workspace must be an absolute path")

        normalized_runtime_spec = self._normalize_runtime_spec(runtime_spec)
        payload: dict[str, Any] = {
            "namespace": normalized_namespace,
            "name": normalized_name,
            "workspace": normalized_workspace,
            "runtime_spec": normalized_runtime_spec,
        }

        if env_vars:
            payload["env_vars"] = {
                str(key): str(value) for key, value in dict(env_vars).items()
            }

        if mounts:
            payload["mounts"] = list(mounts)

        status, body = await asyncio.to_thread(self._do_agent_create, payload)
        parsed = self._parse_agent_api_response(body, status)
        instance_id = str(parsed.get("instance_id") or "").strip()
        if not instance_id:
            raise YuanrongAgentApiError(
                f"create agent missing instance_id: status={status}, body={body!r}"
            )

        info = SandboxInfo(
            sandbox_id=instance_id,
            status="ready",
            metadata={
                "instance_id": instance_id,
                "namespace": normalized_namespace,
                "name": normalized_name,
                "workspace": normalized_workspace,
                "runtime_spec": dict(normalized_runtime_spec),
                "env_vars": dict(payload.get("env_vars") or {}),
                "mounts": list(payload.get("mounts") or []),
                "provisioning": "yuanrong_agent_api_inline",
            },
        )
        logger.info(
            "[YuanrongSandboxClient] create_sandbox: "
            "instance_id=%s name=%s namespace=%s runtime=%s imageurl=%s",
            instance_id,
            normalized_name,
            normalized_namespace,
            normalized_runtime_spec.get("runtime"),
            (normalized_runtime_spec.get("rootfs") or {}).get("imageurl"),
        )
        return info

    async def delete_sandbox(self, sandbox_id: str) -> None:
        """Destroy a detached agent instance via DELETE /api/agent/:instanceId.

        元戎已无该实例（HTTP 404 或 body code=404）视作成功（幂等）。
        """
        normalized_sandbox_id = str(sandbox_id or "").strip()
        if not normalized_sandbox_id:
            raise ValueError("sandbox_id is required to delete sandbox")

        status, body = await asyncio.to_thread(
            self._do_agent_delete,
            normalized_sandbox_id,
        )
        if self._agent_api_not_found(status, body):
            logger.info(
                "[YuanrongSandboxClient] delete_sandbox: already gone instance_id=%s",
                normalized_sandbox_id,
            )
            return
        self._parse_agent_api_response(body, status)
        logger.info(
            "[YuanrongSandboxClient] delete_sandbox: instance_id=%s",
            normalized_sandbox_id,
        )

    async def get_agent_info(self, instance_id: str) -> dict[str, Any]:
        """Query agent instance info via GET /api/agent/:instanceId.

        Returns the ``instance`` dict (contains node_ip, sandbox_ip,
        sandbox_type, rootfs, workspace, env_vars, status, etc.).
        """
        normalized_id = str(instance_id or "").strip()
        if not normalized_id:
            raise ValueError("instance_id is required to get agent info")
        status, body = await asyncio.to_thread(self._do_agent_get, normalized_id)
        parsed = self._parse_agent_api_response(body, status)
        instance = parsed.get("instance")
        if not isinstance(instance, dict):
            instance = {}
        else:
            instance = dict(instance)
        top_status = str(parsed.get("status") or parsed.get("state") or "").strip()
        if top_status and not str(instance.get("status") or "").strip():
            instance["status"] = top_status
        return instance

    async def wait_until_running(self, instance_id: str) -> dict[str, Any]:
        """Poll GET /api/agent/:id until ``status`` is explicitly ``running``.

        Create is asynchronous: the port probe completes after POST returns
        ``instance_id``. Missing ``status`` is not treated as ready: keep
        polling. Only an explicit ``running`` (or a failed status / timeout)
        ends the loop. Deadline exceeded raises ``YuanrongAgentTimeoutError``
        (调用方回 504）；失败状态抛 ``YuanrongAgentApiError``（回 502）。
        """
        normalized_id = str(instance_id or "").strip()
        if not normalized_id:
            raise ValueError("instance_id is required to wait for running")
        timeout_s = float(self._wait_running_timeout_s)
        interval_s = float(self._wait_running_interval_s)
        deadline = asyncio.get_running_loop().time() + timeout_s
        attempt = 0
        last_error: BaseException | None = None
        last_status = ""
        while True:
            attempt += 1
            instance: dict[str, Any] = {}
            try:
                instance = await self.get_agent_info(normalized_id)
                last_error = None
            except YuanrongAgentApiError as exc:
                last_error = exc
            status = _instance_status(instance)
            last_status = status
            if status in AGENT_FAILED_STATUSES:
                raise YuanrongAgentApiError(
                    f"agent instance failed: instance_id={normalized_id}, "
                    f"status={status}"
                )
            if _is_agent_running(instance):
                if attempt > 1:
                    logger.info(
                        "[YuanrongSandboxClient] instance running after GET poll: "
                        "instance_id=%s attempt=%s status=%s",
                        normalized_id,
                        attempt,
                        status or AGENT_RUNNING_STATUS,
                    )
                return instance
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                detail = (
                    str(last_error)
                    if last_error is not None
                    else f"status={last_status or 'empty'}"
                )
                raise YuanrongAgentTimeoutError(
                    f"agent instance not running after "
                    f"{timeout_s:.0f}s: "
                    f"instance_id={normalized_id}, last={detail}"
                )
            sleep_for = min(interval_s, remaining)
            logger.debug(
                "[YuanrongSandboxClient] GET not running yet: "
                "instance_id=%s attempt=%s status=%s sleep=%.1fs last_error=%s",
                normalized_id,
                attempt,
                status or "empty",
                sleep_for,
                type(last_error).__name__ if last_error is not None else "-",
            )
            await asyncio.sleep(sleep_for)

    # ── HTTP 底层（阻塞 urllib，由 to_thread 包裹）───────────────────

    def _agent_create_url(self) -> str:
        return f"{self._frontend_endpoint}/api/agent"

    def _agent_delete_url(self, instance_id: str) -> str:
        encoded = urllib.parse.quote(instance_id, safe="")
        return f"{self._frontend_endpoint}/api/agent/{encoded}"

    def _do_agent_create(self, payload: dict[str, Any]) -> tuple[int, str]:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        req = urllib.request.Request(
            self._agent_create_url(),
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        return self._urlopen_request(
            req,
            timeout=self._agent_timeout_s,
            raise_on_timeout=True,
        )

    def _do_agent_delete(self, instance_id: str) -> tuple[int, str]:
        req = urllib.request.Request(
            self._agent_delete_url(instance_id),
            headers={"Content-Type": "application/json"},
            method="DELETE",
        )
        return self._urlopen_request(
            req,
            timeout=self._agent_timeout_s,
            raise_on_timeout=True,
        )

    def _do_agent_get(self, instance_id: str) -> tuple[int, str]:
        req = urllib.request.Request(
            self._agent_delete_url(instance_id),  # same URL: /api/agent/{instanceId}
            headers={"Content-Type": "application/json"},
            method="GET",
        )
        return self._urlopen_request(
            req,
            timeout=self._agent_timeout_s,
        )

    @staticmethod
    def _parse_agent_api_response(body: str, status: int) -> dict[str, Any]:
        try:
            parsed = json.loads(body) if body else {}
        except Exception as exc:
            raise YuanrongAgentApiError(
                f"invalid agent API response: status={status}, body={body!r}"
            ) from exc
        if not isinstance(parsed, dict):
            raise YuanrongAgentApiError(
                f"invalid agent API response shape: status={status}, body={body!r}"
            )
        code = parsed.get("code")
        if not (200 <= status < 300) or code not in (200, "200"):
            message = parsed.get("message") or parsed.get("status") or body
            raise YuanrongAgentApiError(
                f"agent API failed: http_status={status}, code={code}, message={message!r}"
            )
        return parsed

    @staticmethod
    def _agent_api_not_found(status: int, body: str) -> bool:
        if int(status) == 404:
            return True
        try:
            parsed = json.loads(body) if body else {}
        except Exception:
            return False
        if not isinstance(parsed, dict):
            return False
        return parsed.get("code") in (404, "404")

    def _urlopen_request(
        self,
        req: urllib.request.Request,
        *,
        timeout: float | None = None,
        raise_on_timeout: bool = False,
    ) -> tuple[int, str]:
        resolved_timeout = self._agent_timeout_s if timeout is None else float(timeout)
        try:
            with urllib.request.urlopen(req, timeout=resolved_timeout) as resp:
                status = int(getattr(resp, "status", 200))
                text = resp.read().decode("utf-8", errors="replace")
                return status, text
        except urllib.error.HTTPError as err:
            text = err.read().decode("utf-8", errors="replace") if err.fp else str(err)
            logger.error(
                "[YuanrongSandboxClient] HTTP error: url=%s code=%d",
                req.full_url,
                getattr(err, "code", 500),
            )
            return int(getattr(err, "code", 500) or 500), text
        except Exception as err:
            logger.error(
                "[YuanrongSandboxClient] request failed: url=%s error=%s",
                req.full_url,
                str(err),
            )
            if raise_on_timeout and self._is_timeout_error(err):
                raise YuanrongAgentTimeoutError(
                    f"request timeout after {resolved_timeout}s: "
                    f"url={req.full_url}, error={err}"
                ) from err
            return 500, str(err)

    @staticmethod
    def _is_timeout_error(err: BaseException) -> bool:
        if isinstance(err, TimeoutError):
            return True
        reason = getattr(err, "reason", None)
        if isinstance(reason, TimeoutError):
            return True
        text = str(err).lower()
        return "timed out" in text or "timeout" in type(err).__name__.lower()
