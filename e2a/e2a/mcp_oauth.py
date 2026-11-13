# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.

"""MCP OAuth 回调的共享契约（不含 provider 实现与 provider 查找）。

来源：``jiuwenswarm/common/mcp_oauth.py``。两仓都会把 OAuth 回调交给当前进程
注册的 ``mcp_registry`` provider 执行，失败语义必须一致：

- 异常类型 ``McpOAuthError`` 是"可展示给用户的预期失败"与"内部崩溃"的分界，
  Gateway 侧回调路由据此选择 400 / 500 落地页；
- provider 域名常量避免两侧字面量漂移。

provider 查找依赖各进程自己的 ``external_providers`` 注册表，故
``complete_oauth_flow`` / ``complete_oauth_flow_async`` 仍在两仓实现侧各持一份
薄封装（``~6`` 行），只引用本模块的常量与异常类型。
"""

from __future__ import annotations


class McpOAuthError(RuntimeError):
    """An expected authorization failure safe to display on the callback page."""


#: 当前进程注册 MCP OAuth 回调实现所用的 provider 域名。
MCP_REGISTRY_PROVIDER_DOMAIN = "mcp_registry"

__all__ = ["MCP_REGISTRY_PROVIDER_DOMAIN", "McpOAuthError"]
