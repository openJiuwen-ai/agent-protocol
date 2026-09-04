"""Tests for intelli_router.core.deployment."""
import time
import pytest
from intelli_router.core.deployment import Deployment, DeploymentStatus


def test_deployment_status_values():
    assert DeploymentStatus.HEALTHY.value == "healthy"
    assert DeploymentStatus.COOLDOWN.value == "cooldown"


def test_deployment_defaults():
    dep = Deployment(model_name="gpt-4", api_key="sk-key", api_base="https://api.test.com")
    assert len(dep.id) == 8
    assert dep.status == DeploymentStatus.HEALTHY
    assert dep.consecutive_failures == 0
    assert dep.cooldown_until is None
    assert dep.tags == []
    assert dep.tpm is None
    assert dep.rpm is None
    assert dep.timeout is None
    assert dep.verify_ssl is True


def test_deployment_auto_id():
    dep1 = Deployment(model_name="m", api_key="k", api_base="b")
    dep2 = Deployment(model_name="m", api_key="k", api_base="b")
    assert dep1.id != dep2.id


def test_to_dict():
    dep = Deployment(
        id="test123", model_name="gpt-4", api_key="sk-key", api_base="https://api.test.com",
        status=DeploymentStatus.COOLDOWN, tags=["prod"], rpm=100,
    )
    d = dep.to_dict()
    assert d["id"] == "test123"
    assert d["model_name"] == "gpt-4"
    assert d["api_key"] == "sk-key"
    assert d["api_base"] == "https://api.test.com"
    assert d["status"] == "cooldown"
    assert d["tags"] == ["prod"]
    assert d["rpm"] == 100


def test_from_dict():
    data = {
        "id": "from_dict_id",
        "model_name": "gpt-4",
        "api_key": "sk-fd",
        "api_base": "https://api.test.com",
        "status": "healthy",
        "tags": ["test"],
        "rpm": 50,
        "tpm": 10000,
    }
    dep = Deployment.from_dict(data)
    assert dep.id == "from_dict_id"
    assert dep.model_name == "gpt-4"
    assert dep.status == DeploymentStatus.HEALTHY
    assert dep.tags == ["test"]
    assert dep.rpm == 50
    assert dep.tpm == 10000


def test_from_dict_default_status():
    """from_dict without status defaults to healthy."""
    dep = Deployment.from_dict({
        "model_name": "m", "api_key": "k", "api_base": "b",
    })
    assert dep.status == DeploymentStatus.HEALTHY


def test_is_available_healthy():
    dep = Deployment(model_name="m", api_key="k", api_base="b")
    assert dep.is_available(time.time()) is True


def test_is_available_cooldown_future():
    dep = Deployment(
        model_name="m", api_key="k", api_base="b",
        status=DeploymentStatus.COOLDOWN,
        cooldown_until=time.time() + 3600,
    )
    assert dep.is_available(time.time()) is False


def test_is_available_cooldown_past():
    dep = Deployment(
        model_name="m", api_key="k", api_base="b",
        status=DeploymentStatus.COOLDOWN,
        cooldown_until=time.time() - 10,
    )
    assert dep.is_available(time.time()) is True


def test_is_available_cooldown_no_timestamp():
    """COOLDOWN with cooldown_until=None is treated as available."""
    dep = Deployment(
        model_name="m", api_key="k", api_base="b",
        status=DeploymentStatus.COOLDOWN,
    )
    assert dep.is_available(time.time()) is True


def test_from_dict_extra_keys_raises():
    """Unknown keys in dict should raise TypeError."""
    data = {
        "model_name": "m", "api_key": "k", "api_base": "b",
        "nonexistent_field": "value",
    }
    with pytest.raises(TypeError):
        Deployment.from_dict(data)


def test_from_dict_missing_required():
    """Missing required fields should raise TypeError."""
    with pytest.raises(TypeError):
        Deployment.from_dict({"model_name": "m"})


# -------- __post_init__ validation (issue #51) --------

def _base_kwargs():
    return {"model_name": "m", "api_key": "k", "api_base": "b"}


@pytest.mark.parametrize("field", ["model_name", "api_key", "api_base"])
def test_empty_string_fields_raise(field):
    with pytest.raises(ValueError, match=field):
        Deployment(**{**_base_kwargs(), field: ""})


def test_non_string_model_name_raises():
    with pytest.raises(ValueError, match="model_name"):
        Deployment(model_name=123, api_key="k", api_base="b")


@pytest.mark.parametrize("field", ["tpm", "rpm"])
def test_tpm_rpm_valid(field):
    dep = Deployment(**_base_kwargs(), **{field: 100})
    assert getattr(dep, field) == 100
    dep = Deployment(**_base_kwargs())  # None is allowed
    assert getattr(dep, field) is None


@pytest.mark.parametrize("field", ["tpm", "rpm"])
def test_tpm_rpm_zero_or_negative_raises(field):
    with pytest.raises(ValueError, match=field):
        Deployment(**_base_kwargs(), **{field: 0})
    with pytest.raises(ValueError, match=field):
        Deployment(**_base_kwargs(), **{field: -5})


@pytest.mark.parametrize("field", ["tpm", "rpm"])
def test_tpm_rpm_non_int_raises(field):
    with pytest.raises(TypeError, match=field):
        Deployment(**_base_kwargs(), **{field: "abc"})
    with pytest.raises(TypeError, match=field):
        Deployment(**_base_kwargs(), **{field: 1.5})
    # bool is a subclass of int and must be rejected
    with pytest.raises(TypeError, match=field):
        Deployment(**_base_kwargs(), **{field: True})


def test_consecutive_failures_non_negative_int():
    dep = Deployment(**_base_kwargs(), consecutive_failures=3)
    assert dep.consecutive_failures == 3
    with pytest.raises(ValueError, match="consecutive_failures"):
        Deployment(**_base_kwargs(), consecutive_failures=-1)
    with pytest.raises(TypeError, match="consecutive_failures"):
        Deployment(**_base_kwargs(), consecutive_failures="x")
    with pytest.raises(TypeError, match="consecutive_failures"):
        Deployment(**_base_kwargs(), consecutive_failures=True)


def test_timeout_validation():
    dep = Deployment(**_base_kwargs(), timeout=30.0)
    assert dep.timeout == 30.0
    with pytest.raises(ValueError, match="timeout"):
        Deployment(**_base_kwargs(), timeout=0)
    with pytest.raises(ValueError, match="timeout"):
        Deployment(**_base_kwargs(), timeout=-1.0)
    with pytest.raises(TypeError, match="timeout"):
        Deployment(**_base_kwargs(), timeout="30")
