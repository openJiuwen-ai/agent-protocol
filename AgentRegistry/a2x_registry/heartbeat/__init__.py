"""Service heartbeat / lease module for A2X Registry.

Per-namespace opt-in: a namespace remains "no heartbeat support" until its
``lease_config.json`` has ``enabled: true``; an individual service stays
permanent unless registered with an explicit ``lease_ttl``. See
``docs/heartbeat_design.md`` for the full design (lease state machine,
4-corner matrix, sweeper algorithm).

Public API:
    - ``HeartbeatStore`` — in-memory lease tracking + sweep
    - ``HeartbeatLease`` / ``HBState`` — dataclass / enum models
    - ``HeartbeatSweeper`` — background daemon (marks unhealthy, hard-deletes)
    - ``get_heartbeat_store`` / ``set_heartbeat_store`` — module-level singleton hooks
    - ``router`` — FastAPI router for ``/api/datasets/{ds}/services/{sid}/heartbeat``
    - domain exceptions (``HeartbeatNotSupportedError``, ``TTLOutOfRangeError``,
      ``TTLRequiredError``) — all subclasses of ``ValueError`` so the existing
      ``_run`` error mapper turns them into structured 400 responses

The store / models / sweeper layer is FastAPI-free (testable without uvicorn).
Web concerns live in ``deps.py`` / ``router.py``. The ``system_ctx`` module
provides the synthetic ``AuthContext`` the sweeper uses to call into
``RegistryService.deregister`` for hard-deletion.
"""

from .errors import (
    HeartbeatNotSupportedError,
    TTLOutOfRangeError,
    TTLRequiredError,
)
from .models import HeartbeatLease, HBState
from .store import HeartbeatStore
from .sweeper import HeartbeatSweeper
from .deps import get_heartbeat_store, set_heartbeat_store
from .system_ctx import SYSTEM_CTX

__all__ = [
    "HeartbeatStore",
    "HeartbeatLease",
    "HBState",
    "HeartbeatSweeper",
    "HeartbeatNotSupportedError",
    "TTLOutOfRangeError",
    "TTLRequiredError",
    "get_heartbeat_store",
    "set_heartbeat_store",
    "SYSTEM_CTX",
]
