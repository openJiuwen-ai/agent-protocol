# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""cron 共享模型（gateway 与 Agent Runtime 两仓共用）。

来源：``jiuwenswarm/runtime/cron/models.py``（gateway 侧 ``gateway/cron/models.py``
现为别名转发，是 gateway→jiuwenswarm 的反向依赖点，protocol 化后两侧同时解开）。

边界说明（第一版）：
- 纯数据模型（``CronTargetChannel`` / ``CronTarget`` / ``CronJob`` / ``CronRunState``）
  与无实现依赖的常量、规范函数进本包；
- 原 models.py 中依赖 jiuwenswarm 实现的函数（``validate_cron_model`` 等，
  依赖 ``cron_expr`` 校验、``work_mode``/``mode_matrix``）**不进本包**，留在
  各仓实现侧（本阶段目标为模块边界确认，非功能全量迁移）。两仓切换时以
  本包模型为准，实现侧函数签名不变。
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Any

# 与 jiuwenswarm ``common/work_mode.py`` 的同名常量保持一致（暂不共享定义，
# 两仓分叉期以同名同值方便追踪；gateway 仓拉起后统一收敛到本包）。
DEFAULT_WEB_WORK_MODE: str = "work"


class CronTargetChannel(str, Enum):
    """推送频道枚举。"""
    WEB = "web"
    TUI = "tui"
    FEISHU = "feishu"
    WHATSAPP = "whatsapp"
    WECOM = "wecom"
    XIAOYI = "xiaoyi"
    WECHAT = "wechat"
    DINGTALK = "dingtalk"


def _feishu_enterprise_app_id(s: str) -> str:
    """feishu_enterprise 通道键仅为 feishu_enterprise:<app_id>；忽略 :chat: 等后续后缀。"""
    parts = str(s or "").strip().split(":")
    if len(parts) < 2 or parts[0].strip().lower() != "feishu_enterprise":
        return ""
    return parts[1].strip()


def is_valid_target_channel_id(raw: str) -> bool:
    s = str(raw or "").strip()
    if not s:
        return False
    if s.startswith("feishu_enterprise:"):
        return bool(_feishu_enterprise_app_id(s))
    try:
        CronTargetChannel(s.lower())
        return True
    except ValueError:
        return False


def normalize_target_channel_id(
    raw: str, *, default: str = CronTargetChannel.WEB.value
) -> str:
    s = str(raw or "").strip()
    if not s:
        return default
    if s.startswith("feishu_enterprise:"):
        app_id = _feishu_enterprise_app_id(s)
        if app_id:
            return f"feishu_enterprise:{app_id}"
        return default
    low = s.lower()
    try:
        return CronTargetChannel(low).value
    except ValueError:
        return default


def _normalize_targets_str(raw: str) -> str:
    """将 targets 字符串规范为 CronTargetChannel 枚举值，非法则默认 web。"""
    return normalize_target_channel_id(raw, default=CronTargetChannel.WEB.value)


# 名称/描述最大长度（前后端保持一致，见 CronTaskDrawer.tsx 同名常量）。
CRON_JOB_NAME_MAX_LENGTH: int = 64
CRON_JOB_DESCRIPTION_MAX_LENGTH: int = 500

# Canonical default when create/update/runtime do not specify mode.
CRON_JOB_DEFAULT_MODE: str = "agent"


@dataclass(frozen=True)
class CronTarget:
    """Where to push cron results."""

    channel_id: str
    session_id: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "channel_id": self.channel_id,
            "session_id": self.session_id,
        }

    @staticmethod
    def from_dict(data: dict[str, Any]) -> "CronTarget":
        channel_id = str(data.get("channel_id") or "").strip()
        session_id_raw = data.get("session_id", None)
        session_id = (
            str(session_id_raw).strip() if isinstance(session_id_raw, str) else None
        )
        if not channel_id:
            raise ValueError("target.channel_id is required")
        return CronTarget(channel_id=channel_id, session_id=session_id or None)


