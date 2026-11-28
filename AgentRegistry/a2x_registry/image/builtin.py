"""Built-in image seeding for appliance mode.

The appliance ships one built-in agent carrier (``jiuwenswarm``) that is not
produced by the image-processing module. Seeding it through the image
registry keeps ``GET /api/images`` the single source for the management
plane's agent cards; the query API does not special-case static entries.

The row is system-owned (``uploaded_by="system"``). When the version or
runtime spec changes, the old system row is replaced, so an upgrade never
leaves stale metadata behind.
"""

from __future__ import annotations

from importlib import metadata
from typing import Any, Dict

from .service import ImageService

# ══ 可配置常量（环境变量注入）════════════════════════════════════════
# 预置镜像版本；空 = 优先读已安装的 jiuwenswarm/workswarm 发行版版本，
# 都取不到时回退 "latest"（A2X_REGISTRY_BUILTIN_IMAGE_VERSION）
BUILTIN_IMAGE_VERSION_ENV = "A2X_REGISTRY_BUILTIN_IMAGE_VERSION"

# ══ 不可配置常量（进程内固定）════════════════════════════════════════
BUILTIN_IMAGE_NAME = "jiuwenswarm"
BUILTIN_IMAGE_FRAMEWORK = "jiuwenswarm"
BUILTIN_IMAGE_DESCRIPTION = "Built-in jiuwenswarm agent runtime"

# Fallback when neither the explicit version nor the installed distribution
# version is available (e.g. a registry-only install).
_BUILTIN_IMAGE_VERSION_FALLBACK = "latest"

# Distribution names used by jiuwenswarm builds: release wheels install as
# ``jiuwenswarm``; the source project is published as ``workswarm``.
_BUILTIN_DISTRIBUTIONS = ("jiuwenswarm", "workswarm")

# Same shape the gateway uses for the built-in agent (supervisor sandbox,
# AgentServer on 18092). The registry stores it as an opaque runtime_spec.
BUILTIN_IMAGE_RUNTIME_SPEC: Dict[str, Any] = {
    "sandbox_type": "supervisor",
    "runtime": "python3.11",
    "rootfs": {
        "imageurl": "jiuwenswarm-agent-runtime:latest",
        "user": "agentos",
    },
    "cmds": [
        ["sh", "-c", "exec jiuwenswarm-agentserver --port 18092"],
    ],
    "probes": {
        "startup": {
            "tcpSocket": {"port": 18092},
            "initialDelaySeconds": 3,
            "periodSeconds": 3,
            "timeoutSeconds": 2,
            "failureThreshold": 6,
        },
        "liveness": {
            "tcpSocket": {"port": 18092},
            "timeoutSeconds": 2,
            "failureThreshold": 3,
        },
    },
    "cpu": 2000,
    "memory": 4096,
}

# Per-user workspace/env are added by the gateway at launch time; only the
# static runtime profile belongs to the built-in image row.
BUILTIN_IMAGE_ENV_VARS = {"JIUWENSWARM_RUNTIME_PROFILE": "agentos"}


def resolve_builtin_image_version(explicit: str = "") -> str:
    """Return the version to publish: explicit > installed dist > latest."""
    value = str(explicit or "").strip()
    if value:
        return value
    for distribution in _BUILTIN_DISTRIBUTIONS:
        try:
            installed = metadata.version(distribution).strip()
        except (metadata.PackageNotFoundError, ValueError):
            continue
        if installed:
            return installed
    return _BUILTIN_IMAGE_VERSION_FALLBACK


def seed_builtin_image(svc: ImageService, *, version: str = "") -> str:
    """Create or refresh the system-owned built-in image row.

    Returns a short status string for logging (``"unchanged"`` /
    ``"created"`` / ``"updated"``).
    """
    resolved = resolve_builtin_image_version(version)
    return svc.ensure_system_image(
        name=BUILTIN_IMAGE_NAME,
        framework=BUILTIN_IMAGE_FRAMEWORK,
        version=resolved,
        description=BUILTIN_IMAGE_DESCRIPTION,
        runtime_spec=BUILTIN_IMAGE_RUNTIME_SPEC,
        access_mode=[],
        env_vars=BUILTIN_IMAGE_ENV_VARS,
    )
