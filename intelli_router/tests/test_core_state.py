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


def test_get_available_deployments_empty(router_state):
    available = router_state.get_available_deployments(time.time())
    assert available == []


def test_get_token_remaining_with_usage(router_state):
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    remaining = router_state.get_token_remaining(dep_id)
    # TokenUsage.limit=0, used=50, so remaining = max(0, 0-50) = 0
    assert remaining == 0


def test_get_token_remaining_no_usage(router_state):
    remaining = router_state.get_token_remaining("nonexistent")
    assert remaining == float('inf')


def test_get_rpm_remaining_with_tracker(router_state):
    dep_id = "dep1"
    router_state.on_success(dep_id, latency=0.1, tokens=50)
    # rpm_limit=0 by default, so remaining should be 0
    remaining = router_state.get_rpm_remaining(dep_id)
    assert remaining == 0  # max(0, 0 - 1)


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
