# coding: utf-8
"""ACP data models."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, TypedDict, Literal


class ACPState(TypedDict):
    task_results: Dict[str, Any]
    current_agent: str
    retry_count: int
    eval_decision: str


@dataclass
class TodoItem:
    itemId: str
    description: str

    def to_dict(self) -> Dict[str, Any]:
        return {"itemId": self.itemId, "description": self.description}


@dataclass
class ItemStateUpdate:
    itemId: str
    state: int

    def to_dict(self) -> Dict[str, Any]:
        return {"itemId": self.itemId, "state": self.state}


@dataclass
class KeyInformation:
    itemId: str
    outputabstract: str

    def to_dict(self) -> Dict[str, Any]:
        return {"itemId": self.itemId, "outputabstract": self.outputabstract}


@dataclass
class GoalStatus:
    Goal: str
    Status: str

    def to_dict(self) -> Dict[str, Any]:
        return {"Goal": self.Goal, "Status": self.Status}


@dataclass
class TaskContext:
    TaskID: str
    TaskName: str
    TaskDescription: str
    Goals: List[Dict[str, Any]] = field(default_factory=list)
    GoalStatus: List[GoalStatus] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "TaskID": self.TaskID,
            "TaskName": self.TaskName,
            "TaskDescription": self.TaskDescription,
            "Goals": self.Goals,
            "GoalStatus": [item.to_dict() for item in self.GoalStatus],
        }

    def to_payload(self) -> Dict[str, Any]:
        return {"TaskContext": self.to_dict()}


@dataclass
class AgentContext:
    AgentID: str
    AgentName: str
    SubTaskID: str
    SubTaskName: str = ""
    Dependencies: List[str] = field(default_factory=list)
    DependencyOutputs: Dict[str, str] = field(default_factory=dict)
    todoItems: List[TodoItem] = field(default_factory=list)
    ItemstateUpdates: List[ItemStateUpdate] = field(default_factory=list)
    KeyInformation: List[KeyInformation] = field(default_factory=list)
    Feedback: str = ""
    full_output: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "AgentID": self.AgentID,
            "AgentName": self.AgentName,
            "SubTaskID": self.SubTaskID,
            "SubTaskName": self.SubTaskName,
            "Dependencies": self.Dependencies,
            "DependencyOutputs": self.DependencyOutputs,
            "todoItems": [item.to_dict() for item in self.todoItems],
            "ItemstateUpdates": [item.to_dict() for item in self.ItemstateUpdates],
            "KeyInformation": [item.to_dict() for item in self.KeyInformation],
            "Feedback": self.Feedback,
            "full_output": self.full_output,
        }


DecisionType = Literal["pass", "retry", "force_pass"]


@dataclass
class EvaluationResult:
    decision: DecisionType
    feedback: str
    retry_count: int
    raw_eval: str

    def to_dict(self) -> Dict[str, Any]:
        return {
            "decision": self.decision,
            "feedback": self.feedback,
            "retry_count": self.retry_count,
            "raw_eval": self.raw_eval,
        }
