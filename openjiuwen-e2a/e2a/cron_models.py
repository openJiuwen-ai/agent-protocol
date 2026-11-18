# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""cron 共享模型（gateway 与 Agent Runtime 两仓共用）。

来源：``jiuwenswarm/runtime/cron/models.py``（gateway 侧 ``gateway/cron/models.py``
现为别名转发，是 gateway→jiuwenswarm 的反向依赖点，protocol 化后两侧同时解开）。

边界说明：本模块为**纯数据结构**（dataclass / enum / 常量）。原 models.py 中的
函数（``validate_cron_model``、``is_valid_target_channel_id``、
``normalize_target_channel_id`` 等）与 dataclass 的 ``to_dict`` / ``from_dict``
均属实现逻辑，**不进本包**，由各仓实现侧持有一份副本；本包只作为数据结构
source of truth。
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

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
    # 会话级模型选择快照（provider/model 等键值对）；None 表示未启用会话级选择。
    model_selection: dict[str, str] | None = None
    # 执行时会话级启用的 MCP 名称列表（来自创建时 chat-session 的快照，
    # 或显式传入）；None 表示不注入（沿用既有全局默认集行为）。
    mcp: list[str] | None = None
    # 飞书多应用场景：创建该定时任务的 app_id，用于推送时定位到正确的 app 配置
    app_id: str = ""
    # 创建者标识（web 端 user_id）。执行时透传给 faas 的 X-Session-Context，
    # 否则 CreateSandbox 拉不起导致 60s 超时。
    # 默认空串兼容旧数据；语义=创建者，创建后不可变。
    user_id: str = ""
    # 登录免费模型的凭据句柄绑定（创建连接的华为账号登录会话派生）。
    # 空串=未绑定（自配模型/未选模型）；执行侧凭据注入只认该绑定。
    credential_ref: str = ""
    # 工作模式派生快照：由 project_id 归属推导（"code" / DEFAULT_WEB_WORK_MODE）。
    work_mode: str = DEFAULT_WEB_WORK_MODE
    # 企业多租户维度：创建该任务的 group / bot；个人版为空串（与
    # ``jiuwenswarm/runtime/cron/models.py`` 同名字段一致）。
    group_id: str = ""
    bot_id: str = ""
    # 调度器写入的下次触发时间（epoch 秒）；用于列表展示与「即将执行」排序。
    next_run_at: float | None = None

    # NOTE(to_dict / from_dict)：序列化与反序列化属实现逻辑，不在本包；
    # 各仓实现侧持有（from_dict 依赖 validate_cron_expression /
    # normalize_cron_job_mode / normalize_work_mode 等本仓函数）。本包仅作为
    # 数据结构 source of truth。


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
    result_text: str | None = None
    error: str | None = None
    job_name: str | None = None
    targets: str | None = None
    session_id: str | None = None
    chat_type: str | None = None
    timezone: str | None = None
    # Canonical mode captured before execution. It remains available after a
    # job is removed from the store, so ghost/timeout cancellation reaches the
    # same Runtime agent that owns the in-flight request.
    exec_mode: str | None = None
    exec_channel_id: str | None = None
    exec_session_id: str | None = None
    # Tenant route captured with the executing request.  A removed persisted
    # job must not make ghost/timeout cancellation fall back to the default user.
    exec_user_id: str | None = None
    exec_app_id: str | None = None
    exec_group_id: str | None = None
    exec_bot_id: str | None = None
    # Remaining AgentManager cache identity captured by the executing request.
    exec_work_mode: str | None = None
    exec_project_id: str | None = None
    exec_project_dir: str | None = None
    # ``run_now`` allocates the single-agent session before the wake event so
    # Web can open the real session immediately; wake must reuse it.
    execution_session_allocated: bool = False
    # Manual proactive checks report their outcome through a UI notification.
    manually_triggered: bool = False
    # 执行流已经把终稿放进会话直播。后面的推送不再往同一条直播放第二遍。
    live_final_forwarded: bool = False
    # 直播帧序号。浏览器若短暂连了两次，用它丢掉来晚的旧帧。
    live_seq: int = 0
    # 执行请求 / 执行实例标识：区分同一任务的多次尝试，供直播与通知去重。
    exec_request_id: str | None = None
    exec_execution_id: str | None = None
    # 通知版本号与执行流错误标记：Web 侧据此决定是否重放终态。
    notification_revision: int = 0
    stream_error_seen: bool = False


__all__ = [
    "CRON_JOB_DEFAULT_MODE",
    "CRON_JOB_DESCRIPTION_MAX_LENGTH",
    "CRON_JOB_NAME_MAX_LENGTH",
    "DEFAULT_WEB_WORK_MODE",
    "CronJob",
    "CronRunState",
    "CronTarget",
    "CronTargetChannel",
]