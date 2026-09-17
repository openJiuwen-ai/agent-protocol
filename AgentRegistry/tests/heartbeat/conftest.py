"""Shared fixtures for the heartbeat test suite.

Builds on the per-feature ``tests/conftest.py`` fixtures (``lite_app``,
``agent_card``). Adds:

- ``heartbeat_enabled_dataset`` — fresh dataset with ``lease_config.enabled=true``
- ``heartbeat_disabled_dataset`` — fresh dataset with no lease config (legacy)
- ``short_ttl_dataset`` — min_ttl=1, max_ttl=3, grace=2 for fast sweep tests
- helpers to drive the sweeper synchronously (bypass real time)
"""

from __future__ import annotations

import uuid
from typing import Iterator, Tuple

import pytest
from fastapi.testclient import TestClient


def _new_ds_name(prefix: str = "hb") -> str:
    return f"{prefix}_" + uuid.uuid4().hex[:8]


@pytest.fixture
def heartbeat_enabled_dataset(lite_app: TestClient) -> Iterator[str]:
    """Create a dataset with heartbeat lease enabled at production defaults.

    min_ttl=5, max_ttl=60, grace_period=10. Tests that need fast sweep
    use ``short_ttl_dataset`` instead.
    """
    name = _new_ds_name("hbon")
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    r = lite_app.post(
        f"/api/datasets/{name}/lease-config",
        json={"enabled": True, "min_ttl": 5, "max_ttl": 60, "grace_period": 10},
    )
    assert r.status_code == 200, r.text
    yield name
    lite_app.delete(f"/api/datasets/{name}")


@pytest.fixture
def heartbeat_disabled_dataset(lite_app: TestClient) -> Iterator[str]:
    """Plain dataset, no lease_config — backward-compat baseline."""
    name = _new_ds_name("hboff")
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    yield name
    lite_app.delete(f"/api/datasets/{name}")


@pytest.fixture
def short_ttl_dataset(lite_app: TestClient) -> Iterator[Tuple[str, int, int]]:
    """Dataset with tiny TTL window for sweep / lifecycle tests.

    Returns ``(dataset_name, ttl_to_use, grace_period)``. The sweeper
    period is the server's default (5s), but tests should drive
    ``HeartbeatStore.sweep_tick`` directly to avoid sleep.
    """
    name = _new_ds_name("hbshort")
    r = lite_app.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    r = lite_app.post(
        f"/api/datasets/{name}/lease-config",
        json={"enabled": True, "min_ttl": 1, "max_ttl": 3, "grace_period": 2},
    )
    assert r.status_code == 200, r.text
    yield (name, 1, 2)
    lite_app.delete(f"/api/datasets/{name}")


@pytest.fixture
def heartbeat_store():
    """Live HeartbeatStore singleton from the warmed-up backend.

    Tests use this to drive sweep_tick directly without spinning the
    daemon (millisecond-fast). Lazy import so the test only fails when
    actually accessed if the module isn't loaded.
    """
    from a2x_registry.heartbeat.deps import get_heartbeat_store
    store = get_heartbeat_store()
    assert store is not None, "lite_app didn't initialize heartbeat store"
    return store


@pytest.fixture
def a2a_card_factory():
    """Same minimal A2A v1.0 card builder as in tests/auth/conftest.py.

    Duplicated here so this module is self-contained — keeping the
    fixture local makes each test suite's prerequisites explicit.
    """
    def _make(name: str = "agent-1") -> dict:
        return {
            "name": name,
            "description": "tester",
            "url": "http://example.invalid",
            "version": "1.0",
            "protocolVersion": "0.0.1",
            "capabilities": {},
            "defaultInputModes": ["text/plain"],
            "defaultOutputModes": ["text/plain"],
            "skills": [
                {"id": "s", "name": "s", "description": "s", "tags": ["t"]}
            ],
        }
    return _make
