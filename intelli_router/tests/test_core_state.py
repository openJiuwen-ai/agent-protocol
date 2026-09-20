"""Tests for intelli_router.core.state."""
import time
import threading
import pytest
from intelli_router.core.state import (
    LatencyRecord, TokenUsage, RPMRecord, RPMTracker, LocalRouterState,
    MAX_LATENCY_HISTORY,
)
from intelli_router.core.deployment import DeploymentStatus


# -------- LatencyRecord --------

def test_latency_record_defaults():
    rec = LatencyRecord(latency=0.5, tokens=100, normalized=0.005)
    assert rec.latency == 0.5
    assert rec.tokens == 100
    assert rec.normalized == 0.005
    assert rec.timestamp > 0


# -------- TokenUsage --------

def test_token_usage_remaining():
    tu = TokenUsage(used=100, limit=1000)
    assert tu.remaining == 900


def test_token_usage_remaining_negative():
    tu = TokenUsage(used=1000, limit=100)
    assert tu.remaining == 0


def test_token_usage_utilization_ratio():
    tu = TokenUsage(used=250, limit=1000)
    assert tu.utilization_ratio == 0.25


def test_token_usage_utilization_ratio_zero_limit():
    tu = TokenUsage(used=100, limit=0)
    assert tu.utilization_ratio == 0.0


# -------- RPMRecord --------

def test_rpm_record_defaults():
    rec = RPMRecord()
    assert rec.timestamp > 0


# -------- RPMTracker --------

def test_rpm_tracker_defaults():
    tr = RPMTracker()
    assert tr.rpm_limit == 0
    assert tr.requests == []


def test_rpm_tracker_add_request():
    tr = RPMTracker(rpm_limit=100)
    tr.add_request()
    assert len(tr.requests) == 1
    assert tr.current_rpm == 1


def test_rpm_tracker_remaining():
    tr = RPMTracker(rpm_limit=100)
    tr.add_request()
    assert tr.remaining == 99


def test_rpm_tracker_remaining_zero_limit():
    tr = RPMTracker(rpm_limit=0)
    tr.add_request()
    assert tr.remaining == 0  # max(0, 0 - 1) == 0


def test_rpm_tracker_utilization_ratio():
    tr = RPMTracker(rpm_limit=100)
    tr.add_request()
    assert 0.0 < tr.utilization_ratio <= 0.02  # 1/100


def test_rpm_tracker_utilization_ratio_zero_limit():
    tr = RPMTracker(rpm_limit=0)
    tr.add_request()
    assert tr.utilization_ratio == 0.0


def test_rpm_cleanup_old_records():
    """Records older than 60s are cleaned up on add_request."""
    tr = RPMTracker(rpm_limit=100)
    old = RPMRecord(timestamp=time.time() - 120)
    recent = RPMRecord()
    tr.requests = [old, recent]
    assert tr.current_rpm == 1  # old is excluded from count
    tr.add_request()
    assert tr.current_rpm == 2  # old was excluded, recent + new = 2


# -------- LocalRouterState --------

def test_post_init_creates_lock(router_state):
    assert router_state._lock is not None
    assert isinstance(router_state.lock, type(router_state._lock))
    # Ensure it's the same lock
    assert router_state.lock is router_state._lock


def test_reset_deployment(router_state):
    dep_id = "dep1"
    router_state.deployment_status[dep_id] = DeploymentStatus.COOLDOWN
    router_state.cooldown_until[dep_id] = time.time() + 3600
    router_state.consecutive_failures[dep_id] = 5

    router_state.reset_deployment(dep_id)
    assert router_state.deployment_status[dep_id] == DeploymentStatus.HEALTHY
    assert router_state.cooldown_until[dep_id] is None
    # consecutive_failures is NOT reset by reset_deployment
    assert router_state.consecutive_failures[dep_id] == 5


