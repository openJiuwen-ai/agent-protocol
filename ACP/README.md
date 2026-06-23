# ACP SDK

For Chinese documentation, see `README.zh-CN.md`.

## Overview
ACP SDK is a decoupled toolkit for structured multi-agent collaboration. It handles ACP-style structured context generation, updating, parsing, and evaluation, while `agent-core` focuses on orchestration and routing,.

Current capabilities:
- Build `TaskContext` for the controller agent.
- Build request-stage `AgentContext` from the latest `TaskContext`.
- Let worker agents update request-stage `AgentContext` into completed `AgentContext`.
- Evaluate completed `AgentContext` and update `TaskContext` after evaluation passes.
- Support streaming LLM calls across the SDK workflow.

## Core Concepts
### TaskContext
Global task state used by the controller agent.

Typical fields:
- `TaskID`
- `TaskName`
- `TaskDescription`
- `Goals`
- `GoalStatus`

`Goals` keeps the structured task definitions, including:
- `goal_id`
- `task_description`
- `agent`
- `dependencies`

### AgentContext
`AgentContext` is the only structured payload exchanged between controller and worker agents.

It has two stages:
- Request-stage `AgentContext`: built by the controller from the latest `TaskContext`.
- Completed `AgentContext`: returned by the worker after updating the request.

Typical fields:
- `AgentID`
- `AgentName`
- `SubTaskID`
- `SubTaskName`
- `Dependencies`
- `DependencyOutputs`
- `todoItems`
- `ItemstateUpdates`
- `KeyInformation`
- `Feedback`
- `full_output`

Important:
- `DependencyOutputs` carries upstream workers' `full_output`.
- There is no separate `ACPSubtaskRequest` concept anymore.
- The controller infers the current goal directly from `TaskContext.Goals` and `TaskContext.GoalStatus`.

## Recommended Flow
1. The controller builds a `TaskContext`.
2. Before dispatching a worker, the controller calls `AgentContextBuilder.build(...)` with the latest `TaskContext`, dependency `full_output`, and retry feedback.
3. The controller sends that request-stage `AgentContext` to the worker.
4. The worker calls `AgentContextBuilder.build_completed(...)` to update the request-stage `AgentContext` into a completed `AgentContext`.
5. The controller evaluates the returned `AgentContext` with `ACPEvaluator.evaluate(...)`.
6. If evaluation passes, the controller updates `TaskContext` through `update_task_context_from_agent_context(...)`.

## Quick Start
### 1. Create an LLM client
```python
from openai import OpenAI
from sdk_core import ACPClient

llm_client = OpenAI(
    api_key="your_api_key",
    base_url="your_url",
)

acp_client = ACPClient(
    llm_client=llm_client,
    model="llm_model",
    temperature=xx,
)
```

### 2. Build a TaskContext
```python
from sdk_core import TaskContextBuilder

builder = TaskContextBuilder(acp_client=acp_client)

task_context = builder.build(
    task_id="T1",
    task_name="NEV Investment Analysis",
    task_description="Controller orchestrates three workers.",
    goals=[
        {"goal_id": "G1", "task_description": "Collect financial data", "agent": "financial_agent", "dependencies": []},
        {"goal_id": "G2", "task_description": "Collect policy signals", "agent": "policy_agent", "dependencies": []},
        {"goal_id": "G3", "task_description": "Write investment report", "agent": "investment_agent", "dependencies": ["G1", "G2"]},
    ],
    use_llm=True,
)
```

### 3. Build a request-stage AgentContext
```python
from sdk_core import AgentContextBuilder

agent_builder = AgentContextBuilder(acp_client=acp_client)

request_agent_context = agent_builder.build(
    task_context=task_context,
    dependency_outputs={
        "G1": "financial agent full_output",
        "G2": "policy agent full_output",
    },
    feedback="",
    use_llm=True,
)
```

### 4. Resolve which goal is currently active
```python
from sdk_core import resolve_current_goal

current_goal = resolve_current_goal(task_context)
worker_agent_id = current_goal["agent"]
```

### 5. Update it in the worker agent
```python
completed_agent_context = agent_builder.build_completed(
    request_agent_context=request_agent_context,
    role_name="investment report agent",
    capability="Synthesizes financial and policy inputs into a report",
    use_llm=True,
)
```

### 6. Evaluate and update TaskContext
```python
from sdk_core import ACPEvaluator, update_task_context_from_agent_context

evaluator = ACPEvaluator(acp_client=acp_client)
eval_result = evaluator.evaluate(completed_agent_context, retry_count=0)

if eval_result["decision"] in {"pass", "force_pass"}:
    task_context = update_task_context_from_agent_context(task_context, completed_agent_context)
```

## API Reference
### `TaskContextBuilder.build(...)`
Purpose:
- Build the latest `TaskContext` for the controller.
- Keep both `Goals` and `GoalStatus` in the structured output.

