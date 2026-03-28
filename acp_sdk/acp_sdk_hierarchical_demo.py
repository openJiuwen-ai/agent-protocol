# coding: utf-8
"""ACP SDK streaming demo with a hierarchical multi-agent workflow."""
from __future__ import annotations

import asyncio
import json
import os
import sys
import warnings
from pathlib import Path
from typing import Any, Dict, List

from openai import OpenAI

DEMO_ROOT = Path(__file__).resolve().parent
REPO_ROOT = DEMO_ROOT.parent
AGENT_CORE_ROOT = REPO_ROOT / "agent-core"
if str(AGENT_CORE_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_CORE_ROOT))

from acp_sdk import (
    ACPClient,
    ACPEvaluator,
    AgentContext,
    AgentContextBuilder,
    ACPState,
    EvaluationResult,
    GoalStatus,
    ItemStateUpdate,
    KeyInformation,
    TaskContext,
    TaskContextBuilder,
    TodoItem,
    dump_agent_context_json,
    extract_agent_context_from_workflow_result,
    extract_agent_context_json_from_workflow_result,
    install_agent_core_acp_sdk_bridge,
    normalize_completed_agent_context,
    parse_agent_context_json,
    resolve_current_goal,
    update_goal_status,
    update_task_context_from_agent_context,
)

install_agent_core_acp_sdk_bridge()

from openjiuwen.core.application.workflow_agent import WorkflowAgent
from openjiuwen.core.controller.legacy.controller import BaseController
from openjiuwen.core.controller.legacy.event.event import Event
from openjiuwen.core.runner import Runner
from openjiuwen.core.session.stream import CustomSchema
from openjiuwen.core.single_agent.legacy import AgentConfig, ControllerAgent, WorkflowAgentConfig
from openjiuwen.core.workflow import End, Start, Workflow, WorkflowCard
from openjiuwen.core.workflow.components.component import WorkflowComponent

from examples.groups.hierarchical_group.config import HierarchicalGroupConfig
from examples.groups.hierarchical_group.hierarchical_group import HierarchicalGroup

warnings.filterwarnings("ignore", category=DeprecationWarning)
os.environ["WORKFLOW_EXECUTE_TIMEOUT"] = "300"

DEMO_LLM_CONFIG = {
    "api_key": "sk-3d784c54aa334377a27821fe1f4d0091",
    "base_url": "https://api.deepseek.com/v1",
    "model": "deepseek-chat",
    "temperature": 0.1,
}

COMPANIES = ["NIO", "Li Auto", "XPeng", "BYD"]

GOALS: List[Dict[str, Any]] = [
    {
        "goal_id": "G1",
        "agent": "financial_agent",
        "agent_name": "financial_agent",
        "task_description": (
            "Collect key earnings and operating data for NIO, Li Auto, XPeng, and BYD. "
            "Cover revenue, gross margin, deliveries or sales, R&D spend, and cash reserves. "
            "Return a structured financial analysis."
        ),
        "dependencies": [],
    },
    {
        "goal_id": "G2",
        "agent": "policy_agent",
        "agent_name": "policy_agent",
        "task_description": (
            "Summarize the most relevant China NEV policy, subsidy, regulatory, and industry trend signals "
            "that affect NIO, Li Auto, XPeng, and BYD. Return a structured policy analysis."
        ),
        "dependencies": [],
    },
    {
        "goal_id": "G3",
        "agent": "investment_agent",
        "agent_name": "investment_agent",
        "task_description": (
            "Based on the financial analysis and policy analysis, produce an investment recommendation report "
            "for NIO, Li Auto, XPeng, and BYD. Include ranking, rationale, major risks, and conclusion."
        ),
        "dependencies": ["G1", "G2"],
    },
]

WORKER_PROFILES: Dict[str, Dict[str, str]] = {
    "financial_agent": {
        "role_name": "financial analysis agent",
        "capability": "Extracts business quality signals from public financial and operating data.",
    },
    "policy_agent": {
        "role_name": "policy analysis agent",
        "capability": "Summarizes NEV policy, subsidy, regulatory, and infrastructure trends.",
    },
    "investment_agent": {
        "role_name": "investment report agent",
        "capability": "Synthesizes company and policy inputs into investment logic, ranking, and risk analysis.",
    },
}