def test_update_health_true(router_state):
    dep_id = "dep1"
    router_state.deployment_status[dep_id] = DeploymentStatus.COOLDOWN
    router_state.cooldown_until[dep_id] = time.time() + 3600
    router_state.health_state[dep_id] = False

    router_state.update_health(dep_id, True)
    assert router_state.health_state[dep_id] is True
    assert router_state.deployment_status[dep_id] == DeploymentStatus.HEALTHY
    assert router_state.cooldown_until[dep_id] is None


def test_update_health_false(router_state):
    dep_id = "dep1"
    router_state.deployment_status[dep_id] = DeploymentStatus.HEALTHY

    router_state.update_health(dep_id, False)
    assert router_state.health_state[dep_id] is False
    # update_health False should NOT change status or cooldown
    assert router_state.deployment_status[dep_id] == DeploymentStatus.HEALTHY


def test_on_success_basic(router_state):
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.5, tokens=100)

    assert router_state.consecutive_failures[dep_id] == 0
    assert router_state.deployment_status[dep_id] == DeploymentStatus.HEALTHY
    assert router_state.cooldown_until[dep_id] is None
    assert len(router_state.latencies[dep_id]) == 1
    assert router_state.latencies[dep_id][0].latency == 0.5
    assert router_state.latencies[dep_id][0].normalized == 0.5 / 100
    assert router_state.total_tokens[dep_id] == 100
    assert router_state.total_requests[dep_id] == 1
    assert router_state.health_state[dep_id] is True
    assert router_state.token_usage[dep_id].used == 100
    assert router_state.rpm_tracker[dep_id].current_rpm >= 1


def test_on_success_zero_tokens(router_state):
    """tokens=0 should not cause division by zero in normalized latency."""
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.5, tokens=0)
    assert router_state.latencies[dep_id][0].normalized == 0.5  # max(0,1) = 1


def test_on_success_recovers_cooldown(router_state):
    dep_id = "dep1"
    router_state.deployment_status[dep_id] = DeploymentStatus.COOLDOWN
    router_state.cooldown_until[dep_id] = time.time() + 3600

    router_state.on_success(dep_id, latency=0.1, tokens=10)
    assert router_state.deployment_status[dep_id] == DeploymentStatus.HEALTHY
    assert router_state.cooldown_until[dep_id] is None


def test_on_success_limits_history(router_state):
    """Should keep at most MAX_LATENCY_HISTORY records."""
    dep_id = "dep1"
    for i in range(MAX_LATENCY_HISTORY + 1):
        router_state.on_success(dep_id, latency=0.1, tokens=10)
    assert len(router_state.latencies[dep_id]) == MAX_LATENCY_HISTORY


def test_on_failure_basic(router_state):
    dep_id = "dep1"
    router_state.on_failure(dep_id, ValueError("fail"))
    assert router_state.consecutive_failures[dep_id] == 1
    assert router_state.deployment_status[dep_id] == DeploymentStatus.COOLDOWN
    assert router_state.cooldown_until[dep_id] > time.time()
    assert router_state.health_state[dep_id] is False


def test_on_failure_exponential_backoff(router_state):
    dep_id = "dep1"
    for i in range(3):
        router_state.on_failure(dep_id, ValueError(f"fail_{i}"))

    assert router_state.consecutive_failures[dep_id] == 3
    # cooldown should be at least 60*3 seconds from now
    expected_min = time.time() + 60 * 3 - 1  # -1 for timing tolerance
    assert router_state.cooldown_until[dep_id] >= expected_min


def test_get_average_latency_with_records(router_state):
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.5, tokens=100)  # normalized: 0.005
    router_state.on_success(dep_id, latency=0.3, tokens=100)  # normalized: 0.003
    avg = router_state.get_average_latency(dep_id)
    assert avg == pytest.approx(0.004, rel=1e-3)


def test_get_average_latency_no_records(router_state):
    avg = router_state.get_average_latency("nonexistent")
    assert avg == float('inf')


