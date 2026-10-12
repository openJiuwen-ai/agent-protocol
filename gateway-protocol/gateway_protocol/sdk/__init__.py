# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""扩展 SDK 共享数据结构汇总导出.

本包只保留跨仓共享的 **dataclass 数据形状**；扩展基类
（``BaseExtension`` / ``AgentServerClientExtension`` / ``CryptoUtility`` /
``ApplicationPluginExtension`` / ``ThirdAgentExtension`` 等）与
``CryptoProvider`` 协议均属接口/实现逻辑，由各仓实现侧持有一份副本。
"""

from __future__ import annotations

from gateway_protocol.sdk.application_plugin import (
    ApplicationPluginServices,
    FrontendContribution,
    WebSocketEndpoint,
    WebSocketRouteContribution,
)

__all__ = [
    "ApplicationPluginServices",
    "FrontendContribution",
    "WebSocketEndpoint",
    "WebSocketRouteContribution",
]