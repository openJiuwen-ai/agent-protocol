from __future__ import annotations

import uuid
from typing import Any, Protocol

from gateway_protocol.e2a.acp.acp_tool_updates import (
    build_acp_todo_update,
    build_acp_tool_call_update,
    build_acp_tool_result_update,
    is_reasoning_event,
)

# ---------------------------------------------------------------------------
# 事件名契约（与 jiuwenswarm common.schema.message.EventType 的 value 一致）。
# ``build_acp_session_update`` 按 ``msg.event_type`` 的**字符串值**分发；
# 调用方传入枚举（任意 Enum，取 .value）或裸字符串均可。
# ---------------------------------------------------------------------------

EVENT_CHAT_SYMPHONY_STATUS = "chat.symphony_status"
EVENT_CHAT_DELTA = "chat.delta"
EVENT_CHAT_REASONING = "chat.reasoning"
EVENT_CHAT_TOOL_CALL = "chat.tool_call"
EVENT_CHAT_TOOL_UPDATE = "chat.tool_update"
EVENT_CHAT_TOOL_RESULT = "chat.tool_result"
EVENT_CHAT_SUBTASK_UPDATE = "chat.subtask_update"
EVENT_TODO_UPDATED = "todo.updated"
EVENT_CHAT_PROCESSING_STATUS = "chat.processing_status"


def _event_name(event_type: Any) -> str:
    return str(getattr(event_type, "value", event_type) or "")


class AcpSessionUpdateState(Protocol):
    assistant_message_id: str | None
    assistant_text: str | None
    thought_message_id: str | None
    thought_text: str | None
    user_message_id: str | None
    tool_call_cache: dict[str, dict[str, Any]] | None


def _ensure_tool_call_cache(state: AcpSessionUpdateState) -> dict[str, dict[str, Any]]:
    cache = getattr(state, "tool_call_cache", None)
    if not isinstance(cache, dict):
        cache = {}
        state.tool_call_cache = cache
    return cache


def _ensure_assistant_message_id(state: AcpSessionUpdateState) -> str:
    if not state.assistant_message_id:
        state.assistant_message_id = str(uuid.uuid4())
    return str(state.assistant_message_id)


def _reset_assistant_message_id(state: AcpSessionUpdateState) -> str:
    state.assistant_message_id = str(uuid.uuid4())
    return str(state.assistant_message_id)


def _ensure_thought_message_id(state: AcpSessionUpdateState) -> str:
    if not state.thought_message_id:
        state.thought_message_id = str(uuid.uuid4())
    return str(state.thought_message_id)


def _reset_thought_message_id(state: AcpSessionUpdateState) -> str:
    state.thought_message_id = str(uuid.uuid4())
    return str(state.thought_message_id)


def _append_state_text(state: AcpSessionUpdateState, attr: str, text: str) -> None:
    current = getattr(state, attr, None)
    setattr(state, attr, f"{str(current or '')}{text}")


def _build_incremental_text_update(
    *,
    text: str,
    state: AcpSessionUpdateState,
    text_attr: str,
    ensure_message_id,
    reset_message_id,
    update_kind: str,
) -> dict[str, Any] | None:
    if not text:
        return None

    existing_text = str(getattr(state, text_attr, None) or "")
    if existing_text:
        if text == existing_text:
            return None
        if text.startswith(existing_text):
            delta_text = text[len(existing_text):]
            if not delta_text:
                return None
            _append_state_text(state, text_attr, delta_text)
            return {
                "sessionUpdate": update_kind,
                "messageId": ensure_message_id(state),
                "content": {"type": "text", "text": delta_text},
            }
        setattr(state, text_attr, text)
        return {
            "sessionUpdate": update_kind,
            "messageId": reset_message_id(state),
            "content": {"type": "text", "text": text},
        }
    _append_state_text(state, text_attr, text)
    return {
        "sessionUpdate": update_kind,
        "messageId": ensure_message_id(state),
        "content": {"type": "text", "text": text},
    }


