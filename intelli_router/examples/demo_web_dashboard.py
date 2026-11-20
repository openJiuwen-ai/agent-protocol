"""
IntelliRouter Web 指标看板 Demo

启动一个轻量 Web 服务，在浏览器中实时展示路由指标。
使用模拟请求，无需真实 API Key。

使用方式：
  python examples/demo_web_dashboard.py

  然后浏览器打开 http://localhost:8080
"""

import asyncio
import os
import random

from intelli_router import (
    ReliableRouter,
    Deployment,
    EventBus,
    MetricsCollector,
    MetricsWebServer,
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
    Deployment(
        id="deepseek-1",
        model_name="deepseek-chat",
        provider="deepseek",
        api_key="fake-key-4",
        api_base="http://localhost:19996",
    ),
]


# ---------------------------------------------------------------------------
# Mock
# ---------------------------------------------------------------------------
async def mock_completion(self, model, messages, deployment=None, **kwargs):
    if deployment is None:
        raise RuntimeError("No deployment")

    profiles = {
        "azure-east": {"latency": (0.2, 0.6), "fail_rate": 0.03},
        "openai-main": {"latency": (0.4, 1.2), "fail_rate": 0.08},
        "anthropic-1": {"latency": (0.5, 1.5), "fail_rate": 0.12},
        "deepseek-1": {"latency": (0.3, 0.9), "fail_rate": 0.06},
    }
    profile = profiles.get(deployment.id, {"latency": (0.5, 1.0), "fail_rate": 0.1})

    latency = random.uniform(*profile["latency"])
    await asyncio.sleep(latency)

    if random.random() < profile["fail_rate"]:
        errors = ["TimeoutError", "RateLimitError", "ConnectionError", "ServerError"]
        raise RuntimeError(random.choice(errors))

    tokens_prompt = random.randint(50, 300)
    tokens_completion = random.randint(20, 200)
    return {
        "choices": [{"message": {"content": "OK"}, "finish_reason": "stop"}],
        "usage": {
            "prompt_tokens": tokens_prompt,
            "completion_tokens": tokens_completion,
            "total_tokens": tokens_prompt + tokens_completion,
        },
    }


async def main():
    port = int(os.environ.get("WEB_PORT", "8080"))

    print("=" * 60)
    print("  IntelliRouter Web Dashboard Demo")
    print("=" * 60)
    print()
    print(f"  Web 看板: http://localhost:{port}")
    print("  模拟 4 个 deployment，持续发送请求")
    print("  按 Ctrl+C 退出")
    print()

    # 设置
    bus = EventBus()
    metrics = MetricsCollector()
    bus.register(metrics)

    server = MetricsWebServer(metrics, port=port)
    server.start()

    router = ReliableRouter(
        deployments=DEPLOYMENTS,
        strategy="simple-shuffle",
        num_retries=2,
        event_bus=bus,
    )

    # Monkey-patch
    original = BaseRouter.completion
    BaseRouter.completion = mock_completion

    print(f"  服务已启动，请在浏览器中打开: http://localhost:{port}")
    print()

    try:
        request_num = 0
        while True:
            request_num += 1
            model = random.choice(["gpt-4", "gpt-4", "claude-3", "deepseek-chat"])
            try:
                await router.completion(model, [{"role": "user", "content": f"req-{request_num}"}])
            except Exception:
                pass
            await asyncio.sleep(random.uniform(0.1, 0.4))

            if request_num % 50 == 0:
                stats = metrics.get_stats()
                print(f"  [{request_num} reqs] "
                      f"success={stats['successful']} "
                      f"failed={stats['failed']} "
                      f"retries={stats['retries']} "
                      f"tokens={stats['tokens']['total']}")

    except KeyboardInterrupt:
        print("\n  停止中...")
    finally:
        server.stop()
        BaseRouter.completion = original
        print("  已退出。")


if __name__ == "__main__":
    asyncio.run(main())
