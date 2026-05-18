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
    assert "token-aware" in args
    assert "rate-limit-aware" in args
