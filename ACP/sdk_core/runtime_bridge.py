# coding: utf-8
"""Runtime bridge helpers for integrating ACP SDK with agent-core."""
from __future__ import annotations

import sys
from types import ModuleType

from .builders import AgentContextBuilder, TaskContextBuilder, extract_agent_context_from_workflow_result
from .client import ACPClient
from .evaluator import ACPEvaluator
from .models import (
    ACPState,
    AgentContext,
    EvaluationResult,
    GoalStatus,
    ItemStateUpdate,
    KeyInformation,
    TaskContext,
    TodoItem,
)
from .utils import (
    dump_agent_context_json,
    extract_agent_context_json_from_workflow_result,
    normalize_completed_agent_context,
    parse_agent_context_json,
    resolve_current_goal,
    update_goal_status,
    update_task_context_from_agent_context,
)


def install_agent_core_acp_sdk_bridge() -> None:
    """Expose the standalone SDK under the import path expected by agent-core."""
    module_name = "openjiuwen.core.multi_agent.acp_sdk"
    if module_name in sys.modules:
        return

    bridge = ModuleType(module_name)
    exports = {
        "ACPClient": ACPClient,
        "ACPEvaluator": ACPEvaluator,
        "AgentContext": AgentContext,
        "AgentContextBuilder": AgentContextBuilder,
        "ACPState": ACPState,
        "EvaluationResult": EvaluationResult,
        "GoalStatus": GoalStatus,
        "ItemStateUpdate": ItemStateUpdate,
        "KeyInformation": KeyInformation,
        "TaskContext": TaskContext,
        "TaskContextBuilder": TaskContextBuilder,
        "TodoItem": TodoItem,
        "dump_agent_context_json": dump_agent_context_json,
        "extract_agent_context_from_workflow_result": extract_agent_context_from_workflow_result,
        "extract_agent_context_json_from_workflow_result": extract_agent_context_json_from_workflow_result,
        "normalize_completed_agent_context": normalize_completed_agent_context,
        "parse_agent_context_json": parse_agent_context_json,
        "resolve_current_goal": resolve_current_goal,
        "update_goal_status": update_goal_status,
        "update_task_context_from_agent_context": update_task_context_from_agent_context,
    }
    for name, value in exports.items():
        setattr(bridge, name, value)
    bridge.__all__ = list(exports.keys())
    sys.modules[module_name] = bridge
