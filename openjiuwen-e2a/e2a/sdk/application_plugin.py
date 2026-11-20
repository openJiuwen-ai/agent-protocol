# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""应用插件（Application Plugin）共享数据结构.

来源：``jiuwenswarm/extensions/sdk/application_plugin.py``。本包只保留跨仓
共享的 **dataclass 数据形状**；插件基类（``ApplicationPluginExtension`` /
``ManifestApplicationPlugin``，含 ``is_enabled`` / ``bind_web_channel`` 等
行为）与 ``ApplicationPluginServices`` 的辅助方法均属实现逻辑，由各仓实现侧
持有一份副本。
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Awaitable, Callable

WebSocketEndpoint = Callable[[Any], Awaitable[None]]


@dataclass(frozen=True)
class ApplicationPluginServices:
    """Core services made available while an application plugin binds to Gateway."""

    agent_client: Any = None
    media_attachment_normalizer: Callable[[dict[str, Any], str | None], None] | None = None


@dataclass(frozen=True)
class WebSocketRouteContribution:
    """A plugin WebSocket route, optionally retained for host-level entry points."""

    path: str
    endpoint: WebSocketEndpoint
    check_origin: bool = True
    available_when_disabled: bool = False


@dataclass(frozen=True)
class FrontendContribution:
    """A page contribution exposed to the web frontend.

    ``bundled`` entries are compiled with Jiuwen and resolved by ``component``.
    ``iframe`` entries are prebuilt assets shipped by an installable plugin.
    """

    id: str
    nav_key: str
    title: str
    title_i18n_key: str = ""
    render_mode: str = "iframe"
    component: str = ""
    entrypoint: str = ""
    position: int = 100


__all__ = [
    "ApplicationPluginServices",
    "FrontendContribution",
    "WebSocketEndpoint",
    "WebSocketRouteContribution",
]