"""Reusable cluster test harness: in-process transport + fake registry.

Importable from both conftest (fixtures) and individual tests (custom
topologies like a 3-node chain).
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from a2x_registry.cluster.config import ClusterConfig
from a2x_registry.cluster.state import ClusterState
from a2x_registry.cluster.store import ClusterStore
from a2x_registry.cluster.transport import Transport
from a2x_registry.register.models import GenericServiceData, RegistryEntry


class FakeRegistry:
    """Minimal RegistryService surface the ClusterStore depends on."""

    def __init__(self) -> None:
        self._data: Dict[str, Dict[str, Tuple[RegistryEntry, dict]]] = {}
        self._auth_required: set[str] = set()

    def add_generic(self, dataset: str, name: str, description: str = "d",
                    source: str = "api_config") -> str:
        from a2x_registry.register.store import generate_service_id
        sid = generate_service_id("generic", name)
        entry = RegistryEntry(
            service_id=sid, type="generic", source=source,
            service_data=GenericServiceData(name=name, description=description),
        )
        wrapped = {
            "id": sid, "type": "generic", "name": name,
            "description": description, "metadata": {},
        }
        self._data.setdefault(dataset, {})[sid] = (entry, wrapped)
        return sid

    def remove(self, dataset: str, sid: str) -> None:
        self._data.get(dataset, {}).pop(sid, None)

    def set_auth_required(self, dataset: str, required: bool) -> None:
        (self._auth_required.add if required else self._auth_required.discard)(dataset)

    def list_datasets(self) -> List[str]:
        return list(self._data)

    def list_entries(self, dataset: str) -> List[RegistryEntry]:
        return [e for e, _ in self._data.get(dataset, {}).values()]

    def list_services(self, dataset: str) -> List[dict]:
        return [w for _, w in self._data.get(dataset, {}).values()]

    def get_entry(self, dataset: str, sid: str) -> Optional[RegistryEntry]:
        rec = self._data.get(dataset, {}).get(sid)
        return rec[0] if rec else None

    def is_auth_required(self, dataset: str) -> bool:
        return dataset in self._auth_required


class InProcessTransport(Transport):
    """Routes peer calls to the target store's handlers in-process."""

    def __init__(self) -> None:
        self._stores: Dict[str, ClusterStore] = {}
        self.dropped: set[Tuple[str, str]] = set()  # (caller_advertise, target) links cut

    def register(self, address: str, store: ClusterStore) -> None:
        self._stores[address] = store

    def cut(self, a: str, b: str) -> None:
        """Simulate a partition between two addresses (both directions)."""
        self.dropped.add((a, b))
        self.dropped.add((b, a))

    def _target(self, caller: str, address: str) -> ClusterStore:
        from a2x_registry.cluster.transport import TransportError
        if (caller, address) in self.dropped:
            raise TransportError(f"partitioned: {caller} -> {address}")
        return self._stores[address]

    # Each call carries the caller via from_node where available; for open we
    # can't see the caller's advertise, so open is never partitioned in tests
    # (partitions are exercised on digest/pull/updates).
    def open(self, address: str, body: dict) -> dict:
        return self._stores[address].handle_open(body)

    def merkle(self, address, from_node, namespaces, token=None):
        return self._target(from_node, address).serve_merkle(from_node, namespaces or None, token)

    def digest(self, address, from_node, namespaces, token=None, buckets=None):
        return self._target(from_node, address).serve_digest(
            from_node, namespaces or None, token, buckets=buckets)

    def pull(self, address, from_node, keys, token=None):
        return self._target(from_node, address).serve_pull(from_node, keys, token)

    def updates(self, address, from_node, envelopes, token=None):
        return self._target(from_node, address).serve_updates(from_node, envelopes, token)

    def keepalive(self, address, from_node, token=None):
        return self._target(from_node, address).handle_keepalive(from_node, token)

    def _reg(self, address: str) -> ClusterStore:
        """Resolve an address; an unregistered one models an unreachable peer."""
        from a2x_registry.cluster.transport import TransportError
        if address not in self._stores:
            raise TransportError(f"unreachable: {address}")
        return self._stores[address]

    # ── membership control plane ────────────────────────────────────────
    # join/evict/evict_self are control ops (an unknown address = unreachable);
    # set_digest/pull/sync ride the partition-able link.
    def join(self, address, body):
        return self._reg(address).membership.handle_join(body)

    def evict(self, address, body):
        return self._reg(address).membership.handle_evicted(body)

    def evict_self(self, address, body):
        return self._reg(address).membership.handle_evict_self(body)

    def set_digest(self, address, from_node, token=None):
        return self._target(from_node, address).membership.serve_set_digest(from_node, token)

    def set_pull(self, address, from_node, node_ids, token=None):
        return self._target(from_node, address).membership.serve_set_pull(from_node, node_ids, token)

    def set_sync(self, address, from_node, records, token=None):
        return self._target(from_node, address).membership.serve_set_sync(from_node, records, token)


def converge(stores, rounds: int = 4) -> None:
    """Drive anti-entropy: each node reconciles each of its peers (records +
    membership deltas), several rounds, to a fixed point."""
    from a2x_registry.cluster.transport import TransportError
    for _ in range(rounds):
        for s in stores:
            for p in s.list_peers():
                try:
                    s.reconcile(p)
                    if s.membership is not None:
                        s.membership.reconcile_with(p)
                except TransportError:
                    pass


def settle(stores, rounds: int = 4) -> None:
    """Drive the membership control loop to a fixed point: each node
    reconciles its connections (roster→sessions) then exchanges record +
    membership deltas. Mirrors what the AntiEntropySweeper does each tick."""
    for _ in range(rounds):
        for s in stores:
            if s.membership is not None:
                s.membership.reconcile_connections()
        converge(stores, rounds=1)


def visible(store, registry, dataset: str) -> set:
    """Service names a node can serve from ``dataset`` = local entries +
    replicated foreign rows (what the merged list endpoint returns)."""
    local = {
        e.service_data.name for e in registry.list_entries(dataset)
        if e.service_data is not None
    }
    foreign = {r["wrapped"]["name"] for r in store.foreign_rows(dataset)}
    return local | foreign


class FakeClock:
    """Manually-advanced monotonic clock for deterministic liveness tests."""

    def __init__(self, t: float = 0.0) -> None:
        self.t = t

    def __call__(self) -> float:
        return self.t

    def advance(self, dt: float) -> None:
        self.t += dt


def build_store(tmp_path, name, registry, transport, *, auth_store=None,
                config: Optional[ClusterConfig] = None, clock=None,
                membership: bool = True) -> ClusterStore:
    from a2x_registry.cluster.membership import MembershipStore
    state = ClusterState.init(node_id=name, path=tmp_path / f"{name}.json")
    store = ClusterStore(
        state,
        config=config or ClusterConfig(),
        registry_svc=registry,
        transport=transport,
        advertise=name,  # node name doubles as its in-process address
        auth_store_getter=(lambda: auth_store),
        clock=clock,
    )
    if membership:
        store.membership = MembershipStore(store)
    transport.register(name, store)
    return store