Main inputs:
- `task_id`
- `task_name`
- `task_description`
- `goals`
- `use_llm`
- `stream_handler`

### `AgentContextBuilder.build(...)`
Purpose:
- Build a request-stage `AgentContext` for the next worker.
- Infer the current goal from `TaskContext`, instead of receiving a separate `goal` argument.

Main inputs:
- `task_context`
- `dependency_outputs`
- `feedback`
- `use_llm`
- `stream_handler`

### `AgentContextBuilder.build_completed(...)`
Purpose:
- Update a request-stage `AgentContext` into a completed `AgentContext` in the worker.

Main inputs:
- `request_agent_context`
- `role_name`
- `capability`
- `use_llm`
- `stream_handler`

### `ACPEvaluator.evaluate(...)`
Purpose:
- Evaluate a completed `AgentContext`.
- If any `ItemstateUpdates.state == 0`, it fails immediately.
- Otherwise it asks the LLM for `pass`, `retry`, or `force_pass`.

### `resolve_current_goal(task_context)`
Purpose:
- Read `TaskContext.Goals` and `TaskContext.GoalStatus`.
- Return the first goal that is not `Finished`.

### `update_task_context_from_agent_context(task_context, agent_context)`
Purpose:
- Update `TaskContext` after a worker passes evaluation.
- Mark the corresponding goal as `Finished` when all item states are `1`.

## Streaming Support
Streaming is supported throughout the SDK:
- `ACPClient.stream_complete(...)`
- `TaskContextBuilder.build(..., stream_handler=...)`
- `AgentContextBuilder.build(..., stream_handler=...)`
- `AgentContextBuilder.build_completed(..., stream_handler=...)`
- `ACPEvaluator.evaluate(..., stream_handler=...)`

## Demo
File: `ACP/acp_sdk_hierarchical_demo.py`

The demo shows:
- 1 controller agent
- 3 workflow agents
- controller-to-worker requests using `AgentContext`
- no explicit `current_goal` input when building request-stage `AgentContext`
- worker-to-controller responses using updated `AgentContext`
- evaluation after each worker
- `TaskContext` updates after evaluation passes
- streaming output throughout execution

Run with an OpenAI-compatible model:
```bash
export ACP_DEMO_API_KEY="your_api_key"
export ACP_DEMO_MODEL="your_model"
export ACP_DEMO_BASE_URL="your_openai_compatible_base_url"
export ACP_DEMO_TEMPERATURE="0.1"
export ACP_DEMO_TIMEOUT="60"
python ACP/acp_sdk_hierarchical_demo.py
```

On PowerShell:
```powershell
$env:ACP_DEMO_API_KEY = "your_api_key"
$env:ACP_DEMO_MODEL = "your_model"
$env:ACP_DEMO_BASE_URL = "your_openai_compatible_base_url"
$env:ACP_DEMO_TEMPERATURE = "0.1"
$env:ACP_DEMO_TIMEOUT = "60"
python ACP\acp_sdk_hierarchical_demo.py
```

Environment variables:
- `ACP_DEMO_API_KEY`: required.
- `ACP_DEMO_MODEL`: required.
- `ACP_DEMO_BASE_URL`: optional OpenAI-compatible base URL.
- `ACP_DEMO_TEMPERATURE`: optional, defaults to `0.1`.
- `ACP_DEMO_TIMEOUT`: optional, defaults to `60` seconds.

Runtime flow:
1. The controller builds the initial `TaskContext`.
2. The controller resolves the next unfinished goal and builds a request-stage `AgentContext`.
3. The target worker workflow updates the request into a completed `AgentContext`.
4. The controller evaluates the completed `AgentContext`.
5. If evaluation passes, the controller updates `TaskContext` and dispatches the next worker.
6. After the investment worker finishes, the demo prints the final report.

## Runtime Bridge Helper
When you want to keep ACP SDK physically outside `agent-core`, but still run `agent-core` code paths that import `openjiuwen.core.multi_agent.acp_sdk`, use the runtime bridge helper from the standalone SDK.

### `install_agent_core_acp_sdk_bridge()`
Purpose:
- Register the standalone SDK into `sys.modules["openjiuwen.core.multi_agent.acp_sdk"]` at runtime.
- Avoid adding ACP SDK shim files under `agent-core`.
- Let demos and external integrations keep `agent-core` unchanged on disk.

Typical usage:
```python
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parent
AGENT_CORE_ROOT = REPO_ROOT / "agent-core"
if str(AGENT_CORE_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_CORE_ROOT))

from sdk_core import install_agent_core_acp_sdk_bridge

install_agent_core_acp_sdk_bridge()

from openjiuwen.core.single_agent.legacy import ControllerAgent
```



