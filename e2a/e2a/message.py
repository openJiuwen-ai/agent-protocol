# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""统一消息模型（WS / E2A 线上报文与 mode 枚举）。

来源：``jiuwenswarm/common/schema/message.py``。线上结构（``Message`` / ``Mode``）
两仓共享，故迁入本包。方法名（``ReqMethod``）与事件名（``EventType``）都属于
实现侧词汇表，定义在 jiuwenswarm ``common/schema/message.py``；本包只把它们
当作线上字符串承载，Gateway 也按字符串判等、不依赖枚举身份。

``Mode.from_raw`` 需要旧的 canonical 模式归一化（``deprecate_mode``），本模块
按"复制优先"内联了一份纯数据/纯函数副本（``_MODE_ALIASES`` /
``_DEPRECATION_MAP``）——jiuwenswarm 侧 ``common/mode_matrix.py`` 仍持有实现侧
副本，两处取值需保持一致（改一处需同步另一处）。
"""

import logging
from dataclasses import dataclass
from enum import Enum
from typing import Any, Literal

logger = logging.getLogger(__name__)

# ``chat.send`` 请求携带该 truthy 参数时，仅为已在运行的 Team Session 附加
# 响应订阅，不提交新输入（gateway / runtime / server 三方共用的协议参数名）。
TEAM_ATTACH_ONLY_PARAM = "attach_only"

# 不属于实现侧事件枚举的历史线协议事件名：由 AgentServer 产出、Gateway / Web
# 消费（两仓字面量必须一致，故收敛到协议包）。
SESSION_REQUEST_COMPLETED_EVENT = "chat.request_completed"
PROMPT_SUGGESTION_READY_EVENT = "prompt_suggestion.ready"
TOPIC_STARTERS_READY_EVENT = "topic_starters.ready"
TOPIC_STARTERS_CONTROL_CHANGED_EVENT = "topic_starters.control_changed"

# ── mode 归一化：与 jiuwenswarm ``common/mode_matrix.py`` 同源的纯数据副本 ──
_MODE_ALIASES: dict[str, str] = {
    "team.plan": "team.plan.normal",
    "team.code": "code.team",
    "team.work": "team.work.normal",
    "team.normal": "team.work.normal",
    "agent.work": "agent.work.normal",
    "agent.code": "agent.code.normal",
}

_DEPRECATION_MAP: dict[str, str] = {
    "agent": "agent.work.normal",
    "agent.plan": "agent.work.plan",
    "agent.fast": "agent.work.normal",
    "code": "agent.code.normal",
    "code.normal": "agent.code.normal",
    "code.plan": "agent.code.plan",
    "code.team": "team.code.normal",
    "team": "team.work.normal",
    "team.plan.normal": "team.work.plan",
    "team.plan.code": "team.code.plan",
}


def _normalize_mode_text(raw_mode: Any) -> str:
    """归一成小写字符串，空值回落到 ``agent``。"""
    raw_value = getattr(raw_mode, "value", raw_mode)
    text = raw_value.strip().lower() if isinstance(raw_value, str) else ""
    return text or "agent"


def _canonicalize_mode_text(raw_mode: Any) -> str:
    text = _normalize_mode_text(raw_mode)
    return _MODE_ALIASES.get(text, text)


def _deprecate_mode(mode: Any) -> str | None:
    """旧 canonical 静默映射到新 canonical；None / 空串等非旧值原样返回。"""
    raw_value = getattr(mode, "value", mode)
    if raw_value is None or (isinstance(raw_value, str) and not raw_value.strip()):
        return raw_value
    text = _canonicalize_mode_text(mode)
    new_text = _DEPRECATION_MAP.get(text, text)
    if new_text != text:
        logger.debug(
            "deprecate_mode: legacy canonical '%s' -> new canonical '%s'",
            text, new_text,
        )
    return new_text


class Mode(Enum):
    AGENT = "agent"
    # 历史值：plan / fast 已合并为 agent，保留以兼容旧序列化数据的反解析。
    AGENT_PLAN = "agent.plan"
    AGENT_FAST = "agent.fast"
    CODE_PLAN = "code.plan"
    CODE_NORMAL = "code.normal"
    CODE_TEAM = "code.team"
    TEAM = "team"
    TEAM_PLAN_NORMAL = "team.plan.normal"
    TEAM_PLAN_CODE = "team.plan.code"
    # 新三段命名 canonical（P2 引入；旧成员保留以兼容历史持久化反解析）。
    AGENT_WORK_NORMAL = "agent.work.normal"
    AGENT_WORK_PLAN = "agent.work.plan"
    AGENT_CODE_NORMAL = "agent.code.normal"
    AGENT_CODE_PLAN = "agent.code.plan"
    TEAM_WORK_NORMAL = "team.work.normal"
    TEAM_WORK_PLAN = "team.work.plan"
    TEAM_CODE_NORMAL = "team.code.normal"
    TEAM_CODE_PLAN = "team.code.plan"

    @classmethod
    def from_raw(cls, raw_mode: Any, default: "Mode | None" = None) -> "Mode":
        """解析 mode：新 canonical 原样返回，旧 canonical 静默映射到新 canonical。

        单一逻辑路径——不再维护手写白名单：``_deprecate_mode`` 对新 canonical
        原样返回（不在 :data:`DEPRECATION_MAP` 里），对旧 canonical 映射到新串，
        对未知串原样返回后由 ``cls(...)`` 抛 ``ValueError`` 兜底到 ``fallback``。
        裸 ``plan`` / ``fast`` 与 CLI ``MODE_ALIASES`` 同语义，直接落到
        :attr:`AGENT_WORK_NORMAL`，不绕 ``agent.fast`` 中间步。
        """
        fallback = default or cls.AGENT_WORK_NORMAL
        if isinstance(raw_mode, Mode):
            raw_mode = raw_mode.value
        if not isinstance(raw_mode, str):
            return fallback
        normalized = raw_mode.strip().lower()
        if not normalized:
            return fallback
        if normalized in ("plan", "fast"):
            logger.debug("Mode.from_raw: bare '%s' -> AGENT_WORK_NORMAL", normalized)
            return cls.AGENT_WORK_NORMAL
        new_mode_str = _deprecate_mode(normalized)
        try:
            mode = cls(new_mode_str)
        except ValueError:
            logger.warning(
                "Mode.from_raw: 无法识别的 mode=%r (deprecate 后=%r)，兜底 %s",
                raw_mode, new_mode_str, fallback.value,
            )
            return fallback
        if new_mode_str != normalized:
            logger.debug(
                "Mode.from_raw: '%s' -> '%s' (legacy canonical 静默迁移)",
                normalized, new_mode_str,
            )
        return mode

    def to_runtime_mode(self) -> str:
        """输出 runtime mode 值；历史 agent.plan / agent.fast 归一为 agent。

        新枚举原样返回（canonical 串即 runtime 串）。旧枚举按历史语义归一为
        合并后的 ``agent`` / 自身 canonical 串。
        """
        if self in (Mode.AGENT_PLAN, Mode.AGENT_FAST):
            return Mode.AGENT.value
        return self.value


@dataclass
class Message:
    """统一消息结构."""
    id: str
    type: Literal["req", "res", "event"]
    channel_id: str
    session_id: str | None
    params: dict
    timestamp: float
    ok: bool
    provider: str | None = None
    chat_id: str | None = None
    user_id: str | None = None
    bot_id: str | None = None  # 已弃用，请使用 app_id + agent_ref 替代
    app_id: str | None = None  # 应用实例标识，从 bot_id 拆出
    agent_ref: Any = None      # AgentRef(mode, id)，后端智能体标识
    payload: dict | None = None
    # Extension methods are kept as strings until AgentServer decides whether
    # they are implemented.  Known methods may use the implementation-side
    # method enum; this module only carries the value.
    # String annotation: the method enum lives on the implementation side,
    # so this module must not resolve it at class-creation time.
    req_method: "ReqMethod | str | None" = None
    # Event names are wire strings.  The implementation side may store its own
    # ``StrEnum`` member here; it compares equal to the wire string, so both the
    # Gateway (plain strings) and AgentServer (enum) read the same value.
    # String annotation for the same reason as ``req_method`` above.
    event_type: "EventType | str | None" = None
    # 与 Mode.from_raw 的 fallback 对齐：客户端不传 mode 时落到 canonical
    # ``agent.work.normal``，避免 schema 默认值与 from_raw 不一致导致
    # Message.mode 在两条入口路径下产生不同默认值。旧 ``Mode.AGENT`` 仍由
    # from_raw("agent") 经 DEPRECATION_MAP 映射到 AGENT_WORK_NORMAL，故不会
    # 丢失旧 canonical 的反解析能力。
    mode: Mode = Mode.AGENT_WORK_NORMAL
    is_stream: bool = False
    stream_seq: int | None = None
    stream_id: str | None = None
    metadata: dict[str, Any] | None = None
    group_digital_avatar: bool = False
    enable_memory: bool | None = None
    enable_streaming: bool = True
