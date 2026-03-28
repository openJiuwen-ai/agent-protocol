# ACP SDK

英文文档见 `README.md`。

## 简介
`ACP(Agent Context Protocol)`是一种智能体上下文交互协议，主要通过构造结构化上下文进行交互来节约模型token消耗数、优化任务执行时间并提升任务执行成功率。
`ACP SDK` 是一个与 `agent-core` 解耦的结构化多智能体协作工具包，负责 ACP 风格结构化上下文的生成、更新、解析与评估，而 `agent-core` 负责智能体编排、消息路由和执行控制，通过调用`ACP SDK`来实现结构化上下文的生成和交互。

当前 `ACP SDK` 主要提供这些能力：
- 生成 `TaskContext`，供主 agent 跟踪全局任务进度。
- 基于最新 `TaskContext` 生成请求态 `AgentContext`。
- 子 agent 基于请求态 `AgentContext` 更新并返回完成态 `AgentContext`。
- 主 agent 对完成态 `AgentContext` 进行评估，并在评估通过后更新 `TaskContext`。
- 在 SDK 关键链路中支持流式大模型调用。

## 核心概念
### TaskContext
由主 agent 使用，表示整个任务的全局状态。

典型字段：
- `TaskID`
- `TaskName`
- `TaskDescription`
- `Goals`
- `GoalStatus`

其中 `Goals` 保存结构化任务定义，包括：
- `goal_id`
- `task_description`
- `agent`
- `dependencies`

### AgentContext
`AgentContext` 是主 agent 和子 agent 之间唯一的结构化交互载体。

它有两个阶段：
- 请求态 `AgentContext`：由主 agent 基于最新 `TaskContext` 生成。
- 完成态 `AgentContext`：由子 agent 在请求态上下文基础上更新后返回。

典型字段：
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

重要说明：
- `DependencyOutputs` 承载的是上游子 agent 的 `full_output`，而不是结构化摘要。
- controller 不再显式传入 `current_goal`，而是由 SDK 根据 `TaskContext.Goals` 与 `TaskContext.GoalStatus` 推断当前任务。
- controller 与 worker 之间统一交换 `AgentContext`，不再使用单独的 `ACPSubtaskRequest` 概念。

## 推荐流程
1. 主 agent 生成 `TaskContext`。
2. 调度下一个子 agent 前，主 agent 调用 `AgentContextBuilder.build(...)`，输入最新 `TaskContext`、依赖 `full_output` 和反馈信息。
3. 主 agent 将这个请求态 `AgentContext` 发送给子 agent。
4. 子 agent 调用 `AgentContextBuilder.build_completed(...)`，把请求态 `AgentContext` 更新为完成态 `AgentContext`。
5. 主 agent 调用 `ACPEvaluator.evaluate(...)` 评估子 agent 返回结果。
6. 如果评估通过，主 agent 调用 `update_task_context_from_agent_context(...)` 更新 `TaskContext`。

## 快速开始
以新能源车企投资建议分析任务为例展开介绍。

### 1. 创建 LLM Client
```python
from openai import OpenAI
from acp_sdk import ACPClient

llm_client = OpenAI(
    api_key="your_api_key",
    base_url="your_llm_url",
)

acp_client = ACPClient(
    llm_client=llm_client,
    model="your_model",
    temperature=0.1,
)
```

### 2. 生成 TaskContext
```python
from acp_sdk import TaskContextBuilder

builder = TaskContextBuilder(acp_client=acp_client)
task_context_json = builder.build(
    task_name="新能源车企投资建议分析",
    task_description="分析蔚来、理想、小鹏、比亚迪的投资价值",
    goals=goals,
)
```
需要注意的是，goals可以通过调用agent-core的任务拆解工具来实现，此处仅为示例。

### 3. 主 agent 生成请求态 AgentContext
```python
from acp_sdk import AgentContextBuilder

builder = AgentContextBuilder(acp_client=acp_client, companies=["NIO", "Li Auto", "XPeng", "BYD"])
request_agent_context_json = builder.build(
    task_context=task_context_json,
    dependency_outputs=dependency_outputs_json,
    feedback="",
)
```

### 4. 子 agent 生成完成态 AgentContext
```python
completed_agent_context_json = builder.build_completed(
    request_agent_context=request_agent_context_json,
    role_name="financial analysis agent",
    capability="Extracts business quality signals from public financial data.",
)
```