def build_acp_session_update(
    msg: Any,
    payload: dict[str, Any],
    state: AcpSessionUpdateState,
) -> dict[str, Any] | None:
    """按消息事件构造 ACP session/update 载荷.

    ``msg`` 为鸭子类型：仅需 ``event_type`` 属性（枚举或字符串，枚举取
    ``.value`` 归一），与 jiuwenswarm ``common.schema.message.Message`` 兼容。
    """
    event_name = _event_name(getattr(msg, "event_type", None))
    if event_name == EVENT_CHAT_SYMPHONY_STATUS:
        return None

    if event_name in (EVENT_CHAT_DELTA, EVENT_CHAT_REASONING):
        text = str(payload.get("content", "") or "")
        if not text:
            return None
        if is_reasoning_event(
            event_name,
            payload,
        ):
            _append_state_text(state, "thought_text", text)
            return {
                "sessionUpdate": "agent_thought_chunk",
                "messageId": _ensure_thought_message_id(state),
                "content": {"type": "text", "text": text},
            }

        _append_state_text(state, "assistant_text", text)
        return {
            "sessionUpdate": "agent_message_chunk",
            "messageId": _ensure_assistant_message_id(state),
            "content": {"type": "text", "text": text},
        }

    if event_name == EVENT_CHAT_TOOL_CALL:
        return build_acp_tool_call_update(payload, cache=_ensure_tool_call_cache(state))

    if event_name in (EVENT_CHAT_TOOL_UPDATE, EVENT_CHAT_TOOL_RESULT):
        return build_acp_tool_result_update(payload, cache=_ensure_tool_call_cache(state))

    if event_name == EVENT_CHAT_SUBTASK_UPDATE:
        return {
            "sessionUpdate": "plan",
            "plan": dict(payload),
        }

    if event_name == EVENT_TODO_UPDATED:
        return build_acp_todo_update(payload)

    if event_name == EVENT_CHAT_PROCESSING_STATUS:
        return {
            "sessionUpdate": "session_info_update",
            "status": "processing" if bool(payload.get("is_processing", True)) else "idle",
        }

    return None


def build_acp_final_text_update(
    payload: dict[str, Any],
    state: AcpSessionUpdateState,
) -> dict[str, Any] | None:
    text = str(payload.get("content", "") or "")
    if not text:
        return None

    if is_reasoning_event("chat.final", payload):
        return _build_incremental_text_update(
            text=text,
            state=state,
            text_attr="thought_text",
            ensure_message_id=_ensure_thought_message_id,
            reset_message_id=_reset_thought_message_id,
            update_kind="agent_thought_chunk",
        )

    return _build_incremental_text_update(
        text=text,
        state=state,
        text_attr="assistant_text",
        ensure_message_id=_ensure_assistant_message_id,
        reset_message_id=_reset_assistant_message_id,
        update_kind="agent_message_chunk",
    )


def build_acp_usage_update(payload: dict[str, Any]) -> dict[str, Any] | None:
    usage = payload.get("usage")
    if not isinstance(usage, dict) or not usage:
        return None
    return {
        "sessionUpdate": "usage_update",
        "usage": dict(usage),
    }


__all__ = [
    "AcpSessionUpdateState",
    "EVENT_CHAT_DELTA",
    "EVENT_CHAT_PROCESSING_STATUS",
    "EVENT_CHAT_REASONING",
    "EVENT_CHAT_SUBTASK_UPDATE",
    "EVENT_CHAT_SYMPHONY_STATUS",
    "EVENT_CHAT_TOOL_CALL",
    "EVENT_CHAT_TOOL_RESULT",
    "EVENT_CHAT_TOOL_UPDATE",
    "EVENT_TODO_UPDATED",
    "build_acp_final_text_update",
    "build_acp_session_update",
    "build_acp_usage_update",
]
