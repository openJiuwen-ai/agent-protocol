# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""审计打点字段契约（Gateway ↔ 企业版 telemetry）。

来源：``gateway/enterprise/capabilities/audit/audit_emit.py``。gateway 侧不能
依赖企业版私有库，因此把「事件类型 / 字段名 / 响应码 / 等级」等契约固化在
本包，由 provider 注入具体打点实现。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

# ── 事件类型（``event_type`` 首参）──────────────────────────────────────────
AUDIT_EVENT_USER_ACTION: str = "UA"
AUDIT_EVENT_EVENT: str = "EVT"

# ── 业务字段名（大写，作为 kwargs / 顶层映射键）────────────────────────────
AUDIT_FIELD_SUBMDL: str = "SUBMDL"
AUDIT_FIELD_PROC: str = "PROC"
AUDIT_FIELD_RSPCD: str = "RSPCD"
AUDIT_FIELD_UA: str = "UA"
AUDIT_FIELD_EVT: str = "EVT"
AUDIT_FIELD_MSG: str = "MSG"
AUDIT_FIELD_UID: str = "UID"
AUDIT_FIELD_UID_LOWER: str = "uid"
AUDIT_FIELD_SESSION_ID: str = "session_id"
AUDIT_FIELD_REQUEST_ID: str = "request_id"

#: 必须进 ``audit_claw_log`` 顶层映射入参、不能只塞 extra 的字段
#: （否则无 ContextVar 时 trace_id/txn_seq/UID 仍为占位）。
AUDIT_LIFTED_FIELDS: tuple[str, ...] = (
    AUDIT_FIELD_UA,
    AUDIT_FIELD_EVT,
    AUDIT_FIELD_MSG,
    AUDIT_FIELD_UID,
    AUDIT_FIELD_UID_LOWER,
    AUDIT_FIELD_SESSION_ID,
    AUDIT_FIELD_REQUEST_ID,
)

# ── 响应码 / 等级 ──────────────────────────────────────────────────────────
AUDIT_RSPCD_SUCCESS: str = "0000"
AUDIT_RSPCD_FAILURE: str = "E999"
AUDIT_LEVEL_INFO: str = "INFO"
AUDIT_LEVEL_WARN: str = "WARN"


@dataclass
class AuditEvent:
    """一条审计事件（provider 实现的入参形状）。

    成功/失败以 ``rspcd`` 为准：``AUDIT_RSPCD_SUCCESS`` 表示成功，其余取值
    （如 ``AUDIT_RSPCD_FAILURE``）表示失败。``success`` 只是实现侧的便捷标记，
    与 ``rspcd`` 冗余；落盘、检索与告警一律读 ``rspcd``，两者不一致时以
    ``rspcd`` 为准，实现侧应尽量由 ``rspcd`` 推导 ``success`` 以避免漂移。
    """

    submdl: str
    proc: str
    level: str = AUDIT_LEVEL_INFO
    #: 权威字段：成功/失败判定以此为准。
    rspcd: str = AUDIT_RSPCD_SUCCESS
    #: 便捷标记，与 ``rspcd`` 冗余；不一致时以 ``rspcd`` 为准（见类文档）。
    success: bool = True
    uid: str | None = None
    session_id: str | None = None
    request_id: str | None = None
    desc: str | None = None
    message: str = ""
    caller: str = "-"
    extra: dict[str, Any] = field(default_factory=dict)


__all__ = [
    "AUDIT_EVENT_EVENT",
    "AUDIT_EVENT_USER_ACTION",
    "AUDIT_FIELD_EVT",
    "AUDIT_FIELD_MSG",
    "AUDIT_FIELD_PROC",
    "AUDIT_FIELD_REQUEST_ID",
    "AUDIT_FIELD_RSPCD",
    "AUDIT_FIELD_SESSION_ID",
    "AUDIT_FIELD_SUBMDL",
    "AUDIT_FIELD_UA",
    "AUDIT_FIELD_UID",
    "AUDIT_FIELD_UID_LOWER",
    "AUDIT_LEVEL_INFO",
    "AUDIT_LEVEL_WARN",
    "AUDIT_LIFTED_FIELDS",
    "AUDIT_RSPCD_FAILURE",
    "AUDIT_RSPCD_SUCCESS",
    "AuditEvent",
]
