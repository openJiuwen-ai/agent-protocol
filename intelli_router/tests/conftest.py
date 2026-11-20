"""Shared fixtures for intelli_router tests."""
import time
import pytest
from unittest.mock import MagicMock, AsyncMock
from intelli_router.core.deployment import Deployment, DeploymentStatus
from intelli_router.core.context import RoutingContext
from intelli_router.core.state import LocalRouterState
from intelli_router.cache.local_cache import LocalCache


# -------- Sample Deployments --------

@pytest.fixture
def deployment_gpt4_1():
    """A healthy GPT-4 deployment with tags."""
    return Deployment(
        id="dep_gpt4_1",
        model_name="gpt-4",
        api_key="sk-test-1",
        api_base="https://api1.example.com",
        tags=["production", "us-east"],
        tpm=100000,
        rpm=1000,
    )


@pytest.fixture
def deployment_gpt4_2():
    """A second healthy GPT-4 deployment, different region."""
    return Deployment(
        id="dep_gpt4_2",
        model_name="gpt-4",
        api_key="sk-test-2",
        api_base="https://api2.example.com",
        tags=["production", "eu-west"],
        tpm=50000,
        rpm=500,
    )


@pytest.fixture
def deployment_gpt4_cooldown():
    """A GPT-4 deployment currently in cooldown."""
    return Deployment(
        id="dep_cooldown",
        model_name="gpt-4",
        api_key="sk-test-3",
        api_base="https://api3.example.com",
        status=DeploymentStatus.COOLDOWN,
        cooldown_until=time.time() + 3600,
        tags=["staging"],
    )


@pytest.fixture
def deployment_gpt3():
    """A GPT-3.5 deployment (different model)."""
    return Deployment(
        id="dep_gpt3",
        model_name="gpt-3.5-turbo",
        api_key="sk-test-4",
        api_base="https://api4.example.com",
    )


@pytest.fixture
def sample_deployments(deployment_gpt4_1, deployment_gpt4_2, deployment_gpt4_cooldown, deployment_gpt3):
    """Standard set of deployments for integration tests."""
    return [deployment_gpt4_1, deployment_gpt4_2, deployment_gpt4_cooldown, deployment_gpt3]


# -------- Routing Contexts --------

@pytest.fixture
def basic_context():
    """Simple routing context with no tags."""
    return RoutingContext(
        model="gpt-4",
        messages=[{"role": "user", "content": "hello"}],
    )


@pytest.fixture
def tagged_context():
    """Routing context with request tags."""
    return RoutingContext(
        model="gpt-4",
        messages=[{"role": "user", "content": "hello"}],
        request_tags=["production", "us-east"],
    )


@pytest.fixture
def session_context():
    """Routing context with session_id."""
    return RoutingContext(
        model="gpt-4",
        messages=[{"role": "user", "content": "hello"}],
        kwargs={"session_id": "session_abc123"},
    )


# -------- Router State --------

@pytest.fixture
def router_state():
    """Fresh LocalRouterState."""
    return LocalRouterState()


@pytest.fixture
def populated_state(router_state, deployment_gpt4_1, deployment_gpt4_2):
    """Router state with some latency history and token usage."""
    router_state.on_success(deployment_gpt4_1.id, latency=0.5, tokens=100)
    router_state.on_success(deployment_gpt4_1.id, latency=0.3, tokens=150)
    router_state.on_success(deployment_gpt4_2.id, latency=1.2, tokens=200)
    return router_state


# -------- Cache --------

@pytest.fixture
def empty_cache():
    """Empty LocalCache with default settings."""
    return LocalCache()


@pytest.fixture
def small_cache():
    """Small cache (max 3) for testing LRU eviction."""
    return LocalCache(max_size=3, default_ttl=3600)


@pytest.fixture
def short_ttl_cache():
    """Cache with very short TTL for expiry tests."""
    return LocalCache(max_size=100, default_ttl=0.01)


# -------- Async HTTP Mocks --------

@pytest.fixture
def mock_async_client():
    """A base AsyncMock for httpx.AsyncClient.post."""
    mock_client = AsyncMock()
    mock_response = MagicMock()
    mock_response.raise_for_status = MagicMock()
    mock_response.json.return_value = {
        "choices": [{"message": {"content": "test response"}}],
        "usage": {"completion_tokens": 10, "total_tokens": 20},
    }
    mock_response.status_code = 200
    mock_client.post.return_value = mock_response
    return mock_client
