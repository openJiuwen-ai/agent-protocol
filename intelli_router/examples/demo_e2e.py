"""
intelli_router 端到端 Demo

演示场景：
  1. OpenAI 真实调用
  2. Anthropic 真实调用
  3. Gemini 真实调用
  4. Failover — dead endpoint 自动切换到 healthy endpoint
  5. All dead — 所有 endpoint 不可用，抛出 RouterError

使用方式：
  export OPENAI_API_KEY="sk-..."
  export ANTHROPIC_API_KEY="sk-ant-..."
  export GEMINI_API_KEY="AIza..."

  python examples/demo_e2e.py

没有配置 key 的场景会自动 skip。
"""

import os
import asyncio
import time

from intelli_router.core.deployment import Deployment
from intelli_router.router.reliable_router import ReliableRouter
from intelli_router.utils.exceptions import RouterError

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------

MESSAGE = [{"role": "user", "content": "回复三个中文字符：你好世界"}]

OPENAI_KEY = os.environ.get("OPENAI_API_KEY", "")
ANTHROPIC_KEY = os.environ.get("ANTHROPIC_API_KEY", "")
GEMINI_KEY = os.environ.get("GEMINI_API_KEY", "")

DEAD_BASE = "http://localhost:19999"  # 没有服务监听的端口

MODELS = {
    "openai": "gpt-4o-mini",
    "anthropic": "claude-3-haiku-20240307",
    "gemini": "gemini-1.5-flash",
}


# ---------------------------------------------------------------------------
# 场景
# ---------------------------------------------------------------------------


async def scene_openai():
    """场景 1：OpenAI 真实调用"""
    print("\n=== scene: OpenAI real ===")
    dep = Deployment(
        id="openai-1",
        model_name=MODELS["openai"],
        api_key=OPENAI_KEY,
        api_base="https://api.openai.com",
        provider="openai",
    )
    router = ReliableRouter(
        deployments=[dep],
        num_retries=0,
        strategy="simple-shuffle",
    )
    t0 = time.time()
    resp = await router.completion(MODELS["openai"], MESSAGE)
    cost = time.time() - t0
    print(f"  response: {resp['choices'][0]['message']['content']}")
    print(f"  latency:  {cost:.2f}s")
    print(f"  usage:    {resp['usage']}")
    await router.close()


async def scene_anthropic():
    """场景 2：Anthropic 真实调用"""
    print("\n=== scene: Anthropic real ===")
    dep = Deployment(
        id="anthropic-1",
        model_name=MODELS["anthropic"],
        api_key=ANTHROPIC_KEY,
        api_base="https://api.anthropic.com",
        provider="anthropic",
    )
    router = ReliableRouter(
        deployments=[dep],
        num_retries=0,
        strategy="simple-shuffle",
    )
    t0 = time.time()
    resp = await router.completion(MODELS["anthropic"], MESSAGE)
    cost = time.time() - t0
    print(f"  response: {resp['choices'][0]['message']['content']}")
    print(f"  latency:  {cost:.2f}s")
    print(f"  usage:    {resp['usage']}")
    await router.close()


async def scene_gemini():
    """场景 3：Gemini 真实调用"""
    print("\n=== scene: Gemini real ===")
    dep = Deployment(
        id="gemini-1",
        model_name=MODELS["gemini"],
        api_key=GEMINI_KEY,
        api_base="https://generativelanguage.googleapis.com",
        provider="google-gemini",
    )
    router = ReliableRouter(
        deployments=[dep],
        num_retries=0,
        strategy="simple-shuffle",
    )
    t0 = time.time()
    resp = await router.completion(MODELS["gemini"], MESSAGE)
    cost = time.time() - t0
    print(f"  response: {resp['choices'][0]['message']['content']}")
    print(f"  latency:  {cost:.2f}s")
    print(f"  usage:    {resp['usage']}")
    await router.close()


async def scene_failover():
    """场景 4：Failover — dead endpoint 自动切换到 healthy

    两个 deployment 指向同一模型 "gpt-4o-mini"：
      - openai-dead:  api_base 指向不存在的 localhost:19999
      - openai-real:  指向真实 OpenAI

    ReliableRouter 首次选到 dead 后 AdaptiveStrategy 标记 cooldown，
    重试选到 real，验证 fallback 成功。
    """
    print("\n=== scene: Failover ===")
    dead = Deployment(
        id="openai-dead",
        model_name=MODELS["openai"],
        api_key="fake-key",
        api_base=DEAD_BASE,
        provider="openai",
    )
    real = Deployment(
        id="openai-real",
        model_name=MODELS["openai"],
        api_key=OPENAI_KEY,
        api_base="https://api.openai.com",
        provider="openai",
    )
    router = ReliableRouter(
        deployments=[dead, real],
        num_retries=1,
        strategy="adaptive",  # AdaptiveStrategy 在 on_failure 时标记 cooldown
    )
    t0 = time.time()
    resp = await router.completion(MODELS["openai"], MESSAGE)
    cost = time.time() - t0

    print(f"  primary deployment (dead):    {dead.id}  → {DEAD_BASE}")
    print(f"  fallback deployment (healthy): {real.id}  → https://api.openai.com")
    print(f"  response: {resp['choices'][0]['message']['content']}")
    print(f"  latency:  {cost:.2f}s")

    stats = router.get_stats()
    print(f"  after call status:")
    for dep_id, status in stats["deployment_status"].items():
        print(f"    {dep_id}: {status}")

    await router.close()


async def scene_all_dead():
    """场景 5：All dead — 所有 endpoint 不可用

    两个 deployment 都指向 localhost:19999，预期抛出 RouterError。
    """
    print("\n=== scene: All dead ===")
    dead1 = Deployment(
        id="dead-1",
        model_name=MODELS["openai"],
        api_key="fake-key",
        api_base=DEAD_BASE,
        provider="openai",
    )
    dead2 = Deployment(
        id="dead-2",
        model_name=MODELS["openai"],
        api_key="fake-key",
        api_base=DEAD_BASE,
        provider="openai",
    )
    router = ReliableRouter(
        deployments=[dead1, dead2],
        num_retries=1,
        strategy="adaptive",
    )
    try:
        await router.completion(MODELS["openai"], MESSAGE)
        print("  ERROR: expected RouterError but got success")
    except RouterError as e:
        print(f"  caught expected RouterError ✓")
        print(f"  error: {e}")
    await router.close()


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


async def main():
    print("intelli_router 端到端 Demo")
    print("=" * 50)

    if OPENAI_KEY:
        await scene_openai()
        await scene_failover()
        await scene_all_dead()
    else:
        print("\n[skip] OPENAI_API_KEY 未设置，跳过 OpenAI / failover / all-dead 场景")

    if ANTHROPIC_KEY:
        await scene_anthropic()
    else:
        print("\n[skip] ANTHROPIC_API_KEY 未设置，跳过 Anthropic 场景")

    if GEMINI_KEY:
        await scene_gemini()
    else:
        print("\n[skip] GEMINI_API_KEY 未设置，跳过 Gemini 场景")

    print("\n=== done ===")


if __name__ == "__main__":
    asyncio.run(main())