def _configure_stdout() -> None:
    stream = getattr(sys, "stdout", None)
    if stream and hasattr(stream, "reconfigure"):
        try:
            stream.reconfigure(encoding="utf-8")
        except Exception:
            pass


def _safe_print(text: str = "", end: str = "\n") -> None:
    try:
        print(text, end=end, flush=True)
    except UnicodeEncodeError:
        stream = getattr(sys, "stdout", None)
        buffer = getattr(stream, "buffer", None)
        if buffer is not None:
            buffer.write((str(text) + end).encode("utf-8", errors="ignore"))
            buffer.flush()
        else:
            sanitized = str(text).encode("ascii", errors="ignore").decode("ascii", errors="ignore")
            print(sanitized, end=end, flush=True)


def _build_demo_acp_client() -> ACPClient:
    client_kwargs = {"api_key": str(DEMO_LLM_CONFIG.get("api_key", "")).strip()}
    base_url = str(DEMO_LLM_CONFIG.get("base_url", "")).strip()
    if base_url:
        client_kwargs["base_url"] = base_url
    llm_client = OpenAI(**client_kwargs)
    return ACPClient(
        llm_client=llm_client,
        model=str(DEMO_LLM_CONFIG.get("model", "")).strip(),
        temperature=float(DEMO_LLM_CONFIG.get("temperature", 0.1)),
    )


def _preflight_check(acp_client: ACPClient) -> None:
    acp_client.complete('Return {"ok": true} as JSON only.', temperature=0.0)


async def _emit_custom(session: Any, payload: Dict[str, Any]) -> None:
    if hasattr(session, "write_custom_stream"):
        await session.write_custom_stream(payload)


def _chunk_to_dict(chunk: Any) -> Dict[str, Any]:
    if isinstance(chunk, dict):
        return chunk
    if isinstance(chunk, CustomSchema):
        return chunk.model_dump()
    if hasattr(chunk, "model_dump"):
        dumped = chunk.model_dump()
        return dumped if isinstance(dumped, dict) else {}
    if hasattr(chunk, "dict"):
        dumped = chunk.dict()
        return dumped if isinstance(dumped, dict) else {}
    return {}


class ACPResearchWorkerComponent(WorkflowComponent):
    def __init__(self, acp_client: ACPClient, worker_profile: Dict[str, str], agent_id: str):
        super().__init__()
        self._agent_id = agent_id
        self._worker_profile = worker_profile
        self._builder = AgentContextBuilder(acp_client=acp_client, companies=COMPANIES)

    async def invoke(self, inputs: Dict[str, Any], session, context) -> Dict[str, Any]:
        request_agent_context = inputs.get("agent_context", "") if isinstance(inputs, dict) else ""
        pending_streams: List[asyncio.Task[Any]] = []

        def on_chunk(chunk: str) -> None:
            pending_streams.append(
                asyncio.create_task(
                    _emit_custom(
                        session,
                        {
                            "type": "acp_worker_stream",
                            "agent_id": self._agent_id,
                            "agent_name": self._worker_profile["role_name"],
                            "delta": chunk,
                        },
                    )
                )
            )

        agent_context = self._builder.build_completed(
            request_agent_context=request_agent_context,
            role_name=self._worker_profile["role_name"],
            capability=self._worker_profile["capability"],
            use_llm=True,
            stream_handler=on_chunk,
        )
        if pending_streams:
            await asyncio.gather(*pending_streams)
        return {"agent_context": agent_context}


def _build_worker_workflow(agent_id: str, acp_client: ACPClient) -> Workflow:
    workflow = Workflow(
        card=WorkflowCard(
            id=agent_id,
            version="1.0",
            name=agent_id,
            description=f"ACP worker workflow for {agent_id}",
            input_params={
                "type": "object",
                "properties": {
                    "query": {"type": "string"},
                    "agent_context": {"type": "string"},
                },
            },
        )
    )
    workflow.set_start_comp("start", Start(), inputs_schema={"agent_context": "${agent_context}"})
    workflow.add_workflow_comp(
        "research",
        ACPResearchWorkerComponent(acp_client, WORKER_PROFILES[agent_id], agent_id),
        inputs_schema={"agent_context": "${start.agent_context}"},
    )
    workflow.set_end_comp("end", End(), inputs_schema={"agent_context": "${research.agent_context}"})
    workflow.add_connection("start", "research")
    workflow.add_connection("research", "end")
    return workflow


