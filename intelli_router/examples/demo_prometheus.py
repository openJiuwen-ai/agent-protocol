"""
IntelliRouter Prometheus 指标导出 Demo

演示 Prometheus 指标暴露。使用模拟请求，无需真实 API Key。
启动后可通过 curl http://localhost:9090/metrics 查看指标。

使用方式：
  pip install prometheus-client
  python examples/demo_prometheus.py

验证（默认端口 9091，可通过 METRICS_PORT 环境变量修改）：
  curl http://localhost:9091/metrics | grep intelli_router
"""

import asyncio
import os
import random
import signal
import sys

from intelli_router import (
    ReliableRouter,
    Deployment,
    EventBus,
    MetricsCollector,
    LoggingHook,
)
from intelli_router.router.base_router import BaseRouter


# ---------------------------------------------------------------------------
# 模拟 Deployments
# ---------------------------------------------------------------------------
DEPLOYMENTS = [
    Deployment(
        id="azure-east",
        model_name="gpt-4",
        provider="openai",
        api_key="fake-key-1",
        api_base="http://localhost:19999",
    ),
    Deployment(
        id="openai-main",
        model_name="gpt-4",
        provider="openai",
        api_key="fake-key-2",
        api_base="http://localhost:19998",
    ),
    Deployment(
        id="anthropic-1",
        model_name="claude-3",
        provider="anthropic",
        api_key="fake-key-3",
        api_base="http://localhost:19997",
    ),
]


# ---------------------------------------------------------------------------
# Mock：模拟不同 deployment 的延迟和失败
# ---------------------------------------------------------------------------
async def mock_completion(self, model, messages, deployment=None, **kwargs):
    if deployment is None:
        raise RuntimeError("No deployment")

    # 不同 deployment 模拟不同特性
    profiles = {
        "azure-east": {"latency": (0.3, 0.8), "fail_rate": 0.05},
        "openai-main": {"latency": (0.5, 1.5), "fail_rate": 0.10},
        "anthropic-1": {"latency": (0.8, 2.0), "fail_rate": 0.15},
    }
    profile = profiles.get(deployment.id, {"latency": (0.5, 1.0), "fail_rate": 0.1})

    latency = random.uniform(*profile["latency"])
    await asyncio.sleep(latency)

    if random.random() < profile["fail_rate"]:
        error_types = [
            RuntimeError("Connection timeout"),
            RuntimeError("Rate limit exceeded"),
            RuntimeError("Internal server error"),
        ]
        raise random.choice(error_types)

    tokens_prompt = random.randint(50, 300)
    tokens_completion = random.randint(20, 200)
    return {
        "choices": [{"message": {"content": "Hello!"}, "finish_reason": "stop"}],
        "usage": {
            "prompt_tokens": tokens_prompt,
            "completion_tokens": tokens_completion,
            "total_tokens": tokens_prompt + tokens_completion,
        },
    }


async def main():
    print("=" * 60)
    print("  IntelliRouter Prometheus Demo")
    print("=" * 60)
    print()
    port = int(os.environ.get("METRICS_PORT", "9091"))
    print(f"  指标端口: http://localhost:{port}/metrics")
    print("  模拟 3 个 deployment (gpt-4 x2, claude-3 x1)")
    print("  按 Ctrl+C 退出")
    print()

    # 设置 EventBus
    bus = EventBus()

    # Prometheus 指标采集
    metrics = MetricsCollector(enable_prometheus=True)
    metrics.expose_prometheus(port=port)
    bus.register(metrics)

    # 同时开启文本日志（方便观察）
    bus.register(LoggingHook(format="text"))

    # 创建 Router
    router = ReliableRouter(
        deployments=DEPLOYMENTS,
        strategy="simple-shuffle",
        num_retries=2,
        event_bus=bus,
    )

    # Monkey-patch
    original_completion = BaseRouter.completion
    BaseRouter.completion = mock_completion

    print("  Prometheus HTTP server 已启动")
    print("  验证: curl http://localhost:9090/metrics | grep intelli_router")
    print()
    print("  开始模拟请求...\n")

    try:
        request_num = 0
        while True:
            request_num += 1

            # 随机选择模型
            model = random.choice(["gpt-4", "gpt-4", "gpt-4", "claude-3"])

            try:
                await router.completion(model, [{"role": "user", "content": f"Request #{request_num}"}])
            except Exception:
                pass

            # 模拟流量间隔
            await asyncio.sleep(random.uniform(0.2, 0.8))

            # 每 20 个请求打印一次汇总
            if request_num % 20 == 0:
                stats = metrics.get_stats()
                print(f"\n  --- 已完成 {request_num} 个请求 ---")
                print(f"  成功: {stats['successful']}  "
                      f"失败: {stats['failed']}  "
                      f"重试: {stats['retries']}  "
                      f"Token: {stats['tokens']['total']}")
                if stats['latency']['count'] > 0:
                    print(f"  延迟: avg={stats['latency']['avg']:.2f}s  "
                          f"min={stats['latency']['min']:.2f}s  "
                          f"max={stats['latency']['max']:.2f}s")
                print()

    except KeyboardInterrupt:
        print("\n\n  正在停止...")
    finally:
        BaseRouter.completion = original_completion

        # 打印最终统计
        stats = metrics.get_stats()
        print("\n  ========== 最终统计 ==========")
        print(f"  总请求: {stats['total_requests']}")
        print(f"  成功: {stats['successful']}")
        print(f"  失败: {stats['failed']}")
        print(f"  重试: {stats['retries']}")
        print(f"  耗尽: {stats['exhausted']}")
        if stats['latency']['count'] > 0:
            print(f"  平均延迟: {stats['latency']['avg']:.2f}s")
        print(f"  总 Token: {stats['tokens']['total']}")
        if stats['errors_by_type']:
            print(f"  错误分布: {dict(stats['errors_by_type'])}")
        print()
        print("  Prometheus 指标示例 (最终值):")
        print("    intelli_router_requests_total{status='success'} =", stats['successful'])
        print("    intelli_router_retries_total =", stats['retries'])
        print("    intelli_router_tokens_total =", stats['tokens']['total'])
        print()


if __name__ == "__main__":
    asyncio.run(main())
