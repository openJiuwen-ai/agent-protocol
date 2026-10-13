from __future__ import annotations

from typing import Any

# agentInfo.version 由调用方注入（jiuwenswarm 侧传 common.version.__version__），
# 协议包不依赖实现侧版本定义；未注入时以包自身版本占位。
_GATEWAY_PROTOCOL_FALLBACK_VERSION = "0.0.0"


def build_acp_initialize_result(
    *,
    agent_version: str = _GATEWAY_PROTOCOL_FALLBACK_VERSION,
    agent_name: str = "jiuwenswarm",
    agent_title: str = "JiuwenSwarm",
) -> dict[str, Any]:
    return {
        "protocolVersion": 1,
        "agentInfo": {
            "name": agent_name,
            "title": agent_title,
            "version": agent_version,
        },
        "agentCapabilities": {
            "loadSession": False,
            "promptCapabilities": {
                "image": False,
                "audio": False,
                "embeddedContext": False,
            },
            "sessionCapabilities": {
                "list": {},
            },
            "mcpCapabilities": {
                "http": False,
                "sse": False,
            },
        },
        "authMethods": [],
    }


def build_acp_session_new_result(session_id: str) -> dict[str, Any]:
    return {
        "sessionId": str(session_id or "").strip(),
        "configOptions": [],
    }


def build_acp_session_list_result(session_ids: list[str]) -> dict[str, Any]:
    normalized = []
    seen: set[str] = set()
    for session_id in session_ids:
        sid = str(session_id or "").strip()
        if not sid or sid in seen:
            continue
        seen.add(sid)
        normalized.append({"sessionId": sid})
    return {"sessions": normalized}


def build_acp_prompt_result(
    *,
    stop_reason: str,
    user_message_id: str | None = None,
) -> dict[str, Any]:
    result: dict[str, Any] = {"stopReason": stop_reason}
    if isinstance(user_message_id, str) and user_message_id.strip():
        result["userMessageId"] = user_message_id.strip()
    return result


__all__ = [
    "build_acp_initialize_result",
    "build_acp_prompt_result",
    "build_acp_session_list_result",
    "build_acp_session_new_result",
]