class NewEnergyInvestmentController(BaseController):
    def __init__(self, acp_client: ACPClient, goals: List[Dict[str, Any]]):
        super().__init__()
        self._goals = goals
        self._task_builder = TaskContextBuilder(acp_client=acp_client)
        self._agent_context_builder = AgentContextBuilder(acp_client=acp_client, companies=COMPANIES)
        self._evaluator = ACPEvaluator(acp_client=acp_client)

    async def _emit_status(self, session, message: str, **extra: Any) -> None:
        payload = {"type": "acp_controller_status", "message": message}
        payload.update(extra)
        await _emit_custom(session, payload)

    async def _emit_llm_stream(self, session, stage: str, delta: str, **extra: Any) -> None:
        payload = {"type": "acp_sdk_stream", "stage": stage, "delta": delta}
        payload.update(extra)
        await _emit_custom(session, payload)

    async def handle_event(self, event: Event, session):
        task_context_streams: List[asyncio.Task[Any]] = []

        def on_task_context_chunk(chunk: str) -> None:
            task_context_streams.append(asyncio.create_task(self._emit_llm_stream(session, "task_context", chunk)))

        task_context = self._task_builder.build(
            task_id="T1",
            task_name="NEV Investment Recommendation Analysis",
            task_description="The controller agent orchestrates three subagents for financial, policy, and investment analysis.",
            goals=self._goals,
            use_llm=True,
            stream_handler=on_task_context_chunk,
        )
        if task_context_streams:
            await asyncio.gather(*task_context_streams)

        results_by_goal: Dict[str, Dict[str, Any]] = {}
        await self._emit_status(session, "Controller generated TaskContext.", stage="task_context")

        for goal in self._goals:
            feedback = ""
            retry_count = 0

            while True:
                dependency_outputs = {
                    dep_goal_id: str(results_by_goal[dep_goal_id]["agent_context"].get("full_output", "") or "")
                    for dep_goal_id in goal.get("dependencies", [])
                    if dep_goal_id in results_by_goal
                }

                current_goal = resolve_current_goal(task_context)
                request_streams: List[asyncio.Task[Any]] = []

                def on_request_chunk(chunk: str) -> None:
                    request_streams.append(
                        asyncio.create_task(
                            self._emit_llm_stream(
                                session,
                                "request_agent_context",
                                chunk,
                                goal_id=current_goal["goal_id"],
                                agent_id=current_goal["agent"],
                            )
                        )
                    )

                request_agent_context = self._agent_context_builder.build(
                    task_context=json.dumps(task_context, ensure_ascii=False),
                    dependency_outputs=json.dumps(dependency_outputs, ensure_ascii=False),
                    feedback=feedback,
                    use_llm=True,
                    stream_handler=on_request_chunk,
                )
                if request_streams:
                    await asyncio.gather(*request_streams)

                await self._emit_status(
                    session,
                    f"Dispatching {current_goal.get('agent_name', current_goal['agent'])}.",
                    stage="dispatch",
                    goal_id=current_goal["goal_id"],
                    agent_id=current_goal["agent"],
                    retry_count=retry_count,
                )

                sub_event = Event.create_user_event(
                    content=current_goal["task_description"],
                    conversation_id=session.get_session_id(),
                    extensions={"agent_context": request_agent_context},
                )
                sub_result = await self.send_to_agent(current_goal["agent"], sub_event, session)
                agent_context_json = extract_agent_context_from_workflow_result(sub_result)
                agent_context = parse_agent_context_json(agent_context_json)

                evaluator_streams: List[asyncio.Task[Any]] = []

                def on_evaluator_chunk(chunk: str) -> None:
                    evaluator_streams.append(
                        asyncio.create_task(
                            self._emit_llm_stream(
                                session,
                                "evaluator",
                                chunk,
                                goal_id=current_goal["goal_id"],
                                agent_id=current_goal["agent"],
                            )
                        )
                    )

                eval_result = self._evaluator.evaluate(
                    agent_context_json,
                    retry_count=retry_count,
                    stream_handler=on_evaluator_chunk,
                )
                if evaluator_streams:
                    await asyncio.gather(*evaluator_streams)
                retry_count = int(eval_result.get("retry_count", retry_count))
                await self._emit_status(
                    session,
                    f"{current_goal.get('agent_name', current_goal['agent'])} evaluation finished, decision={eval_result['decision']}",
                    stage="evaluation",
                    goal_id=current_goal["goal_id"],
                    agent_id=current_goal["agent"],
                    evaluation=eval_result,
                )

                if eval_result["decision"] in {"pass", "force_pass"}:
                    results_by_goal[current_goal["goal_id"]] = {
                        "agent": current_goal["agent"],
                        "agent_context_json": agent_context_json,
                        "agent_context": agent_context,
                        "evaluation": eval_result,
                    }
                    update_task_context_from_agent_context(task_context, agent_context)
                    break

                feedback = str(eval_result.get("feedback", "") or "")
                await self._emit_status(
                    session,
                    f"{current_goal.get('agent_name', current_goal['agent'])} failed evaluation and will retry.",
                    stage="retry",
                    goal_id=current_goal["goal_id"],
                    agent_id=current_goal["agent"],
                    feedback=feedback,
                    retry_count=retry_count,
                )

        final_report = str(results_by_goal.get("G3", {}).get("agent_context", {}).get("full_output", "") or "")
        final_payload = {"final_report": final_report}
        await _emit_custom(session, {"type": "acp_final_result", "payload": final_payload})
        return final_payload


