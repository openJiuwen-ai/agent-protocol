"""
Demo Adaptive Strategy - 自适应策略演示

演示场景:
1. Token感知策略 - Token耗尽无缝切换
2. RPM限流策略 - RPM限流负载均衡
3. 自适应策略 - 多级决策树
"""
import sys
import os
# 添加包路径以支持直接运行
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import asyncio
import time
from dataclasses import dataclass, field

from sdk_llm.strategy.token_aware import TokenAwareStrategy
from sdk_llm.strategy.rate_limit_aware import RateLimitAwareStrategy
from sdk_llm.strategy.adaptive import AdaptiveStrategy
from sdk_llm.core.state import LocalRouterState, TokenUsage, RPMTracker, LatencyRecord
from sdk_llm.core.deployment import Deployment, DeploymentStatus
from sdk_llm.core.context import RoutingContext


def create_demo_state() -> LocalRouterState:
    """创建演示状态"""
    state = LocalRouterState()

    # 部署1: 健康, Token剩余5000, RPM剩余30, 平均延迟0.5秒
    state.health_state["dep1"] = True
    state.token_usage["dep1"] = TokenUsage(used=5000, limit=10000)
    tracker1 = RPMTracker(rpm_limit=60)
    for _ in range(30):
        tracker1.add_request()
    state.rpm_tracker["dep1"] = tracker1
    state.latencies["dep1"] = [
        LatencyRecord(latency=0.5, tokens=100, normalized=0.005, timestamp=time.time())
    ]

    # 部署2: 健康, Token剩余2000, RPM剩余50, 平均延迟0.2秒
    state.health_state["dep2"] = True
    state.token_usage["dep2"] = TokenUsage(used=8000, limit=10000)
    tracker2 = RPMTracker(rpm_limit=60)
    for _ in range(10):
        tracker2.add_request()
    state.rpm_tracker["dep2"] = tracker2
    state.latencies["dep2"] = [
        LatencyRecord(latency=0.2, tokens=100, normalized=0.002, timestamp=time.time())
    ]

    # 部署3: 不健康
    state.health_state["dep3"] = False
    state.deployment_status["dep3"] = DeploymentStatus.FAILED

    return state


def create_demo_deployments() -> list[Deployment]:
    """创建演示部署"""
    return [
        Deployment(
            model_name="gpt-4",
            api_key="sk-key1",
            api_base="http://localhost:8001/v1",
            id="dep1",
            tpm=10000,
            rpm=60
        ),
        Deployment(
            model_name="gpt-4",
            api_key="sk-key2",
            api_base="http://localhost:8002/v1",
            id="dep2",
            tpm=10000,
            rpm=60
        ),
        Deployment(
            model_name="gpt-4",
            api_key="sk-key3",
            api_base="http://localhost:8003/v1",
            id="dep3",
            tpm=10000,
            rpm=60
        ),
    ]


async def demo_token_aware():
    """演示Token感知策略"""
    print("\n=== Token感知策略演示 ===")

    state = create_demo_state()
    deployments = create_demo_deployments()
    strategy = TokenAwareStrategy(state=state, token_threshold=1000, exploration_ratio=0.0)

    context = RoutingContext(
        model="gpt-4",
        messages=[{"role": "user", "content": "Hello!"}]
    )

    # 选择部署
    selected = await strategy.select_deployment(deployments, context)
    print(f"选择的部署: {selected.id}")
    print(f"  - Token剩余: {state.get_token_remaining(selected.id)}")
    print(f"  - Token使用率: {state.get_token_utilization(selected.id):.2%}")

    # 模拟请求成功
    strategy.on_success(selected, latency=0.5, tokens=100)
    print(f"\n请求成功后:")
    print(f"  - Token已使用: {state.token_usage[selected.id].used}")
    print(f"  - Token剩余: {state.token_usage[selected.id].remaining}")


