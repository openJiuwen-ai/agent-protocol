"""Tests for intelli_router public API exports."""
from intelli_router import (
    ReliableRouter, BaseRouter,
    Deployment, DeploymentStatus,
    RoutingStrategy,
    StrategyType,
    TokenUsage,
)


def test_all_public_exports_importable():
    assert ReliableRouter is not None
    assert BaseRouter is not None
    assert Deployment is not None
    assert DeploymentStatus is not None
    assert RoutingStrategy is not None
    assert StrategyType is not None
    assert TokenUsage is not None


def test_strategy_type_is_literal():
    from typing import get_args
    args = get_args(StrategyType)
    assert "simple-shuffle" in args
    assert "adaptive" in args
    assert "lowest-latency" in args
    assert "tag-based" in args
    assert "ordered-failover" in args
    assert "tag-filtered" in args
    assert "token-aware" in args
    assert "rate-limit-aware" in args


# -------- __version__ (PR !358 review) --------

def test_dunder_version_is_exposed():
    """PR !358 二轮意见 B：包暴露 __version__，使用者可运行期确认版本。

    取安装元数据（发行包名 "intelli-router"），未安装时回退 pyproject
    声明的版本。断言保持宽松（非空 str 且以 "0." 开头），避免 bump
    版本时测试随之烂掉。
    """
    import intelli_router

    assert isinstance(intelli_router.__version__, str)
    assert intelli_router.__version__
    assert intelli_router.__version__.startswith("0.")


def test_dunder_version_in_all():
    """__version__ 应列入 __all__，属于公开接口的一部分。"""
    import intelli_router

    assert "__version__" in intelli_router.__all__
