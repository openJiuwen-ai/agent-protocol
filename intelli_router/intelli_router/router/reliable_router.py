"""
SDK LLM Router - 可靠性路由

ReliableRouter: 集成状态管理、策略选择、健康检查
"""
from typing import Dict, List, Optional, Any, Union, Literal, AsyncIterator
from dataclasses import dataclass, field
import asyncio
import time
import json
import httpx

from .base_router import BaseRouter
from ..core.deployment import Deployment, DeploymentStatus
from ..core.context import RoutingContext
from ..core.state import LocalRouterState
from ..strategy.base_strategy import RoutingStrategy
from ..strategy import create_strategy, StrategyType
from ..health.checker import SDKHealthChecker
from ..cache.local_cache import LocalCache
from ..utils.exceptions import RouterError, NoDeploymentAvailable, AllDeploymentsFailed
from ..types import (
    AssistantMessage,
    AssistantMessageChunk,
    ToolCall,
    UsageMetadata,
)
from ..parser.base import BaseOutputParser
from ..observability.bus import EventBus
from ..observability.events import RoutingEvent, RoutingEventType

MODEL_WILDCARD = "*"


class ReliableRouter(BaseRouter):
    """
    可靠性路由器 - 集成状态管理，策略选择，健康检查

    特性:
    - API池化: 一个模型名 → 多个部署
    - 策略插拔: 随机/延迟/标签策略
    - 状态管理: 失败计数、冷却机制
    - 健康检查: 后台健康检查
    - 重试机制: 自动重试
    - 流式支持: 支持流式响应
    """

    def __init__(
        self,
        deployments: List[Deployment],
        strategy: Union[StrategyType, RoutingStrategy] = "simple-shuffle",
        num_retries: int = 3,
        timeout: float = 30.0,
        cooldown_time: float = 60.0,
        enable_health_check: bool = False,
        health_check_interval: float = 300,
        cache: Optional[LocalCache] = None,
        event_bus: Optional[EventBus] = None,
        **strategy_kwargs
    ):
        super().__init__(
            deployments=deployments,
            num_retries=num_retries,
            timeout=timeout,
            cache=cache,
        )
        # 状态管理
        self.state = LocalRouterState()
        # 策略
        if isinstance(strategy, str):
            self.strategy = create_strategy(strategy, state=self.state, **strategy_kwargs)
        else:
            self.strategy = strategy
        # cooldown 参数
        self.cooldown_time = cooldown_time
        # 可观测性
        self.event_bus = event_bus or EventBus()
        # 健康检查（可选，用于加速恢复）
        self.health_checker: Optional[SDKHealthChecker] = None
        if enable_health_check:
            self.health_checker = SDKHealthChecker(
                deployments=deployments,
                state=self.state,
                check_interval=health_check_interval
            )

    async def __aenter__(self):
        """异步上下文管理器入口"""
        if self.health_checker:
            await self.health_checker.start_background_check()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """异步上下文管理器出口"""
        if self.health_checker:
            await self.health_checker.stop_background_check()
        await self.close()
        return False

    def _get_available_deployments(self, model: str) -> List[Deployment]:
        """获取可用部署

        model="*" 时从所有 deployment 中选（统一调度）。
        COOLDOWN 超时后自动恢复为 HEALTHY。
        没有 FAILED 状态——所有失败都是可自愈的。
        """
        now = time.time()
        if model == MODEL_WILDCARD:
            all_deployments = self.deployments
        else:
            all_deployments = self.get_deployments_for_model(model)
        with self.state.lock:
            available = []
            for dep in all_deployments:
                status = self.state.deployment_status.get(dep.id, DeploymentStatus.HEALTHY)
                if status == DeploymentStatus.COOLDOWN:
                    cooldown_until = self.state.cooldown_until.get(dep.id, 0)
                    if now < cooldown_until:
                        continue
                    self.state.deployment_status[dep.id] = DeploymentStatus.HEALTHY
                    self.state.cooldown_until[dep.id] = None
                available.append(dep)
        return available

    async def completion(
        self,
        model: str,
        messages: List[Dict[str, str]],
        **kwargs
    ) -> Any:
        """
        可靠性completion - 自动路由选择和重试
        """
        request_id = RoutingEvent.new_request_id()
        total_attempts = self.num_retries + 1

        available = self._get_available_deployments(model)
        if not available:
            await self.event_bus.emit(RoutingEvent(
                event_type=RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED,
                request_id=request_id,
                model=model,
                error_message="No available deployments",
            ))
            raise NoDeploymentAvailable(f"No available deployment for model: {model}")

        await self.event_bus.emit(RoutingEvent(
            event_type=RoutingEventType.REQUEST_STARTED,
            request_id=request_id,
            model=model,
            total_attempts=total_attempts,
        ))

        context = RoutingContext(
            model=model,
            messages=messages,
            kwargs=kwargs
        )
        errors = []
        overall_start = time.time()

        for attempt in range(total_attempts):
            selected = await self.strategy.select_deployment(available, context)
            if selected is None:
                if available:
                    selected = available[0]
                else:
                    break
            context.mark_attempt(selected)
            try:
                start_time = time.time()
                actual_model = selected.model_name if model == MODEL_WILDCARD else model
                response = await super().completion(
                    model=actual_model,
                    messages=messages,
                    deployment=selected,
                    **kwargs
                )
                end_time = time.time()
                latency = end_time - start_time
                usage = response.get('usage') or {}
                tokens = usage.get('completion_tokens', 0)
                prompt_tokens = usage.get('prompt_tokens', 0)
                self.strategy.on_success(selected, latency, tokens)
                context.set_success(selected, response)

                await self.event_bus.emit(RoutingEvent(
                    event_type=RoutingEventType.REQUEST_SUCCEEDED,
                    request_id=request_id,
                    model=model,
                    deployment_id=selected.id,
                    provider=selected.provider,
                    latency=latency,
                    prompt_tokens=prompt_tokens,
                    completion_tokens=tokens,
                    total_tokens=prompt_tokens + tokens,
                    attempt=attempt + 1,
                    total_attempts=total_attempts,
                ))
                return response
            except Exception as e:
                self.strategy.on_failure(selected, e)
                context.set_failure(e)
                errors.append((selected.id, str(e)))

                if attempt < self.num_retries:
                    await self.event_bus.emit(RoutingEvent(
                        event_type=RoutingEventType.REQUEST_RETRIED,
                        request_id=request_id,
                        model=model,
                        deployment_id=selected.id,
                        provider=selected.provider,
                        attempt=attempt + 1,
                        total_attempts=total_attempts,
                        error_type=type(e).__name__,
                        error_message=str(e),
                    ))

                available = [d for d in available if d.id != selected.id]
                if not available:
                    break

        await self.event_bus.emit(RoutingEvent(
            event_type=RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED,
            request_id=request_id,
            model=model,
            latency=time.time() - overall_start,
            attempt=len(errors),
            total_attempts=total_attempts,
            error_type=type(context.error).__name__ if context.error else None,
            error_message=str(context.error) if context.error else None,
        ))
        raise AllDeploymentsFailed(model=model, errors=errors)

    async def stream_completion(
        self,
        model: str,
        messages: List[Dict[str, str]],
        **kwargs
    ) -> AsyncIterator[Dict[str, Any]]:
        """
        可靠性流式completion - 自动路由选择和重试

        Retry only happens before the first chunk is received.
        Once streaming starts, mid-stream errors are raised directly.
        """
        available = self._get_available_deployments(model)
        if not available:
            raise NoDeploymentAvailable(f"No available deployment for model: {model}")

        context = RoutingContext(
            model=model,
            messages=messages,
            kwargs=kwargs
        )

        errors = []
        for attempt in range(self.num_retries + 1):
            selected = await self.strategy.select_deployment(available, context)
            if selected is None:
                if available:
                    selected = available[0]
                else:
                    break

            context.mark_attempt(selected)
            first_chunk_received = False
            ttfb = 0.0

            try:
                start_time = time.time()
                actual_model = selected.model_name if model == MODEL_WILDCARD else model
                async for chunk in self.acompletion_stream(
                    model=actual_model,
                    messages=messages,
                    deployment=selected,
                    **kwargs,
                ):
                    if not first_chunk_received:
                        first_chunk_received = True
                        ttfb = time.time() - start_time
                    yield chunk

                self.strategy.on_success(selected, ttfb, 0)
                context.set_success(selected, None)
                return

            except Exception as e:
                if first_chunk_received:
                    self.strategy.on_failure(selected, e)
                    raise

                self.strategy.on_failure(selected, e)
                context.set_failure(e)
                errors.append((selected.id, str(e)))
                available = [d for d in available if d.id != selected.id]
                if not available:
                    break

        raise AllDeploymentsFailed(model=model, errors=errors)

    async def batch_completion(
        self,
        requests: List[Dict[str, Any]],
        max_concurrent: int = 10
    ) -> List[Any]:
        """
        批量completion

        Args:
            requests: 请求列表，每个包含 model, messages, **kwargs
            max_concurrent: 最大并发数
        """
        semaphore = asyncio.Semaphore(max_concurrent)
        async def process_one(req):
            async with semaphore:
                return await self.completion(**req)
        tasks = [process_one(req) for req in requests]
        return await asyncio.gather(*tasks, return_exceptions=True)

    def update_deployments(self, new_deployments: List[Deployment]) -> None:
        """更新部署列表"""
        self.deployments = new_deployments
        self._build_model_indices()
        if self.health_checker:
            self.health_checker.deployments = new_deployments

    # ------------------------------------------------------------------
    # Typed invoke / stream (高层 SDK 接口)
    # ------------------------------------------------------------------

    def _build_params(self, **kwargs) -> Dict[str, Any]:
        """从 kwargs 中滤除 None 值，构建传递给 completion 的参数 dict。"""
        return {k: v for k, v in kwargs.items() if v is not None}

    def _resolve_model_name(self, model: Optional[str] = None) -> str:
        if model:
            return model
        if not self.deployments:
            raise ValueError(
                "No model specified and no deployments configured. "
                "Use model='*' for unified routing across all deployments."
            )
        return self.deployments[0].model_name

    async def invoke(
        self,
        messages: List[Dict[str, Any]],
        *,
        tools: Optional[List[Dict[str, Any]]] = None,
        temperature: Optional[float] = None,
        top_p: Optional[float] = None,
        model: Optional[str] = None,
        max_tokens: Optional[int] = None,
        stop: Optional[Union[str, List[str]]] = None,
        output_parser: Optional[BaseOutputParser] = None,
        **kwargs,
    ) -> AssistantMessage:
        """发送 chat completion 请求，返回类型化响应。

        Args:
            messages: 消息列表 [{"role": "user", "content": "..."}]
            tools: 工具定义列表
            temperature: 采样温度
            top_p: 核采样参数
            model: 模型名（覆盖 deployment 配置）
            max_tokens: 最大生成 token 数
            stop: 停止序列
            output_parser: 可选输出解析器，用于解析 content
            **kwargs: 透传给底层 completion 的额外参数

        Returns:
            AssistantMessage: 类型化响应
        """
        params = self._build_params(
            tools=tools, temperature=temperature, top_p=top_p,
            max_tokens=max_tokens, stop=stop, **kwargs,
        )
        model_name = self._resolve_model_name(model)

        raw = await self.completion(
            model=model_name,
            messages=messages,
            **params,
        )
        msg = self._response_to_message(raw)

        if output_parser is not None and msg.content:
            try:
                parsed = await output_parser.parse(msg)
                if parsed is not None:
                    msg.content = json.dumps(parsed, ensure_ascii=False) if isinstance(parsed, dict) else str(parsed)
            except Exception:
                pass

        return msg

    async def stream(
        self,
        messages: List[Dict[str, Any]],
        *,
        tools: Optional[List[Dict[str, Any]]] = None,
        temperature: Optional[float] = None,
        top_p: Optional[float] = None,
        model: Optional[str] = None,
        max_tokens: Optional[int] = None,
        stop: Optional[Union[str, List[str]]] = None,
        **kwargs,
    ) -> AsyncIterator[AssistantMessageChunk]:
        """流式 chat completion，逐 chunk 返回 AssistantMessageChunk。

        参数同 invoke()。支持连接阶段的 retry/failover。
        """
        params = self._build_params(
            tools=tools, temperature=temperature, top_p=top_p,
            max_tokens=max_tokens, stop=stop, **kwargs,
        )
        model_name = self._resolve_model_name(model)

        request_id = RoutingEvent.new_request_id()
        total_attempts = self.num_retries + 1

        available = self._get_available_deployments(model_name)
        if not available:
            await self.event_bus.emit(RoutingEvent(
                event_type=RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED,
                request_id=request_id,
                model=model_name,
                error_message="No available deployments",
            ))
            raise NoDeploymentAvailable(f"No available deployment for model: {model_name}")

        await self.event_bus.emit(RoutingEvent(
            event_type=RoutingEventType.STREAM_STARTED,
            request_id=request_id,
            model=model_name,
            total_attempts=total_attempts,
        ))

        overall_start = time.time()
        errors = []

        for attempt in range(total_attempts):
            selected = await self.strategy.select_deployment(available, RoutingContext(
                model=model_name, messages=messages, kwargs=params,
            ))
            if selected is None:
                if available:
                    selected = available[0]
                else:
                    break
            try:
                actual_model = selected.model_name if model_name == MODEL_WILDCARD else model_name
                chunk_count = 0
                ttft = None
                stream_start = time.time()
                async for chunk in self.acompletion_stream(
                    model=actual_model,
                    messages=messages,
                    deployment=selected,
                    **params,
                ):
                    parsed = self._chunk_to_assistant_message_chunk(chunk)
                    if parsed is not None:
                        chunk_count += 1
                        if chunk_count == 1:
                            ttft = time.time() - stream_start
                        yield parsed

                await self.event_bus.emit(RoutingEvent(
                    event_type=RoutingEventType.STREAM_SUCCEEDED,
                    request_id=request_id,
                    model=model_name,
                    deployment_id=selected.id,
                    provider=selected.provider,
                    latency=time.time() - overall_start,
                    attempt=attempt + 1,
                    total_attempts=total_attempts,
                    chunk_count=chunk_count,
                    extra={"ttft": ttft} if ttft is not None else {},
                ))
                return
            except Exception as e:
                self.strategy.on_failure(selected, e)
                errors.append((selected.id, type(e).__name__, str(e)))

                if attempt < self.num_retries:
                    await self.event_bus.emit(RoutingEvent(
                        event_type=RoutingEventType.REQUEST_RETRIED,
                        request_id=request_id,
                        model=model_name,
                        deployment_id=selected.id,
                        provider=selected.provider,
                        attempt=attempt + 1,
                        total_attempts=total_attempts,
                        error_type=type(e).__name__,
                        error_message=str(e),
                    ))

                available = [d for d in available if d.id != selected.id]
                if not available:
                    break

        await self.event_bus.emit(RoutingEvent(
            event_type=RoutingEventType.ALL_DEPLOYMENTS_EXHAUSTED,
            request_id=request_id,
            model=model_name,
            latency=time.time() - overall_start,
            attempt=len(errors),
            total_attempts=total_attempts,
            error_type=errors[-1][1] if errors else None,
            error_message=errors[-1][2] if errors else None,
        ))
        raise RouterError(f"All deployments failed for stream after {total_attempts} attempts: {errors}")

    # ------------------------------------------------------------------
    # Internal: type conversion helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _response_to_message(raw: Dict[str, Any]) -> AssistantMessage:
        """将 OpenAI 格式的 dict 响应转换为 AssistantMessage。"""
        choices = raw.get("choices", [])
        if not choices:
            return AssistantMessage(content="")

        choice = choices[0]
        message = choice.get("message", {})

        content = message.get("content") or ""
        finish_reason = choice.get("finish_reason") or "stop"
        reasoning_content = message.get("reasoning_content")

        # Parse tool_calls
        tool_calls = None
        raw_tool_calls = message.get("tool_calls")
        if raw_tool_calls:
            tool_calls = []
            for idx, tc in enumerate(raw_tool_calls):
                func = tc.get("function", {})
                tool_calls.append(ToolCall(
                    id=tc.get("id", ""),
                    type=tc.get("type", "function"),
                    name=func.get("name", ""),
                    arguments=func.get("arguments", ""),
                    index=tc.get("index", idx),
                ))

        # Parse usage
        usage_metadata = None
        usage = raw.get("usage")
        if usage:
            prompt_tokens = usage.get("prompt_tokens") or 0
            completion_tokens = usage.get("completion_tokens") or 0
            total_tokens = usage.get("total_tokens") or 0

            cache_tokens = 0
            prompt_tokens_details = usage.get("prompt_tokens_details")
            if prompt_tokens_details:
                cache_tokens = prompt_tokens_details.get("cached_tokens") or 0

            usage_metadata = UsageMetadata(
                input_tokens=prompt_tokens,
                output_tokens=completion_tokens,
                total_tokens=total_tokens,
                cache_tokens=cache_tokens,
            )

        return AssistantMessage(
            content=content,
            tool_calls=tool_calls,
            usage_metadata=usage_metadata,
            finish_reason=finish_reason,
            reasoning_content=reasoning_content,
        )

    @staticmethod
    def _chunk_to_assistant_message_chunk(
        chunk: Dict[str, Any],
    ) -> Optional[AssistantMessageChunk]:
        """将 OpenAI 格式的 streaming chunk dict 转换为 AssistantMessageChunk。"""
        choices = chunk.get("choices")
        if not choices:
            return None

        choice = choices[0]
        delta = choice.get("delta", {})
        finish_reason = choice.get("finish_reason")

        content = delta.get("content") or ""
        reasoning_content = delta.get("reasoning_content")
        raw_tool_calls = delta.get("tool_calls")

        if not content and not finish_reason and not raw_tool_calls and not reasoning_content:
            return None

        tool_calls = None
        if raw_tool_calls:
            tool_calls = []
            for tc in raw_tool_calls:
                func = tc.get("function", {})
                tool_calls.append(ToolCall(
                    id=tc.get("id", ""),
                    type=tc.get("type", "function"),
                    name=func.get("name", ""),
                    arguments=func.get("arguments", ""),
                    index=tc.get("index"),
                ))

        return AssistantMessageChunk(
            content=content,
            reasoning_content=reasoning_content,
            tool_calls=tool_calls,
            finish_reason=finish_reason,
        )

    def get_stats(self) -> Dict[str, Any]:
        """获取统计信息"""
        return {
            "total_deployments": len(self.deployments),
            "model_list": self.get_model_list(),
            "deployment_status": {
                dep.id: self.state.deployment_status.get(dep.id, DeploymentStatus.HEALTHY).value
                for dep in self.deployments
            },
            "consecutive_failures": dict(self.state.consecutive_failures),
            "latency_stats": {
                dep.id: {
                    "avg_latency": self.state.get_average_latency(dep.id),
                    "total_tokens": self.state.total_tokens.get(dep.id, 0),
                    "total_requests": self.state.total_requests.get(dep.id, 0),
                }
                for dep in self.deployments
            }
        }
