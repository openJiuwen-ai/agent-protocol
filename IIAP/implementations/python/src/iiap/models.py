"""Model and business-agent extension ports."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Awaitable, Callable, Literal, Protocol


@dataclass(frozen=True)
class ModelRequest:
    prompt: str
    payload: dict[str, Any]
    operation: Literal["decision", "assistance"]


class ModelAdapter(Protocol):
    async def generate_decision(self, request: ModelRequest) -> object: ...
    async def generate_assistance(self, request: ModelRequest) -> object: ...


AgentCall = Callable[[ModelRequest], Awaitable[object]]


class AgentRouter:
    """Routes IIAP calls to an independent model or the host business Agent."""

    def __init__(
        self,
        independent: ModelAdapter,
        *,
        business_decision: AgentCall | None = None,
        business_assistance: AgentCall | None = None,
        route: Literal["independent", "business_agent"] = "independent",
    ) -> None:
        self._independent = independent
        self._business_decision = business_decision
        self._business_assistance = business_assistance
        self._route = route

    async def generate_decision(self, request: ModelRequest) -> object:
        if self._route == "business_agent" and self._business_decision:
            return await self._business_decision(request)
        return await self._independent.generate_decision(request)

    async def generate_assistance(self, request: ModelRequest) -> object:
        if self._route == "business_agent" and self._business_assistance:
            return await self._business_assistance(request)
        return await self._independent.generate_assistance(request)