async def demo_rate_limit_aware():
    """演示RPM限流策略"""
    print("\n=== RPM限流策略演示 ===")

    state = create_demo_state()
    deployments = create_demo_deployments()
    strategy = RateLimitAwareStrategy(state=state, rpm_threshold=10, exploration_ratio=0.0)

    context = RoutingContext(
        model="gpt-4",
        messages=[{"role": "user", "content": "Hello!"}]
    )

    # 选择部署
    selected = await strategy.select_deployment(deployments, context)
    print(f"选择的部署: {selected.id}")
    print(f"  - RPM剩余: {state.get_rpm_remaining(selected.id)}")
    print(f"  - RPM使用率: {state.get_rpm_utilization(selected.id):.2%}")

    # 模拟请求成功
    strategy.on_success(selected, latency=0.3, tokens=50)
    print(f"\n请求成功后:")
    print(f"  - 当前RPM: {state.rpm_tracker[selected.id].current_rpm}")
    print(f"  - RPM剩余: {state.rpm_tracker[selected.id].remaining}")


async def demo_adaptive():
    """演示自适应策略"""
    print("\n=== 自适应策略演示 ===")

    state = create_demo_state()
    deployments = create_demo_deployments()
    strategy = AdaptiveStrategy(
        state=state,
        token_threshold=1000,
        rpm_threshold=10,
        exploration_ratio=0.0,
        w_health=1.0,
        w_token=0.5,
        w_rpm=0.3,
        w_latency=0.2
    )

    context = RoutingContext(
        model="gpt-4",
        messages=[{"role": "user", "content": "Hello!"}]
    )

    # 计算各部署得分
    print("\n部署评分:")
    now = time.time()
    for d in deployments:
        if d.is_available(now):
            score = strategy._calculate_score(d, now)
            print(f"  {d.id}: 得分={score:.3f}")
            print(f"    - 健康: {state.health_state.get(d.id, False)}")
            print(f"    - Token剩余: {state.get_token_remaining(d.id)}")
            print(f"    - RPM剩余: {state.get_rpm_remaining(d.id)}")
            print(f"    - 平均延迟: {state.get_average_latency(d.id):.3f}")

    # 选择部署
    selected = await strategy.select_deployment(deployments, context)
    print(f"\n选择的部署: {selected.id}")

    # 模拟请求成功
    strategy.on_success(selected, latency=0.4, tokens=80)
    print(f"\n请求成功后:")
    print(f"  - Token已使用: {state.token_usage[selected.id].used}")
    print(f"  - 当前RPM: {state.rpm_tracker[selected.id].current_rpm}")
    print(f"  - 总请求数: {state.total_requests[selected.id]}")


async def demo_token_exhaustion():
    """演示Token耗尽场景"""
    print("\n=== Token耗尽无缝切换演示 ===")

    state = create_demo_state()
    deployments = create_demo_deployments()

    # 设置所有部署Token都快耗尽
    state.token_usage["dep1"] = TokenUsage(used=9900, limit=10000)  # 剩余100
    state.token_usage["dep2"] = TokenUsage(used=9950, limit=10000)  # 剩余50
    state.token_usage["dep3"] = TokenUsage(used=9990, limit=10000)  # 剩余10

    strategy = TokenAwareStrategy(state=state, token_threshold=1000, exploration_ratio=0.0)
    context = RoutingContext(
        model="gpt-4",
        messages=[{"role": "user", "content": "Hello!"}]
    )

    # 选择部署 (应选择剩余最多的dep1)
    selected = await strategy.select_deployment(deployments, context)
    print(f"Token快耗尽时选择的部署: {selected.id}")
    print(f"  - Token剩余: {state.token_usage[selected.id].remaining}")

    # 模拟多次请求直到耗尽
    for i in range(3):
        strategy.on_success(selected, latency=0.5, tokens=50)
        print(f"\n第{i+1}次请求后:")
        print(f"  - Token剩余: {state.token_usage[selected.id].remaining}")

        if state.token_usage[selected.id].remaining <= 0:
            print("  - Token已耗尽，切换到下一个部署")
            # 在实际场景中，会重新选择部署
            available = [d for d in deployments if d.id != selected.id]
            if available:
                selected = available[0]
                print(f"  - 切换到: {selected.id}")


async def main():
    """主演示函数"""
    print("=" * 60)
    print("自适应策略演示")
    print("=" * 60)

    await demo_token_aware()
    await demo_rate_limit_aware()
    await demo_adaptive()
    await demo_token_exhaustion()

    print("\n" + "=" * 60)
    print("演示完成")
    print("=" * 60)


if __name__ == "__main__":
    asyncio.run(main())
