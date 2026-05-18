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
    assert dep.litellm_params == {}


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
