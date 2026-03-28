# coding: utf-8
"""TaskContext and AgentContext builders."""
from __future__ import annotations

import json
from typing import Any, Callable, Dict, List, Optional

from .client import ACPClient
from .models import (
    TaskContext,
    GoalStatus,
    AgentContext,
    TodoItem,
    ItemStateUpdate,
    KeyInformation,
)
from .utils import (
    dump_agent_context_json,
    extract_json_object,
    ensure_list,
    normalize_completed_agent_context,
    extract_agent_context_json_from_workflow_result,
    resolve_current_goal,
)


DEFAULT_GOAL_STATUS = "Not Started"


def _normalize_goal_list(goals: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    normalized = []
    for goal in goals or []:
        goal_id = goal.get("goal_id") or goal.get("Goal") or goal.get("id")
        description = goal.get("task_description") or goal.get("description") or goal.get("name")
        agent = goal.get("agent") or goal.get("agent_id") or goal.get("agent_name")
        dependencies = goal.get("dependencies") if isinstance(goal.get("dependencies"), list) else []
        normalized.append(
            {
                "goal_id": str(goal_id) if goal_id is not None else "",
                "task_description": str(description) if description is not None else "",
                "agent": str(agent) if agent is not None else "",
                "dependencies": [str(dep) for dep in dependencies],
            }
        )
    return normalized


def extract_agent_context_from_workflow_result(result: Any) -> str:
    return extract_agent_context_json_from_workflow_result(result)


class TaskContextBuilder:
    def __init__(
        self,
        acp_client: Optional[ACPClient] = None,
        model: Optional[str] = None,
        temperature: float = 0.0,
    ) -> None:
        self._client = acp_client
        self._model = model
        self._temperature = temperature

    def build(
        self,
        *,
        task_id: str,
        task_name: str,
        task_description: str,
        goals: List[Dict[str, Any]],
        use_llm: bool = True,
        stream_handler: Optional[Callable[[str], None]] = None,
    ) -> Dict[str, Any]:
        normalized_goals = _normalize_goal_list(goals)
        fallback = TaskContext(
            TaskID=task_id,
            TaskName=task_name,
            TaskDescription=task_description,
            Goals=normalized_goals,
            GoalStatus=[GoalStatus(Goal=g["goal_id"], Status=DEFAULT_GOAL_STATUS) for g in normalized_goals],
        )

        if not use_llm or self._client is None:
            return fallback.to_payload()

        prompt = f"""
You are the task-context builder for the main agent. Generate TaskContext from the input.

Task ID: {task_id}
Task Name: {task_name}
Task Description: {task_description}
Goals:
{json.dumps(normalized_goals, ensure_ascii=False, separators=(",", ":"))}

Rules:
1. Output strict JSON only.
2. Preserve the Goals list with goal_id, task_description, agent, and dependencies.
3. Every Goal in GoalStatus must come from Goals.goal_id.
4. Status must be one of: Not Started / In Progress / Finished.

Output format:
{{
  "TaskContext": {{
    "TaskID": "{task_id}",
    "TaskName": "{task_name}",
    "TaskDescription": "{task_description}",
    "Goals": {json.dumps(normalized_goals, ensure_ascii=False)},
    "GoalStatus": [{{"Goal": "G1", "Status": "Not Started"}}]
  }}
}}
"""
        if stream_handler is not None:
            parts: List[str] = []
            for chunk in self._client.stream_complete(prompt, model=self._model, temperature=self._temperature):
                parts.append(chunk)
                stream_handler(chunk)
            raw = "".join(parts).strip()
        else:
            raw = self._client.complete(prompt, model=self._model, temperature=self._temperature)
        parsed = extract_json_object(raw)
        task_ctx = parsed.get("TaskContext") if isinstance(parsed, dict) else None

        if not isinstance(task_ctx, dict):
            return fallback.to_payload()

        goal_status = ensure_list(task_ctx.get("GoalStatus"))
        normalized_status = []
        allowed = {"Not Started", "In Progress", "Finished"}
        for item in goal_status:
            if not isinstance(item, dict):
                continue
            goal = str(item.get("Goal", ""))
            status = str(item.get("Status", DEFAULT_GOAL_STATUS))
            if status not in allowed:
                status = DEFAULT_GOAL_STATUS
            normalized_status.append(GoalStatus(Goal=goal, Status=status))

        if not normalized_status:
            normalized_status = fallback.GoalStatus

        goals_payload = _normalize_goal_list(ensure_list(task_ctx.get("Goals")))
        if not goals_payload:
            goals_payload = normalized_goals

        normalized = TaskContext(
            TaskID=str(task_ctx.get("TaskID") or task_id),
            TaskName=str(task_ctx.get("TaskName") or task_name),
            TaskDescription=str(task_ctx.get("TaskDescription") or task_description),
            Goals=goals_payload,
            GoalStatus=normalized_status,
        )
        return normalized.to_payload()


class AgentContextBuilder:
    def __init__(
        self,
        acp_client: Optional[ACPClient] = None,
        model: Optional[str] = None,
        temperature: float = 0.0,
        companies: Optional[List[str]] = None,
    ) -> None:
        self._client = acp_client
        self._model = model
        self._temperature = temperature
        self._companies = companies or []

    def build(
        self,
        *,
        task_context: str | Dict[str, Any],
        dependency_outputs: Optional[str | Dict[str, str]] = None,
        feedback: str = "",
        use_llm: bool = True,
        stream_handler: Optional[Callable[[str], None]] = None,
    ) -> str:
        task_context_payload = extract_json_object(task_context) if isinstance(task_context, str) else dict(task_context or {})
        dependency_outputs_payload = (
            extract_json_object(dependency_outputs) if isinstance(dependency_outputs, str) else dict(dependency_outputs or {})
        )

        goal = resolve_current_goal(task_context_payload)
        goal_id = str(goal.get("goal_id") or goal.get("Goal") or goal.get("id") or "")
        agent_name = str(goal.get("agent") or goal.get("agent_name") or "")
        task_desc = str(goal.get("task_description") or goal.get("description") or "")
        dependencies = [str(dep) for dep in ensure_list(goal.get("dependencies"))]
        dep_outputs = {str(k): str(v) for k, v in dependency_outputs_payload.items()}

        fallback = self._fallback_context(goal_id, agent_name, task_desc, dependencies, dep_outputs, feedback)

        if not use_llm or self._client is None:
            return dump_agent_context_json(fallback.to_dict())

        prompt = f"""
You are the controller-side task decomposition module. Build an AgentContext request for the next worker agent.

Latest TaskContext:
{json.dumps(task_context_payload, ensure_ascii=False, separators=(",", ":"))}

Dependency full outputs:
{json.dumps(dep_outputs, ensure_ascii=False, indent=2)}

Evaluator feedback for retry, if any:
{feedback or "None"}

Companies:
{json.dumps(self._companies, ensure_ascii=False)}

Rules:
1. Output strict JSON only.
2. Infer the current subtask from the latest TaskContext. Do not require any extra current-goal input.
3. Build a request AgentContext for the next unfinished or in-progress worker goal.
4. todoItems must be actionable and aligned with the inferred current goal.
5. ItemstateUpdates must initialize to 0.
6. KeyInformation.outputabstract must initialize to empty string.
7. Keep DependencyOutputs and Feedback in the output so the worker can use them.
8. full_output should remain empty in the request stage.

Output format:
{{
  "AgentID": "{goal_id}",
  "AgentName": "{agent_name}",
  "SubTaskID": "{goal_id}",
  "SubTaskName": "{task_desc}",
  "Dependencies": {json.dumps(dependencies, ensure_ascii=False)},
  "DependencyOutputs": {json.dumps(dep_outputs, ensure_ascii=False)},
  "todoItems": [{{"itemId": "...", "description": "..."}}],
  "ItemstateUpdates": [{{"itemId": "...", "state": 0}}],
  "KeyInformation": [{{"itemId": "...", "outputabstract": ""}}],
  "Feedback": "{feedback}",
  "full_output": ""
}}
"""
        if stream_handler is not None:
            parts: List[str] = []
            for chunk in self._client.stream_complete(prompt, model=self._model, temperature=self._temperature):
                parts.append(chunk)
                stream_handler(chunk)
            raw = "".join(parts).strip()
        else:
            raw = self._client.complete(prompt, model=self._model, temperature=self._temperature)
        parsed = extract_json_object(raw)
        if not isinstance(parsed, dict):
            return dump_agent_context_json(fallback.to_dict())

        normalized = self._normalize_context(parsed, fallback)
        return dump_agent_context_json(normalized.to_dict())

    def build_completed(
        self,
        *,
        request_agent_context: str | Dict[str, Any],
        role_name: str,
        capability: str,
        use_llm: bool = True,
        stream_handler: Optional[Callable[[str], None]] = None,
    ) -> str:
        request_payload = (
            extract_json_object(request_agent_context)
            if isinstance(request_agent_context, str)
            else dict(request_agent_context or {})
        )
        dependencies = ensure_list(request_payload.get("Dependencies"))
        dependency_outputs = request_payload.get("DependencyOutputs", {})
        feedback = str(request_payload.get("Feedback", "") or "")

        if not use_llm or self._client is None:
            return dump_agent_context_json(request_payload)

        prompt = f"""
You are {role_name}.
Capability: {capability}

Input request AgentContext:
{json.dumps(request_payload, ensure_ascii=False, indent=2)}

Dependency full outputs:
{json.dumps(dependency_outputs, ensure_ascii=False, indent=2)}

Retry feedback:
{feedback or "None"}

Rules:
1. Output strict JSON only.
2. Update the request AgentContext into a completed AgentContext.
3. Keep todoItems aligned one-to-one with ItemstateUpdates and KeyInformation.
4. Completed items use state=1, unfinished items use state=0.
5. KeyInformation must contain concise item-level summaries.
6. full_output must be the complete final response from this worker.
7. Use DependencyOutputs as the direct dependency input.
8. If data is uncertain, state uncertainty explicitly instead of fabricating.

Output format:
{{
  "AgentID": "{request_payload.get("AgentID", "")}",
  "AgentName": "{request_payload.get("AgentName", "")}",
  "SubTaskID": "{request_payload.get("SubTaskID", "")}",
  "SubTaskName": "{request_payload.get("SubTaskName", "")}",
  "Dependencies": {json.dumps(dependencies, ensure_ascii=False)},
  "DependencyOutputs": {json.dumps(dependency_outputs, ensure_ascii=False)},
  "todoItems": [{{"itemId": "...", "description": "..."}}],
  "ItemstateUpdates": [{{"itemId": "...", "state": 1}}],
  "KeyInformation": [{{"itemId": "...", "outputabstract": "..."}}],
  "Feedback": "{feedback}",
  "full_output": "..."
}}
"""
        parts: List[str] = []
        if stream_handler is not None:
            for chunk in self._client.stream_complete(prompt, model=self._model, temperature=self._temperature):
                parts.append(chunk)
                stream_handler(chunk)
            raw = "".join(parts).strip()
        else:
            raw = self._client.complete(prompt, model=self._model, temperature=self._temperature)

        parsed = extract_json_object(raw)
        if not isinstance(parsed, dict):
            parsed = {}
        return dump_agent_context_json(normalize_completed_agent_context(request_payload, parsed, raw))

    def _fallback_context(
        self,
        goal_id: str,
        agent_name: str,
        task_desc: str,
        dependencies: List[str],
        dependency_outputs: Dict[str, str],
        feedback: str,
    ) -> AgentContext:
        todo_items = []
        item_updates = []
        key_info = []

        if self._companies:
            for idx, company in enumerate(self._companies, start=1):
                item_id = f"{goal_id}_item_{idx}"
                todo_items.append(TodoItem(itemId=item_id, description=f"For {company}: {task_desc}"))
                item_updates.append(ItemStateUpdate(itemId=item_id, state=0))
                key_info.append(KeyInformation(itemId=item_id, outputabstract=""))
        else:
            item_id = f"{goal_id}_item_1"
            todo_items.append(TodoItem(itemId=item_id, description=task_desc or "Subtask"))
            item_updates.append(ItemStateUpdate(itemId=item_id, state=0))
            key_info.append(KeyInformation(itemId=item_id, outputabstract=""))

        return AgentContext(
            AgentID=goal_id,
            AgentName=agent_name,
            SubTaskID=goal_id,
            SubTaskName=task_desc,
            Dependencies=dependencies,
            DependencyOutputs=dependency_outputs,
            todoItems=todo_items,
            ItemstateUpdates=item_updates,
            KeyInformation=key_info,
            Feedback=feedback,
            full_output="",
        )

    def _normalize_context(self, generated: Dict[str, Any], fallback: AgentContext) -> AgentContext:
        todo_items_raw = ensure_list(generated.get("todoItems"))
        todo_items = []
        for item in todo_items_raw:
            if isinstance(item, dict):
                item_id = str(item.get("itemId") or "")
                desc = str(item.get("description") or "")
                if item_id:
                    todo_items.append(TodoItem(itemId=item_id, description=desc))

        if not todo_items:
            todo_items = fallback.todoItems

        updates_map = {item["itemId"]: item for item in ensure_list(generated.get("ItemstateUpdates")) if isinstance(item, dict)}
        key_info_map = {item["itemId"]: item for item in ensure_list(generated.get("KeyInformation")) if isinstance(item, dict)}

        item_updates = []
        key_info = []
        for item in todo_items:
            upd = updates_map.get(item.itemId, {})
            key = key_info_map.get(item.itemId, {})
            state = upd.get("state", 0)
            try:
                state = int(state)
            except Exception:
                state = 0
            item_updates.append(ItemStateUpdate(itemId=item.itemId, state=state))
            key_info.append(KeyInformation(itemId=item.itemId, outputabstract=str(key.get("outputabstract", ""))))

        dependency_outputs = generated.get("DependencyOutputs")
        if not isinstance(dependency_outputs, dict):
            dependency_outputs = fallback.DependencyOutputs

        return AgentContext(
            AgentID=str(generated.get("AgentID") or fallback.AgentID),
            AgentName=str(generated.get("AgentName") or fallback.AgentName),
            SubTaskID=str(generated.get("SubTaskID") or fallback.SubTaskID),
            SubTaskName=str(generated.get("SubTaskName") or fallback.SubTaskName),
            Dependencies=generated.get("Dependencies") if isinstance(generated.get("Dependencies"), list) else fallback.Dependencies,
            DependencyOutputs={str(k): str(v) for k, v in dependency_outputs.items()},
            todoItems=todo_items,
            ItemstateUpdates=item_updates,
            KeyInformation=key_info,
            Feedback=str(generated.get("Feedback") or fallback.Feedback),
            full_output=str(generated.get("full_output") or fallback.full_output),
        )