def _build_group(acp_client: ACPClient) -> HierarchicalGroup:
    worker_agents: Dict[str, WorkflowAgent] = {}
    for goal in GOALS:
        agent_id = goal["agent"]
        worker_agent = WorkflowAgent(
            WorkflowAgentConfig(id=agent_id, version="1.0", description=goal["agent_name"], workflows=[])
        )
        worker_agent.add_workflows([_build_worker_workflow(agent_id, acp_client)])
        worker_agents[agent_id] = worker_agent

    controller_agent = ControllerAgent(
        AgentConfig(id="main_controller", version="1.0", description="Main controller agent for the NEV investment recommendation task."),
        controller=NewEnergyInvestmentController(acp_client, GOALS),
    )

    group = HierarchicalGroup(
        HierarchicalGroupConfig(group_id="nev_investment_demo_group", leader_agent_id="main_controller", max_agents=4)
    )
    group.add_agent("main_controller", controller_agent)
    for agent_id, agent in worker_agents.items():
        group.add_agent(agent_id, agent)
    return group


def _render_stream_chunk(chunk: Any) -> Dict[str, Any] | None:
    payload = _chunk_to_dict(chunk)
    if not payload:
        return None
    chunk_type = payload.get("type")
    if chunk_type == "acp_controller_status":
        _safe_print(f"[controller] {payload.get('message', '')}")
        return payload
    if chunk_type == "acp_worker_stream":
        delta = str(payload.get("delta", "") or "")
        if delta:
            _safe_print(delta, end="")
        return payload
    if chunk_type == "acp_sdk_stream":
        delta = str(payload.get("delta", "") or "")
        stage = str(payload.get("stage", "") or "sdk")
        if delta:
            _safe_print(f"[{stage}] {delta}", end="")
        return payload
    if chunk_type == "acp_final_result":
        _safe_print("")
        return payload
    return None


async def main() -> None:
    _configure_stdout()
    acp_client = _build_demo_acp_client()
    _preflight_check(acp_client)
    group = _build_group(acp_client)
    final_result: Dict[str, Any] = {}

    await Runner.start()
    try:
        event = Event.create_user_event(
            content="Please analyze investment recommendations for NIO, Li Auto, XPeng, and BYD.",
            conversation_id="acp_nev_demo",
        )
        async for chunk in group.stream(event):
            payload = _render_stream_chunk(chunk)
            if payload and payload.get("type") == "acp_final_result":
                final_result = payload.get("payload", {}) or {}
    finally:
        await Runner.stop()

    if not final_result:
        raise RuntimeError("Demo finished without receiving acp_final_result.")

    _safe_print("\n========== Final Investment Report ==========")
    _safe_print(str(final_result.get("final_report", "") or ""))


if __name__ == "__main__":
    asyncio.run(main())