### 5. 主 agent 评估并更新 TaskContext
```python
from acp_sdk import ACPEvaluator, update_task_context_from_agent_context

evaluator = ACPEvaluator(acp_client=acp_client)
result = evaluator.evaluate(completed_agent_context_json)
if result["decision"] == "pass":
    task_context = update_task_context_from_agent_context(task_context_json, completed_agent_context_json)
```

## 核心 API 说明
### `ACPClient.complete(...)`
目的：
- 发起一次完整的 LLM 调用。


### `ACPClient.stream_complete(...)`
目的：
- 直接以流式方式返回模型输出。


### `TaskContextBuilder.build(...)`
目的：
- 根据任务描述和 goals 生成 `TaskContext`。

常见输入：
- `task_name`
- `task_description`
- `goals`
- `stream_handler`

输出：
- `TaskContext` JSON 字符串。

### `AgentContextBuilder.build(...)`
目的：
- 由主 agent 根据最新 `TaskContext`、依赖 `full_output` 和反馈，生成请求态 `AgentContext`。

常见输入：
- `task_context`
- `dependency_outputs`
- `feedback`
- `stream_handler`

输出：
- 请求态 `AgentContext` JSON 字符串。

### `AgentContextBuilder.build_completed(...)`
目的：
- 由子 agent 基于请求态 `AgentContext` 生成完成态 `AgentContext`。

常见输入：
- `request_agent_context`
- `role_name`
- `capability`
- `use_llm`
- `stream_handler`

输出：
- 完成态 `AgentContext` JSON 字符串。

### `ACPEvaluator.evaluate(...)`
目的：
- 评估完成态 `AgentContext`。
- 如果存在任意 `ItemstateUpdates.state == 0`，直接返回不合格。
- 否则调用 LLM 输出 `pass`、`retry` 或 `force_pass`。

### `resolve_current_goal(task_context)`
目的：
- 读取 `TaskContext.Goals` 和 `TaskContext.GoalStatus`。
- 返回第一个状态不是 `Finished` 的 goal。

### `update_task_context_from_agent_context(task_context, agent_context)`
目的：
- 在子 agent 评估通过后更新 `TaskContext`。
- 如果对应任务的所有 item 状态都为 `1`，则将该 goal 标记为 `Finished`。

## 流式支持
SDK 中主要的 LLM 调用都支持流式：
- `ACPClient.stream_complete(...)`
- `TaskContextBuilder.build(..., stream_handler=...)`
- `AgentContextBuilder.build(..., stream_handler=...)`
- `AgentContextBuilder.build_completed(..., stream_handler=...)`
- `ACPEvaluator.evaluate(..., stream_handler=...)`

## 运行时桥接 Helper
如果希望需运行会导入 `openjiuwen.core.multi_agent.acp_sdk` 的 `agent-core` 代码路径，可以使用 SDK 提供的运行时桥接 helper。

### `install_agent_core_acp_sdk_bridge()`
目的：
- 在运行时把独立 SDK 注册到 `sys.modules["openjiuwen.core.multi_agent.acp_sdk"]`。
- 避免在 `agent-core` 目录下增加 ACP SDK shim 文件。
- 让 demo 或外部集成在不改动 `agent-core` 磁盘内容的前提下完成接入。

典型用法：
```python
from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parent
AGENT_CORE_ROOT = REPO_ROOT / "agent-core"
if str(AGENT_CORE_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_CORE_ROOT))

from acp_sdk import install_agent_core_acp_sdk_bridge

install_agent_core_acp_sdk_bridge()

from openjiuwen.core.single_agent.legacy import ControllerAgent
```

。

## Demo
文件：`acp_sdk/acp_sdk_hierarchical_demo.py`

这个 demo 展示了：
- 1 个 controller agent
- 3 个 workflow agent
- 主 agent 到子 agent 用 `AgentContext` 发送结构化请求
- 生成请求态 `AgentContext` 时不再显式传入 `current_goal`
- 子 agent 返回更新后的 `AgentContext`
- 每个子 agent 执行完成后立刻评估
- 评估通过后再根据返回结果更新 `TaskContext`
- 全流程保留流式输出