def test_get_available_deployments_healthy(router_state):
    router_state.deployment_status["dep1"] = DeploymentStatus.HEALTHY
    router_state.deployment_status["dep2"] = DeploymentStatus.HEALTHY
    available = router_state.get_available_deployments(time.time())
    assert "dep1" in available
    assert "dep2" in available


def test_get_available_deployments_cooldown_miss(router_state):
    router_state.deployment_status["dep1"] = DeploymentStatus.COOLDOWN
    router_state.cooldown_until["dep1"] = time.time() + 3600
    available = router_state.get_available_deployments(time.time())
    assert "dep1" not in available


def test_get_available_deployments_cooldown_expired(router_state):
    router_state.deployment_status["dep1"] = DeploymentStatus.COOLDOWN
    router_state.cooldown_until["dep1"] = time.time() - 10
    available = router_state.get_available_deployments(time.time())
    assert "dep1" in available
    # Should auto-reset to HEALTHY
    assert router_state.deployment_status["dep1"] == DeploymentStatus.HEALTHY


# -------- reset_deployment (issue #49) --------

def test_reset_deployment_restores_health_state(router_state):
    """软恢复必须同步恢复 health_state，否则部署永远不会被再次调度。

    场景（issue #49）：部署失败进入 COOLDOWN 且 health_state=False；
    cooldown 到期后软恢复，health_state 应一并恢复为 True，
    让策略重新给该部署调度机会。
    """
    dep_id = "dep1"
    router_state.on_failure(dep_id, error=RuntimeError("boom"))
    assert router_state.deployment_status[dep_id] == DeploymentStatus.COOLDOWN
    assert router_state.health_state[dep_id] is False

    router_state.reset_deployment(dep_id)

    assert router_state.deployment_status[dep_id] == DeploymentStatus.HEALTHY
    assert router_state.cooldown_until[dep_id] is None
    assert router_state.health_state[dep_id] is True
    # consecutive_failures 保留，维持退避递增语义
    assert router_state.consecutive_failures[dep_id] == 1


def test_reset_then_failure_cooldown_increases(router_state):
    """软恢复后再次失败，退避时长应基于保留的失败次数递增。"""
    dep_id = "dep1"
    router_state.on_failure(dep_id, error=RuntimeError("boom"))
    router_state.reset_deployment(dep_id)
    router_state.on_failure(dep_id, error=RuntimeError("boom again"))

    assert router_state.deployment_status[dep_id] == DeploymentStatus.COOLDOWN
    # 第2次失败：cooldown = 60 * 2
    expected_min = time.time() + 60 * 2 - 1
    assert router_state.cooldown_until[dep_id] >= expected_min


def test_get_available_deployments_empty(router_state):
    available = router_state.get_available_deployments(time.time())
    assert available == []


def test_get_token_remaining_with_usage(router_state):
    """State 层语义：on_success 未预先注册配额时 limit=0，remaining=0。"""
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    remaining = router_state.get_token_remaining(dep_id)
    # TokenUsage.limit=0, used=50, so remaining = max(0, 0-50) = 0
    assert remaining == 0


def test_get_token_remaining_with_configured_limit(router_state):
    """已配置配额时 remaining 反映真实 limit（review 4 接线后的语义）。"""
    dep_id = "dep1"
    router_state.token_usage[dep_id] = TokenUsage(limit=100000)
    # 未使用时 remaining == limit
    assert router_state.get_token_remaining(dep_id) == 100000
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    assert router_state.get_token_remaining(dep_id) == 100000 - 50


def test_get_token_remaining_no_usage(router_state):
    remaining = router_state.get_token_remaining("nonexistent")
    assert remaining == float('inf')


def test_get_rpm_remaining_with_tracker(router_state):
    """State 层语义：未预先注册配额时 rpm_limit=0，remaining=0。"""
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    # rpm_limit=0 by default, so remaining should be 0
    remaining = router_state.get_rpm_remaining(dep_id)
    assert remaining == 0  # max(0, 0 - 1)


