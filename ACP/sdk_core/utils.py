# coding: utf-8
"""Utility helpers for ACP SDK."""
from __future__ import annotations

import json
import re
from typing import Any, Dict, List


def extract_json_object(raw: str) -> Dict[str, Any]:
    """Extract a JSON object from LLM output."""
    if raw is None:
        return {}

    text = str(raw).strip()
    if not text:
        return {}

    try:
        data = json.loads(text)
        if isinstance(data, dict):
            return data
    except json.JSONDecodeError:
        pass

    fenced = re.search(r"```json\s*(\{.*\})\s*```", text, flags=re.DOTALL | re.IGNORECASE)
    if fenced:
        try:
            data = json.loads(fenced.group(1))
            if isinstance(data, dict):
                return data
        except json.JSONDecodeError:
            pass

    start = text.find("{")
    end = text.rfind("}")
    if start != -1 and end != -1 and start < end:
        try:
            data = json.loads(text[start : end + 1])
            if isinstance(data, dict):
                return data
        except json.JSONDecodeError:
            return {}

    return {}


def ensure_list(value: Any) -> list:
    if isinstance(value, list):
        return value
    return []


def normalize_completed_agent_context(
    template: Dict[str, Any],
    generated: Dict[str, Any],
    raw_output: str = "",
) -> Dict[str, Any]:
    todo_items: List[Dict[str, Any]] = []
    for item in ensure_list(generated.get("todoItems")):
        if isinstance(item, dict) and item.get("itemId"):
            todo_items.append(
                {
                    "itemId": str(item.get("itemId")),
                    "description": str(item.get("description", "")),
                }
            )
    if not todo_items:
        todo_items = ensure_list(template.get("todoItems"))

    update_map = {}
    for item in ensure_list(generated.get("ItemstateUpdates")):
        if isinstance(item, dict) and item.get("itemId"):
            update_map[str(item.get("itemId"))] = item

    key_info_map = {}
    for item in ensure_list(generated.get("KeyInformation")):
        if isinstance(item, dict) and item.get("itemId"):
            key_info_map[str(item.get("itemId"))] = item

    item_updates: List[Dict[str, Any]] = []
    key_information: List[Dict[str, Any]] = []
    for todo_item in todo_items:
        item_id = str(todo_item.get("itemId", ""))
        update = update_map.get(item_id, {})
        info = key_info_map.get(item_id, {})
        try:
            state = int(update.get("state", 0))
        except (TypeError, ValueError):
            state = 0
        item_updates.append({"itemId": item_id, "state": 1 if state else 0})
        key_information.append(
            {
                "itemId": item_id,
                "outputabstract": str(info.get("outputabstract", "")),
            }
        )

    return {
        "AgentID": str(generated.get("AgentID") or template.get("AgentID", "")),
        "AgentName": str(generated.get("AgentName") or template.get("AgentName", "")),
        "SubTaskID": str(generated.get("SubTaskID") or template.get("SubTaskID", "")),
        "SubTaskName": str(generated.get("SubTaskName") or template.get("SubTaskName", "")),
        "Dependencies": generated.get("Dependencies")
        if isinstance(generated.get("Dependencies"), list)
        else template.get("Dependencies", []),
        "DependencyOutputs": generated.get("DependencyOutputs")
        if isinstance(generated.get("DependencyOutputs"), dict)
        else template.get("DependencyOutputs", {}),
        "todoItems": todo_items,
        "ItemstateUpdates": item_updates,
        "KeyInformation": key_information,
        "Feedback": str(generated.get("Feedback") or template.get("Feedback", "")),
        "full_output": str(generated.get("full_output") or raw_output or ""),
    }


def resolve_current_goal(task_context: Dict[str, Any]) -> Dict[str, Any]:
    task_ctx = task_context.get("TaskContext", {}) if isinstance(task_context, dict) else {}
    goals = ensure_list(task_ctx.get("Goals"))
    status_list = ensure_list(task_ctx.get("GoalStatus"))
    status_map = {}
    for item in status_list:
        if isinstance(item, dict) and item.get("Goal"):
            status_map[str(item.get("Goal"))] = str(item.get("Status", "Not Started"))

    for goal in goals:
        if not isinstance(goal, dict):
            continue
        goal_id = str(goal.get("goal_id") or goal.get("Goal") or goal.get("id") or "")
        if not goal_id:
            continue
        status = status_map.get(goal_id, "Not Started")
        if status != "Finished":
            return goal
    return {}


def update_goal_status(
    task_context: Dict[str, Any],
    goal_id: str,
    status: str,
) -> Dict[str, Any]:
    goal_status = ensure_list(task_context.get("TaskContext", {}).get("GoalStatus"))
    for item in goal_status:
        if isinstance(item, dict) and item.get("Goal") == goal_id:
            item["Status"] = status
            break
    return task_context


def update_task_context_from_agent_context(
    task_context: Dict[str, Any],
    agent_context: Dict[str, Any] | str,
) -> Dict[str, Any]:
    context = parse_agent_context_json(agent_context) if isinstance(agent_context, str) else dict(agent_context or {})
    goal_id = str(context.get("SubTaskID") or context.get("AgentID") or "")
    item_updates = ensure_list(context.get("ItemstateUpdates"))
    if not goal_id:
        return task_context

    status = "In Progress"
    if item_updates and all(isinstance(item, dict) and int(item.get("state", 0)) == 1 for item in item_updates):
        status = "Finished"
    return update_goal_status(task_context, goal_id, status)


def parse_agent_context_json(agent_context_json: str | Dict[str, Any]) -> Dict[str, Any]:
    if isinstance(agent_context_json, dict):
        return agent_context_json
    parsed = extract_json_object(agent_context_json)
    return parsed if isinstance(parsed, dict) else {}


def dump_agent_context_json(agent_context: Dict[str, Any]) -> str:
    return json.dumps(agent_context or {}, ensure_ascii=False)


def extract_agent_context_json_from_workflow_result(result: Any) -> str:
    if not isinstance(result, dict):
        return ""

    workflow_output = result.get("output")
    if workflow_output is None or not hasattr(workflow_output, "result"):
        return ""

    payload = workflow_output.result
    if not isinstance(payload, dict):
        return ""

    raw_agent_context = payload.get("agent_context")
    if isinstance(raw_agent_context, str):
        return raw_agent_context

    output_payload = payload.get("output")
    if isinstance(output_payload, dict) and isinstance(output_payload.get("agent_context"), str):
        return str(output_payload["agent_context"])

    if isinstance(raw_agent_context, dict):
        return dump_agent_context_json(raw_agent_context)

    if isinstance(output_payload, dict) and isinstance(output_payload.get("agent_context"), dict):
        return dump_agent_context_json(output_payload["agent_context"])

    return ""
