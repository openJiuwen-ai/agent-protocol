"""In-memory data structures for the heartbeat store.

Persisted state (``RegistryEntry.lease_ttl``) lives in the register module;
this module only holds the **runtime** countdown state. Restart clears it
all, but the store re-grants a grace-period lease for every entry whose
persisted ``lease_ttl`` is non-empty (restart recovery — gives clients one
grace window to re-heartbeat).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class HBState(str, Enum):
    """Lease health state. ``str`` mixin → serializes cleanly to JSON."""

    HEALTHY = "healthy"      # heartbeat received recently; expires_at is in the future
    UNHEALTHY = "unhealthy"  # TTL elapsed but still in grace window; can recover


@dataclass
class HeartbeatLease:
    """Per-(dataset, sid) heartbeat lease state.

    All time fields use ``time.monotonic()`` to be immune to wall-clock
    jumps (NTP adjustments / suspended VM). The wall-clock ``expires_at``
    is computed on demand for client-facing responses.
    """

    ttl_seconds: int                  # client's requested TTL at registration
    grace_period_seconds: int         # admin-configured on the namespace
    expires_at: float                 # monotonic: when state flips HEALTHY → UNHEALTHY
    grace_deadline: float             # monotonic: when state flips UNHEALTHY → hard-delete
    last_heartbeat_at: float          # monotonic: for audit / observability
    state: HBState = HBState.HEALTHY  # cached; derivable from the timestamps above