def test_get_rpm_remaining_with_configured_limit(router_state):
    """已配置 rpm_limit 时 remaining 反映真实配额（review 4 接线后的语义）。"""
    dep_id = "dep1"
    router_state.rpm_tracker[dep_id] = RPMTracker(rpm_limit=1000)
    assert router_state.get_rpm_remaining(dep_id) == 1000
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    assert router_state.get_rpm_remaining(dep_id) == 1000 - 1


def test_get_rpm_remaining_no_tracker(router_state):
    remaining = router_state.get_rpm_remaining("nonexistent")
    assert remaining == float('inf')


def test_get_token_utilization_with_usage(router_state):
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    ratio = router_state.get_token_utilization(dep_id)
    assert ratio == 0.0  # limit=0 so falls back to 0.0


def test_get_token_utilization_no_usage(router_state):
    ratio = router_state.get_token_utilization("nonexistent")
    assert ratio == 0.0


def test_get_rpm_utilization_with_tracker(router_state):
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    ratio = router_state.get_rpm_utilization(dep_id)
    assert ratio == 0.0  # rpm_limit=0


def test_get_rpm_utilization_no_tracker(router_state):
    ratio = router_state.get_rpm_utilization("nonexistent")
    assert ratio == 0.0


def test_concurrent_success_failure(router_state):
    """Thread safety: concurrent on_success and on_failure on same dep_id."""
    dep_id = "dep_concurrent"
    errors = []

    def success_worker():
        try:
            for _ in range(50):
                router_state.on_success(dep_id, latency=0.5, tokens=100)
        except Exception as e:
            errors.append(e)

    def failure_worker():
        try:
            for _ in range(50):
                router_state.on_failure(dep_id, ValueError("fail"))
        except Exception as e:
            errors.append(e)

    threads = [threading.Thread(target=success_worker) for _ in range(4)]
    threads += [threading.Thread(target=failure_worker) for _ in range(2)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    assert len(errors) == 0
    # State should be internally consistent
    assert router_state.consecutive_failures[dep_id] >= 0


# -------- cleanup_deployments (issue #50) --------

def test_cleanup_deployments_removes_stale_state(router_state):
    """热替换后，已移除部署的残留状态应被清理。"""
    router_state.on_success("dep_old", latency=0.1, tokens=10)
    router_state.on_failure("dep_old", error=RuntimeError("x"))
    router_state.on_success("dep_keep", latency=0.2, tokens=20)
    router_state.session_deployment_map["sess1"] = "dep_old"
    router_state.session_timestamps["sess1"] = time.time()

    removed = router_state.cleanup_deployments(["dep_keep"])

    assert removed == ["dep_old"]
    assert "dep_old" not in router_state.deployment_status
    assert "dep_old" not in router_state.consecutive_failures
    assert "dep_old" not in router_state.cooldown_until
    assert "dep_old" not in router_state.latencies
    assert "dep_old" not in router_state.total_tokens
    assert "dep_old" not in router_state.total_requests
    assert "dep_old" not in router_state.health_state
    assert "dep_old" not in router_state.token_usage
    assert "dep_old" not in router_state.rpm_tracker
    # 指向已移除部署的 session 映射被清理
    assert "sess1" not in router_state.session_deployment_map
    # 保留的部署不受影响
    assert router_state.total_tokens["dep_keep"] == 20


def test_cleanup_deployments_nothing_to_remove(router_state):
    router_state.on_success("dep1", latency=0.1, tokens=10)
    removed = router_state.cleanup_deployments(["dep1"])
    assert removed == []
    assert router_state.total_tokens["dep1"] == 10


# -------- tpm/rpm wiring through ReliableRouter (review 4, P0) --------

class TestRouterQuotaWiring:
    """ReliableRouter 注册部署时，tpm/rpm 必须接入 state 的配额追踪。"""

    def _router(self, deployments):
        from intelli_router.router.reliable_router import ReliableRouter
        return ReliableRouter(deployments=deployments, strategy="simple-shuffle")

    def test_constructor_initializes_quota_entries(
        self, deployment_gpt4_1, deployment_gpt3
    ):
        """带 tpm/rpm 的部署经构造注册后，state 中有对应配额条目。"""
        router = self._router([deployment_gpt4_1, deployment_gpt3])
        usage = router.state.token_usage.get("dep_gpt4_1")
        assert usage is not None
        assert usage.limit == 100000
        # 未使用时 remaining 反映真实 limit（修复前恒 0）
        assert router.state.get_token_remaining("dep_gpt4_1") == 100000
        tracker = router.state.rpm_tracker.get("dep_gpt4_1")
        assert tracker is not None
        assert tracker.rpm_limit == 1000
        assert router.state.get_rpm_remaining("dep_gpt4_1") == 1000

    def test_constructor_none_quota_keeps_inf_semantics(self, deployment_gpt3):
        """tpm/rpm 为 None 的部署不创建条目，remaining 保持 inf（未配置即不设限）。"""
        router = self._router([deployment_gpt3])
        assert "dep_gpt3" not in router.state.token_usage
        assert "dep_gpt3" not in router.state.rpm_tracker
        assert router.state.get_token_remaining("dep_gpt3") == float('inf')
        assert router.state.get_rpm_remaining("dep_gpt3") == float('inf')

    def test_constructor_partial_quota(self, deployment_gpt4_1):
        """只配置 tpm（rpm=None）时仅初始化 token 侧。"""
        from intelli_router.core.deployment import Deployment
        dep = Deployment(
            id="dep_tpm_only", model_name="m", api_key="k", api_base="b", tpm=5000
        )
        router = self._router([dep])
        assert router.state.get_token_remaining("dep_tpm_only") == 5000
        assert "dep_tpm_only" not in router.state.rpm_tracker
        assert router.state.get_rpm_remaining("dep_tpm_only") == float('inf')

    def test_update_deployments_initializes_new_and_cleans_removed(
        self, deployment_gpt4_1, deployment_gpt4_2, deployment_gpt3
    ):
        """热替换后：新增部署的配额条目被初始化，移除部署的条目被清理。"""
        router = self._router([deployment_gpt4_1, deployment_gpt4_2])
        assert router.state.get_token_remaining("dep_gpt4_1") == 100000
        assert router.state.get_rpm_remaining("dep_gpt4_2") == 500

        # 替换：移除 gpt4 两个部署，加入 gpt3（无配额）+ 新的带配额部署
        from intelli_router.core.deployment import Deployment
        dep_new = Deployment(
            id="dep_new", model_name="m2", api_key="k", api_base="b",
            tpm=20000, rpm=200,
        )
        router.update_deployments([deployment_gpt3, dep_new])

        # 移除的部署：配额条目清理
        assert "dep_gpt4_1" not in router.state.token_usage
        assert "dep_gpt4_2" not in router.state.rpm_tracker
        # 新部署：条目初始化
        assert router.state.get_token_remaining("dep_new") == 20000
        assert router.state.get_rpm_remaining("dep_new") == 200
        # None 配置：无条目，inf 语义
        assert "dep_gpt3" not in router.state.token_usage

    def test_update_deployments_preserves_accumulated_usage(
        self, deployment_gpt4_1, deployment_gpt3
    ):
        """同一部署保留在列表中时，热替换不应重置其累计 used。"""
        router = self._router([deployment_gpt4_1])
        router.state.on_success("dep_gpt4_1", latency=0.1, tokens=300)
        assert router.state.get_token_remaining("dep_gpt4_1") == 100000 - 300

        router.update_deployments([deployment_gpt4_1])
        # 累计值保留（不重建条目）
        assert router.state.token_usage["dep_gpt4_1"].used == 300
        assert router.state.get_token_remaining("dep_gpt4_1") == 100000 - 300
