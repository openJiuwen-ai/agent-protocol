"""Tests for intelli_router.utils.exceptions."""
import time
from intelli_router.utils.exceptions import (
    IntelliRouterError, RouterError, NoDeploymentAvailable, AllDeploymentsFailed,
    StrategyError, DeploymentError, DeploymentInCooldown, DeploymentFailed,
    DeploymentTimeoutError, DeploymentAuthError, DeploymentRateLimitError,
    DeploymentServerError, DeploymentNetworkError, CacheError, CacheKeyNotFound,
    CacheExpired, HealthCheckError, HealthCheckFailed, HealthCheckTimeout,
    ConfigError, InvalidDeploymentConfig, MissingRequiredField,
    ERROR_STATUS_MAP, get_status_code,
)


def test_all_exception_instantiation():
    """Verify every exception class can be instantiated with expected args."""
    # -- Base --
    e = IntelliRouterError("base error", details={"k": "v"})
    assert str(e) == "base error"
    assert e.details == {"k": "v"}

    # -- Router --
    e = NoDeploymentAvailable("gpt-4")
    assert "gpt-4" in str(e)

    e = NoDeploymentAvailable("gpt-4", "no endpoints")
    assert "no endpoints" in str(e)

    e = AllDeploymentsFailed("gpt-4", ["err1", "err2"])
    assert "gpt-4" in str(e)
    assert "err1" in e.details["errors"]

    e = StrategyError("adaptive", "no data")
    assert "adaptive" in str(e)

    # -- Deployment --
    e = DeploymentInCooldown("dep1", time.time() + 60)
    assert "dep1" in str(e)

    e = DeploymentFailed("dep1", 5)
    assert "dep1" in str(e)
    assert "5" in str(e)

    e = DeploymentTimeoutError("dep1", 30.0)
    assert "dep1" in str(e)

    e = DeploymentAuthError("dep1", 401)
    assert "dep1" in str(e)
    e2 = DeploymentAuthError("dep1", 403)
    assert "403" in str(e2)

    e = DeploymentRateLimitError("dep1")
    assert e.details.get("retry_after") is None
    e = DeploymentRateLimitError("dep1", 30.0)
    assert e.details.get("retry_after") == 30.0
    assert "dep1" in str(e)

    e = DeploymentServerError("dep1", 500, "internal error")
    assert "dep1" in str(e)
    assert "500" in str(e)
    assert "internal error" in e.details["response_body"]

    e = DeploymentNetworkError("dep1", "connection refused")
    assert "dep1" in str(e)
    assert "connection refused" in str(e)

    # -- Cache --
    e = CacheKeyNotFound("mykey")
    assert "mykey" in str(e)

    e = CacheExpired("mykey", 123456.0)
    assert "mykey" in str(e)

    # -- Health --
    e = HealthCheckFailed("dep1", "timeout")
    assert "dep1" in str(e)
    assert "timeout" in e.details["error"]

    e = HealthCheckTimeout("dep1", 5.0)
    assert "dep1" in str(e)

    # -- Config --
    e = InvalidDeploymentConfig("api_key", "", "empty")
    assert "api_key" in str(e)

    e = MissingRequiredField("api_key", "Deployment")
    assert "api_key" in str(e)
    assert "Deployment" in str(e)


def test_to_dict():
    """Verify to_dict() returns correct format."""
    e = IntelliRouterError("test msg", details={"d1": 1})
    d = e.to_dict()
    assert d["error_type"] == "IntelliRouterError"
    assert d["message"] == "test msg"
    assert d["details"] == {"d1": 1}

    e = NoDeploymentAvailable("gpt-4", "reason")
    d = e.to_dict()
    assert d["error_type"] == "NoDeploymentAvailable"


def test_get_status_code():
    """Verify ERROR_STATUS_MAP and get_status_code MRO traversal."""
    # Direct mapping
    assert get_status_code(NoDeploymentAvailable("m")) == 503
    assert get_status_code(DeploymentTimeoutError("d", 30)) == 504
    assert get_status_code(DeploymentAuthError("d", 401)) == 401
    assert get_status_code(DeploymentRateLimitError("d")) == 429
    assert get_status_code(DeploymentServerError("d", 500, "")) == 502
    assert get_status_code(DeploymentNetworkError("d", "x")) == 502
    assert get_status_code(HealthCheckTimeout("d", 5)) == 504

    # MRO fallback: CacheKeyNotFound -> CacheError -> 500 (but CacheKeyNotFound is directly mapped to 404)
    assert get_status_code(CacheKeyNotFound("k")) == 404

    # Plain Exception fallback
    assert get_status_code(Exception()) == 500
    assert get_status_code(ValueError("x")) == 500


def test_error_status_map_coverage():
    """Every exception class in ERROR_STATUS_MAP is mapping to an int."""
    for exc_cls, code in ERROR_STATUS_MAP.items():
        assert isinstance(code, int), f"{exc_cls} -> {code} is not int"
        assert 100 <= code <= 599
