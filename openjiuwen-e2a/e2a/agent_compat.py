# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.

"""AgentServer：E2AEnvelope → AgentRequest（E2A 兼容转换）。

来源：``jiuwenswarm/common/e2a/agent_compat.py``。两仓共用同一份转换逻辑：
Gateway 产出 E2A 信封、AgentServer 消费为 ``AgentRequest``，任何一侧单独改动
转换规则都会造成线上行为不一致，故收敛到协议包，两仓只保留转发别名。

不得与 ``gateway_normalize`` 的 ``normalize_failed`` 兜底同时使用：含兜底标记的
信封须由调用方先走 legacy 分支。
"""

from __future__ import annotations

import logging
from datetime import datetime
from typing import Any

from e2a.agent_models import AgentRequest
from e2a.gateway_normalize import E2A_INTERNAL_CONTEXT_KEY
from e2a.identity import apply_routing_metadata, web_routing_identity
from e2a.message import ReqMethod
from e2a.models import E2AEnvelope

logger = logging.getLogger(__name__)


def _e2a_timestamp_to_float(ts: str | None) -> float:
    if not ts:
        return 0.0
    try:
        s = ts.replace("Z", "+00:00") if ts.endswith("Z") else ts
        return datetime.fromisoformat(s).timestamp()
    except Exception:  # noqa: BLE001
        return 0.0


def _routing_identity(env: E2AEnvelope, context: dict[str, Any]) -> dict[str, str]:
    """Fill missing routing fields from the envelope.

    ``user_id`` comes from the authenticated envelope when channel metadata
    omits it; non-web channels fall back to ``chat_id`` for ``group_id``.
    """
    identity = web_routing_identity(context)
    if env.user_id and not identity.get("user_id"):
        identity["user_id"] = env.user_id
    if env.channel != "web" and env.chat_id and not identity.get("group_id"):
        identity["group_id"] = env.chat_id
    return identity


def e2a_to_agent_request(env: E2AEnvelope) -> AgentRequest:
    """将规范化成功的 E2A 信封转换为 ``AgentRequest``。

    路由身份按 ``e2a.identity`` 写回 metadata：``user_id`` 放顶层，
    ``group_id`` / ``bot_id`` / ``gateway_id`` 进 ``routing``。
    若 envelope 含 Gateway 兜底标记，须由调用方先分支处理 legacy，勿调用本函数。
    """
    ctx = dict(env.channel_context or {})
    internal = ctx.pop(E2A_INTERNAL_CONTEXT_KEY, None)
    if isinstance(internal, dict) and internal.get("normalize_failed"):
        raise RuntimeError("e2a_to_agent_request called on fallback envelope; use legacy path")

    identity = _routing_identity(env, ctx)
    ctx = apply_routing_metadata(ctx, identity)
    metadata = ctx if ctx else None
    method_str = env.method
    req_method: ReqMethod | str | None = None
    if method_str:
        try:
            req_method = ReqMethod(method_str)
        except ValueError:
            # E2A carries method names as strings.  Keep an unknown method
            # intact so a newer AgentServer can receive it through an older
            # Gateway; the AgentServer remains responsible for deciding
            # whether the method is implemented.
            req_method = method_str
            logger.debug(
                "[E2A][compat] preserving extension method=%r request_id=%s",
                method_str,
                env.request_id,
            )

    return AgentRequest(
        request_id=env.request_id or "",
        channel_id=env.channel or "web",
        session_id=env.session_id,
        chat_id=env.chat_id,
        req_method=req_method,
        params=dict(env.params or {}),
        is_stream=bool(env.is_stream),
        timestamp=_e2a_timestamp_to_float(env.timestamp),
        metadata=metadata,
        # 透传 agent_ref 到 AgentServer，供响应侧 chunk/response 回带。
        agent_ref=env.agent_ref,
        # ``user_id`` 是 Gateway 鉴权后的路由身份，不是通道元数据；跨 E2A 以字符串
        # 透传，metadata 缺失时回退到 channel_context 中的身份。
        user_id=str(env.user_id or identity.get("user_id") or ""),
    )


__all__ = ["e2a_to_agent_request"]