@dataclass
class CronJob:
    """Cron job persisted in cron_jobs.json."""

    id: str
    name: str
    enabled: bool
    cron_expr: str
    timezone: str
    wake_offset_seconds: int = 0
    description: str = ""
    # For one-shot schedules where croniter has no "next" after the run.
    expired: bool = False
    # Target channel ID to push results to (e.g. "web").
    # JSON 字段名仍然叫 targets，用字符串保存频道 ID，兼容旧数据。
    targets: str = ""
    # SessionMap 形态（如 feishu::chat_id::bot_id::...），仅 feishu_enterprise 投递用；由 AgentServer 上下文写入。
    session_id: str | None = None
    created_at: float | None = None
    updated_at: float | None = None
    # 记录定时任务是在群聊("group")还是私聊("p2p")中创建的，用于推送时决定是否走 IMOutboundPipeline
    chat_type: str | None = None
    # 定时任务执行时使用的 Agent 模式；未指定时默认 agent（plan/fast 已合并）
    mode: str = CRON_JOB_DEFAULT_MODE
    # 执行一次后自动删除（用于提醒类任务）
    delete_after_run: bool = False
    # 单次执行超时（秒）；未配置时普通模式与 team 模式默认均为 1 小时
    timeout_seconds: int | None = None
    # 归属项目 ID；由创建时 project_dir 匹配获得，匹配不到可见项目为空串（默认项目）
    project_id: str = ""
    # 最近一次执行产生的会话 ID；调度器在创建执行会话后回写，未执行过为 None
    last_session_id: str | None = None
    # 执行时使用的模型；None 表示使用 AgentServer 默认模型
    model_name: str | None = None
    # 执行时会话级启用的 MCP 名称列表（来自创建时 chat-session 的快照，
    # 或显式传入）；None 表示不注入（沿用既有全局默认集行为）。
    mcp: list[str] | None = None
    # 飞书多应用场景：创建该定时任务的 app_id，用于推送时定位到正确的 app 配置
    app_id: str = ""
    # 创建者标识（web 端 user_id）。执行时透传给 faas 的 X-Session-Context，
    # 否则 CreateSandbox 拉不起导致 60s 超时。
    # 默认空串兼容旧数据；语义=创建者，创建后不可变。
    user_id: str = ""
    # 工作模式派生快照：由 project_id 归属推导（"code" / DEFAULT_WEB_WORK_MODE）。
    work_mode: str = DEFAULT_WEB_WORK_MODE

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "id": self.id,
            "name": self.name,
            "enabled": bool(self.enabled),
            "expired": bool(self.expired),
            "cron_expr": self.cron_expr,
            "timezone": self.timezone,
            "wake_offset_seconds": int(self.wake_offset_seconds),
            "description": self.description,
            "targets": self.targets,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
        }
        if self.session_id:
            d["session_id"] = self.session_id
        if self.chat_type:
            d["chat_type"] = self.chat_type
        if self.mode:
            d["mode"] = self.mode
        if self.delete_after_run:
            d["delete_after_run"] = bool(self.delete_after_run)
        if self.timeout_seconds is not None:
            d["timeout_seconds"] = int(self.timeout_seconds)
        # project_id 始终输出（空串表示默认项目，与 SessionInfo.project_id 语义一致）
        d["project_id"] = self.project_id or ""
        # work_mode 始终输出（派生快照字段，由 project_id 归属推导，与 project_id 一致）
        d["work_mode"] = self.work_mode or DEFAULT_WEB_WORK_MODE
        # last_session_id 仅在非空时输出（与 session_id/chat_type 可选字段策略一致）
        if self.last_session_id:
            d["last_session_id"] = self.last_session_id
        if self.model_name:
            d["model_name"] = self.model_name
        if self.mcp:
            d["mcp"] = list(self.mcp)
        if self.app_id:
            d["app_id"] = self.app_id
        if self.user_id:
            d["user_id"] = self.user_id
        return d

    # NOTE(from_dict)：原实现的 from_dict 依赖 validate_cron_expression /
    # normalize_cron_job_mode / normalize_work_mode 等实现侧函数，不进本包；
    # 反序列化留在各仓实现侧完成（两仓切换时本包仅作为数据结构 source of
    # truth）。待上游确认 from_dict 归属后再定是否在协议侧做无校验版本。


@dataclass
class CronRunState:
    """In-memory state for a single scheduled run (not persisted)."""

    run_id: str
    job_id: str
    wake_at_iso: str
    push_at_iso: str
    status: str = "pending"  # pending|running|succeeded|failed
    placeholder_sent: bool = False
    pushed_final: bool = False
    started_at: float | None = None
    finished_at: float | None = None


__all__ = [
    "CRON_JOB_DEFAULT_MODE",
    "CRON_JOB_DESCRIPTION_MAX_LENGTH",
    "CRON_JOB_NAME_MAX_LENGTH",
    "CronJob",
    "CronRunState",
    "CronTarget",
    "CronTargetChannel",
    "is_valid_target_channel_id",
    "normalize_target_channel_id",
]
