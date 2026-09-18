"""Tests for intelli_router.core.context."""
from intelli_router.core.context import RoutingContext
from intelli_router.core.deployment import Deployment


def test_default_values():
    ctx = RoutingContext(model="gpt-4", messages=[{"role": "user", "content": "hi"}])
    assert ctx.kwargs == {}
    assert ctx.selected_deployment is None
    assert ctx.attempted_deployments == []
    assert ctx.end_time is None
    assert ctx.response is None
    assert ctx.error is None
    assert ctx.request_tags == []


def test_mark_attempt():
    dep = Deployment(model_name="m", api_key="k", api_base="b", id="dep1")
    ctx = RoutingContext(model="m", messages=[])
    ctx.mark_attempt(dep)
    assert ctx.attempted_deployments == ["dep1"]

    dep2 = Deployment(model_name="m", api_key="k", api_base="b", id="dep2")
    ctx.mark_attempt(dep2)
    assert ctx.attempted_deployments == ["dep1", "dep2"]


def test_set_success():
    dep = Deployment(model_name="m", api_key="k", api_base="b", id="dep1")
    ctx = RoutingContext(model="m", messages=[])
    response = {"choices": [{"text": "ok"}]}
    ctx.set_success(dep, response)
    assert ctx.selected_deployment == dep
    assert ctx.response == response
    assert ctx.end_time is not None
    assert ctx.error is None


def test_set_failure():
    ctx = RoutingContext(model="m", messages=[])
    error = ValueError("test error")
    ctx.set_failure(error)
    assert ctx.error is error
    assert ctx.end_time is not None
    assert ctx.selected_deployment is None


def test_latency_no_end_time():
    ctx = RoutingContext(model="m", messages=[])
    assert ctx.latency == 0.0


def test_latency_after_success():
    dep = Deployment(model_name="m", api_key="k", api_base="b", id="dep1")
    ctx = RoutingContext(model="m", messages=[])
    ctx.set_success(dep, {})
    assert ctx.latency > 0.0


def test_latency_after_failure():
    ctx = RoutingContext(model="m", messages=[])
    ctx.set_failure(RuntimeError("fail"))
    assert ctx.latency > 0.0


def test_to_log_dict_no_attempt():
    ctx = RoutingContext(model="m", messages=[])
    log = ctx.to_log_dict()
    assert log["success"] is True
    assert log["latency"] == 0.0
    assert log["model"] == "m"
    assert log["selected_deployment"] is None


def test_to_log_dict_success():
    dep = Deployment(model_name="m", api_key="k", api_base="b", id="dep1")
    ctx = RoutingContext(model="m", messages=[])
    ctx.set_success(dep, {"ok": True})
    log = ctx.to_log_dict()
    assert log["success"] is True
    assert log["selected_deployment"] == "dep1"
    assert log["latency"] > 0.0


def test_to_log_dict_failure():
    ctx = RoutingContext(model="m", messages=[])
    ctx.set_failure(ValueError("fail"))
    log = ctx.to_log_dict()
    assert log["success"] is False
    assert log["latency"] > 0.0


def test_to_log_dict_keys_stable():
    """Keys in to_log_dict are stable across calls."""
    ctx = RoutingContext(model="m", messages=[])
    log = ctx.to_log_dict()
    expected_keys = {"model", "selected_deployment", "latency", "attempted", "success"}
    assert set(log.keys()) == expected_keys
