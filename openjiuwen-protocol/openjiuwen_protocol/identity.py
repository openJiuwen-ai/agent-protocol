# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""请求身份字段契约（Web → Gateway → AgentServer 全链路）。

来源：``jiuwenswarm/common/request_identity.py``。字段名字符串会同时出现在
WS query、HTTP 头、``metadata``、E2A ``channel_context.routing`` 与企业版租户
查询参数中，是跨进程契约。

除字段常量外，本模块还固化三个**零依赖纯函数**：gateway 写侧
``apply_routing_metadata`` 与读侧 ``normalize_routing_identity`` /
``web_routing_identity``。读写两侧必须对「缺字段 / 空串 / 非字符串」采用同一
口径，分叉会导致身份在进程间丢失，故一并收口。``merge_routing_into_params``
是 gateway 本地 handler 的适配层，不属双端契约，留在实现侧。
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

#: ``metadata`` 中承载路由维度的键。
ROUTING_METADATA_KEY: str = "routing"
#: 说话人标识（放在 ``metadata`` 顶层，不放进 routing）。
USER_ID_FIELD: str = "user_id"
#: 仅这些字段写入 ``metadata.routing``。
WEB_ROUTING_ID_FIELDS: tuple[str, ...] = ("group_id", "bot_id", "gateway_id")
#: 完整身份字段（顶层 user_id + routing 维度）。
WEB_IDENTITY_FIELDS: tuple[str, ...] = (USER_ID_FIELD, *WEB_ROUTING_ID_FIELDS)
#: 企业版租户路由/查询字段（HTTP query 与 E2A routing 共用）。
TENANT_ROUTING_FIELDS: tuple[str, ...] = (
    "group_id",
    "bot_id",
    "user_id",
    "service_id",
    "agent_id",
)


def _identity_value(value: Any) -> str | None:
    """归一化单个字段值：列表取首元素，去空白，空串视为未提供。"""
    if isinstance(value, (list, tuple)):
        value = value[0] if value else None
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def _pick_field(mapping: Mapping[str, Any], field: str) -> str | None:
    return _identity_value(mapping.get(field))


def normalize_routing_identity(
    *sources: Mapping[str, Any] | None,
) -> dict[str, str]:
    """从若干 mapping 归一化完整身份（含 ``user_id``）；同一字段以先出现的非空值为准。"""
    identity: dict[str, str] = {}
    for source in sources:
        if not isinstance(source, Mapping):
            continue
        for field in WEB_IDENTITY_FIELDS:
            if field in identity:
                continue
            value = _pick_field(source, field)
            if value:
                identity[field] = value
    return identity


def apply_routing_metadata(
    metadata: Mapping[str, Any] | None,
    routing: Mapping[str, str] | None,
) -> dict[str, Any]:
    """写入身份：``user_id`` → 顶层；``group_id``/``bot_id``/``gateway_id`` → ``routing``。"""
    meta = dict(metadata or {})
    identity = dict(routing or {})

    for field in WEB_ROUTING_ID_FIELDS:
        meta.pop(field, None)

    if USER_ID_FIELD in identity:
        user_id = _identity_value(identity.get(USER_ID_FIELD))
        if user_id:
            meta[USER_ID_FIELD] = user_id
        else:
            meta.pop(USER_ID_FIELD, None)

    cleaned = {
        field: value
        for field, value in identity.items()
        if field in WEB_ROUTING_ID_FIELDS and str(value or "").strip()
    }
    if cleaned:
        meta[ROUTING_METADATA_KEY] = cleaned
    else:
        meta.pop(ROUTING_METADATA_KEY, None)
    return meta


def web_routing_identity(metadata: Mapping[str, Any] | None) -> dict[str, str]:
    """读完整身份：顶层 ``user_id`` + ``metadata.routing`` 三字段。"""
    if not isinstance(metadata, Mapping):
        return {}
    identity: dict[str, str] = {}
    user_id = _pick_field(metadata, USER_ID_FIELD)
    if user_id:
        identity[USER_ID_FIELD] = user_id
    routing = metadata.get(ROUTING_METADATA_KEY)
    if isinstance(routing, Mapping):
        for field in WEB_ROUTING_ID_FIELDS:
            value = _pick_field(routing, field)
            if value:
                identity[field] = value
    return identity


__all__ = [
    "ROUTING_METADATA_KEY",
    "TENANT_ROUTING_FIELDS",
    "USER_ID_FIELD",
    "WEB_IDENTITY_FIELDS",
    "WEB_ROUTING_ID_FIELDS",
    "apply_routing_metadata",
    "normalize_routing_identity",
    "web_routing_identity",
]
