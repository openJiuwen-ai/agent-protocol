"""In-memory data structures for the heartbeat store.

The lease state machine is the generic ``LeaseTable`` in
``a2x_registry.common.lease`` (shared with the cluster module). This
module re-exports the lease dataclass and state enum under their
heartbeat names so existing imports keep working:

    ``HeartbeatLease`` ≡ ``common.lease.Lease``
    ``HBState``        ≡ ``common.lease.LeaseState``

Persisted state (``RegistryEntry.lease_ttl``) lives in the register
module; the table only holds the **runtime** countdown state. Restart
clears it; the store re-grants a grace-period lease for every entry whose
persisted ``lease_ttl`` is non-empty (restart recovery).

``NodeLeaseConfig`` is the global per-node lease configuration used by
the appliance-mode heartbeat (key = node IP). Its field names match the
OpenAPI ``LeaseConfig`` schema so the router can serialise it directly.
"""

from __future__ import annotations

from dataclasses import dataclass

from a2x_registry.common.lease import Lease as HeartbeatLease, LeaseState as HBState


@dataclass
class NodeLeaseConfig:
    """Global lease configuration for per-node heartbeats (appliance mode).

    Field names align with the OpenAPI ``LeaseConfig`` schema
    (``enabled`` / ``min_ttl`` / ``max_ttl`` / ``grace_period``). Since
    the gateway does not send a client TTL on node heartbeats, ``min_ttl``
    doubles as the concrete node lease TTL; ``max_ttl`` is the upper bound
    (informational, reserved for future client-supplied TTL support).
    """

    enabled: bool = True
    min_ttl: int = 90
    max_ttl: int = 3600
    grace_period: int = 30


__all__ = ["HeartbeatLease", "HBState", "NodeLeaseConfig"]
