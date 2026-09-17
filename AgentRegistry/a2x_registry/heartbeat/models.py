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
"""

from __future__ import annotations

from a2x_registry.common.lease import Lease as HeartbeatLease, LeaseState as HBState

__all__ = ["HeartbeatLease", "HBState"]
