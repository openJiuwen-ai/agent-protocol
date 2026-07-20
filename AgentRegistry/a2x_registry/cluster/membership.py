"""Declarative cluster membership control plane.

Each node owns **exactly one** membership record (origin-only, LWW):

    MembershipRecord { node_id, cluster_id|None, address, version, removed }

``roster(C)`` = every record with ``cluster_id == C`` and ``not removed``.
The records are replicated across the cluster as a small overlay
(``_roster``) kept **separate** from the service-record overlay
(``ClusterStore._foreign``). That separation is deliberate: membership must
not flow through namespace-auth gating or the service read path, and a
removal tombstone must survive a session drop (the opposite of how foreign
service records are evicted on disconnect).

Replication has three deterministic paths (no randomness — customer
requirement):

  1. **bootstrap push** — ``set add`` sends each brand-new member a ``join``
     carrying the current roster (it isn't connected yet, so it can't learn
     the roster any other way).
  2. **immediate push** — a local change (add / remove / leave) is pushed to
     all roster members right away (best-effort).
  3. **delta anti-entropy** — the reconcile loop exchanges a compact roster
     version map ``{node_id: version}`` and pulls only what's newer. This
     heals dropped pushes and stays O(N) per peer (vs. shipping the whole
     roster every keepalive).

The control plane decides *who is in the cluster and whom to connect*; the
``ClusterStore`` executes ``connect_peer`` / ``disconnect_peer`` and runs the
data plane. ``register/`` stays completely unaware of all this.
"""

from __future__ import annotations

import logging
import threading
import time
import uuid
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

from .envelope import Version, version_newer
from .transport import TransportError

logger = logging.getLogger(__name__)


def generate_cluster_id() -> str:
    """A readable, globally-unique cluster id. Minted once by ``set add``
    when a node first forms a cluster; never participates in any convergence
    decision, so using a UUID does not violate the no-randomness rule."""
    return f"clu-{uuid.uuid4().hex[:12]}"


@dataclass
class MembershipRecord:
    node_id: str
    cluster_id: Optional[str]
    address: str
    version: Version  # (updated_at_ms, node_id) — LWW key
    removed: bool = False

    def to_dict(self) -> dict:
        return {
            "node_id": self.node_id,
            "cluster_id": self.cluster_id,
            "address": self.address,
            "version": list(self.version),
            "removed": self.removed,
        }

    @classmethod
    def from_dict(cls, d: dict) -> "MembershipRecord":
        return cls(
            node_id=d["node_id"],
            cluster_id=d.get("cluster_id"),
            address=d.get("address", ""),
            version=tuple(d["version"]),
            removed=bool(d.get("removed", False)),
        )


class MembershipStore:
    """Owns the membership overlay + the set add/remove/leave control plane.

    Holds a back-reference to the ``ClusterStore`` for the connection
    primitives (``connect_peer`` / ``disconnect_peer`` / ``list_peers``),
    the transport, identity, and auth. It never opens sockets itself beyond
    those primitives + the membership RPCs on the transport.
    """

    def __init__(self, store) -> None:
        self._store = store
        self._state = store.state
        self._lock = threading.RLock()
        # node_id -> MembershipRecord (own record + peers; may be tombstones)
        self._roster: Dict[str, MembershipRecord] = {}
        self._init_from_state()

    # ── identity ────────────────────────────────────────────────────────

    @property
    def node_id(self) -> str:
        return self._store.node_id

    @property
    def cluster_id(self) -> Optional[str]:
        with self._lock:
            own = self._roster.get(self.node_id)
            return own.cluster_id if own else None

    def my_record(self) -> MembershipRecord:
        with self._lock:
            own = self._roster.get(self.node_id)
            if own is None:
                own = MembershipRecord(
                    self.node_id, None, self._store.advertise,
                    self._store.next_version(), False,
                )
                self._roster[self.node_id] = own
            return own

    def _init_from_state(self) -> None:
        """Seed the overlay from persisted state so a restart rejoins the
        same cluster, auto-reconnects the mesh, and still remembers recent
        removals (tombstones) for the retention window."""
        cid = self._state.cluster_id
        if cid is None:
            return
        mmv = self._state.my_membership_version
        if mmv:
            # Keep the shared version clock monotonic against our restored
            # membership version, so the next local mint can't regress.
            self._store.observe_version(mmv[0])
        ver = tuple(mmv) if mmv else self._store.next_version()
        self._roster[self.node_id] = MembershipRecord(
            self.node_id, cid, self._store.advertise, ver, False,
        )
        for d in self._state.last_roster or []:
            try:
                if "version" in d:
                    rec = MembershipRecord.from_dict(d)
                else:  # legacy {node_id, address} hint
                    nid = d.get("node_id")
                    if not nid:
                        continue
                    rec = MembershipRecord(nid, cid, d.get("address", ""), (0, nid), False)
            except Exception:  # noqa: BLE001 — skip a malformed record
                continue
            if rec.node_id != self.node_id and rec.node_id not in self._roster:
                self._roster[rec.node_id] = rec
        # Forget removals already past the retention window.
        self.gc_membership()

    # ── LWW merge (inbound) ─────────────────────────────────────────────

    def merge(self, records: List[dict]) -> bool:
        """LWW-merge incoming membership records. Returns True if anything
        changed. Our own record is authoritative (self-origin) and ignored,
        EXCEPT a newer removal tombstone for ourselves = we were evicted."""
        changed = False
        evicted_self = False
        with self._lock:
            for d in records:
                try:
                    rec = MembershipRecord.from_dict(d)
                except Exception:  # noqa: BLE001 — skip a malformed record
                    continue
                cur = self._roster.get(rec.node_id)
                if not version_newer(rec.version, cur.version if cur else None):
                    continue
                if rec.node_id == self.node_id:
                    if rec.removed:
                        evicted_self = True
                    continue  # self-origin: local copy wins
                self._roster[rec.node_id] = rec
                changed = True
        if evicted_self:
            self._become_standalone()
            return True
        if changed:
            self._persist()
        return changed

    # ── digest / pull (delta anti-entropy) ──────────────────────────────

    def version_map(self) -> Dict[str, list]:
        with self._lock:
            return {nid: list(r.version) for nid, r in self._roster.items()}

    def records_for(self, node_ids: List[str]) -> List[dict]:
        with self._lock:
            return [self._roster[n].to_dict() for n in node_ids if n in self._roster]

    def reconcile_with(self, peer) -> None:
        """Pull membership deltas from one peer (best-effort)."""
        if self.cluster_id is None:
            return
        try:
            remote = self._store.transport.set_digest(peer.address, self.node_id, peer.token)
        except TransportError:
            return
        mine = self.version_map()
        want = [
            nid for nid, rv in remote.items()
            if version_newer(tuple(rv), tuple(mine[nid]) if nid in mine else None)
        ]
        if not want:
            return
        try:
            recs = self._store.transport.set_pull(peer.address, self.node_id, want, peer.token)
        except TransportError:
            return
        self.merge(recs)

    # ── serve side (peer → us) ──────────────────────────────────────────

    def serve_set_digest(self, from_node: str, token: Optional[str] = None) -> dict:
        if not self._store.authed(from_node, token):
            return {}
        return self.version_map()

    def serve_set_pull(self, from_node: str, node_ids: List[str],
                       token: Optional[str] = None) -> list:
        if not self._store.authed(from_node, token):
            return []
        return self.records_for(node_ids)

    def serve_set_sync(self, from_node: str, records: List[dict],
                       token: Optional[str] = None) -> dict:
        """Receive a pushed batch (immediate-push path) and LWW-merge it."""
        if not self._store.authed(from_node, token):
            return {"accepted": False}
        self.merge(records)
        return {"accepted": True}

    # ── immediate push (outbound) ───────────────────────────────────────

    def push_to_roster(self, records: Optional[List[dict]] = None) -> None:
        """Push records (default: our own) to every live roster member."""
        if records is None:
            records = [self.my_record().to_dict()]
        cid = self.cluster_id
        with self._lock:
            targets = [
                (r.node_id, r.address) for r in self._roster.values()
                if r.node_id != self.node_id and not r.removed and r.cluster_id == cid
                and r.address
            ]
        # Concurrent best-effort push; anti-entropy heals any drop.
        self._store.fan_out([
            (lambda nid=nid, addr=addr: self._store.transport.set_sync(
                addr, self.node_id, records, self._peer_token(nid)))
            for nid, addr in targets
        ])

    # ── control plane (user → us) ───────────────────────────────────────

    def set_add(self, members: List[dict], token: Optional[str] = None) -> dict:
        """Add members (each ``{address, node_id?}``) to this node's cluster,
        minting a cluster_id if we don't have one. Bootstraps each new member
        with a ``join`` push carrying the current roster."""
        with self._lock:
            if self.cluster_id is None:
                cid = generate_cluster_id()
                self._roster[self.node_id] = MembershipRecord(
                    self.node_id, cid, self._store.advertise,
                    self._store.next_version(), False,
                )
            cid = self.cluster_id
            roster_snapshot = [
                r.to_dict() for r in self._roster.values() if not r.removed
            ]
        results = []
        for m in members:
            addr = m.get("address")
            if not addr:
                results.append({"address": addr, "ok": False, "error": "address required"})
                continue
            body = {
                "cluster_id": cid, "roster": roster_snapshot, "token": token,
                "from_node": self.node_id, "from_address": self._store.advertise,
            }
            try:
                resp = self._store.transport.join(addr, body)
            except TransportError as exc:
                results.append({"address": addr, "ok": False, "error": f"unreachable: {exc}"})
                continue
            if not resp.get("accepted"):
                results.append({"address": addr, "ok": False,
                                "error": resp.get("error", "rejected")})
                continue
            nid = resp.get("node_id")
            ver = tuple(resp.get("version")) if resp.get("version") else self._store.next_version()
            with self._lock:
                self._roster[nid] = MembershipRecord(nid, cid, addr, ver, False)
            results.append({"address": addr, "node_id": nid, "ok": True})
        self._persist()
        self.reconcile_connections()
        # Propagate the grown roster to everyone (new members already have it).
        with self._lock:
            full = [r.to_dict() for r in self._roster.values()]
        self.push_to_roster(full)
        return {"cluster_id": self.cluster_id, "results": results}

    def set_remove(self, members: List[dict]) -> dict:
        """Remove members (each ``{node_id}``). Writes a removal tombstone
        (deterministic, not HOLD-based), notifies each removed node, and
        disconnects it."""
        results, tombstones = [], []
        for m in members:
            nid = m.get("node_id")
            if not nid:
                results.append({"ok": False, "error": "node_id required"})
                continue
            with self._lock:
                cur = self._roster.get(nid)
                addr = cur.address if cur else m.get("address", "")
                # Tombstone must out-version the (foreign-minted) live record.
                ver = self._store.next_version_after(cur.version[0] if cur else 0)
                tomb = MembershipRecord(nid, self.cluster_id, addr, ver, True)
                self._roster[nid] = tomb
            tombstones.append(tomb.to_dict())
            try:
                self._store.transport.evict(
                    addr, {"from_node": self.node_id, "cluster_id": self.cluster_id,
                           "token": self._peer_token(nid)},
                )
            except TransportError:
                pass
            self._store.disconnect_peer(nid)
            results.append({"node_id": nid, "ok": True})
        self._persist()
        if tombstones:
            self.push_to_roster(tombstones)
        return {"results": results}

    def show(self) -> dict:
        live = {p.node_id for p in self._store.list_peers()}
        with self._lock:
            roster = [
                {"node_id": r.node_id, "address": r.address,
                 "alive": (r.node_id in live) or (r.node_id == self.node_id)}
                for r in self._roster.values() if not r.removed
            ]
        return {"cluster_id": self.cluster_id, "node_id": self.node_id, "roster": roster}

    # ── adopt / leave (peer → us) ───────────────────────────────────────

    def handle_join(self, body: dict) -> dict:
        """A peer is pulling us into ``cluster_id``. Auth-gate (admin token
        when auth is on), then adopt."""
        if not self._join_authorized(body.get("token")):
            return {"accepted": False, "error": "unauthorized"}
        cid = body.get("cluster_id")
        if not cid:
            return {"accepted": False, "error": "cluster_id required"}
        return self.adopt(cid, body.get("roster") or [])

    def adopt(self, cluster_id: str, roster: List[dict]) -> dict:
        """Adopt ``cluster_id`` (leaving any old cluster first), learn the
        roster, connect the mesh, announce ourselves."""
        old = self.cluster_id
        if old is not None and old != cluster_id:
            self.leave_old(old)
        ver = self._store.next_version()
        with self._lock:
            self._roster[self.node_id] = MembershipRecord(
                self.node_id, cluster_id, self._store.advertise, ver, False,
            )
        self.merge(roster)
        self._persist()
        self.reconcile_connections()
        self.push_to_roster()
        return {"node_id": self.node_id, "cluster_id": cluster_id,
                "version": list(ver), "accepted": True}

    def leave_old(self, old_cluster_id: str) -> None:
        """Gracefully leave the old cluster: tell each old member (so they
        drop us immediately, not via HOLD), then disconnect. Sent BEFORE
        disconnecting so the leave RPC can still authenticate over the
        existing session."""
        with self._lock:
            members = [
                (r.node_id, r.address) for r in self._roster.values()
                if r.node_id != self.node_id and r.cluster_id == old_cluster_id
                and not r.removed
            ]
        for nid, addr in members:
            try:
                self._store.transport.evict_self(
                    addr, {"from_node": self.node_id, "token": self._peer_token(nid)},
                )
            except TransportError:
                pass
        for nid, _ in members:
            self._store.disconnect_peer(nid)
        # Drop only the old cluster's records — keep ourselves and anything a
        # concurrent inbound merge learned about the new cluster during the
        # network window above (a blind reset would lose those).
        with self._lock:
            self._roster = {
                n: r for n, r in self._roster.items()
                if n == self.node_id or r.cluster_id != old_cluster_id
            }

    def handle_evict_self(self, body: dict) -> dict:
        """A peer is gracefully leaving our cluster → tombstone it + drop it.
        Authenticated: the leaver proves its session, so a spoofer can't forge
        a removal tombstone for an arbitrary victim (open cluster → no-op
        gate, consistent with the rest of the open-cluster trust model)."""
        nid = body.get("from_node")
        if not nid or not self._store.authed(nid, body.get("token")):
            return {"ok": False}
        with self._lock:
            cur = self._roster.get(nid)
            ver = self._store.next_version_after(cur.version[0] if cur else 0)
            tomb = MembershipRecord(
                nid, cur.cluster_id if cur else self.cluster_id,
                cur.address if cur else "", ver, True,
            )
            self._roster[nid] = tomb
        self._store.disconnect_peer(nid)
        self._persist()
        self.push_to_roster([tomb.to_dict()])
        return {"ok": True}

    def handle_evicted(self, body: dict) -> dict:
        """We were removed from our cluster → revert to standalone.
        Authenticated: only a peer with a valid session (i.e. the removing
        coordinator) can force this, so an unauthenticated caller can't
        partition us off the cluster (open cluster → no-op gate)."""
        if not self._store.authed(body.get("from_node", ""), body.get("token")):
            return {"ok": False}
        self._become_standalone()
        return {"ok": True}

    # ── reconcile-loop input ────────────────────────────────────────────

    def desired_peers(self) -> Dict[str, str]:
        """{node_id: address} we should hold a session with = live roster of
        our cluster minus ourselves. Empty when standalone."""
        cid = self.cluster_id
        if cid is None:
            return {}
        with self._lock:
            return {
                r.node_id: r.address for r in self._roster.values()
                if r.node_id != self.node_id and r.cluster_id == cid
                and not r.removed and r.address
            }

    def is_removed_or_absent(self, node_id: str) -> bool:
        """True if ``node_id`` is not a wanted member (tombstoned, gone, or in
        another cluster) — i.e. the reconcile loop may disconnect it."""
        cid = self.cluster_id
        with self._lock:
            r = self._roster.get(node_id)
        return r is None or r.removed or r.cluster_id != cid

    def reconcile_connections(self) -> None:
        """Drive the session set toward ``desired_peers()``: connect missing
        members, disconnect removed/absent ones. Called by the background
        sweeper every tick. No-op when standalone (so manual add-peer is
        untouched when membership isn't in use)."""
        if self.cluster_id is None:
            return
        desired = self.desired_peers()
        actual = {p.node_id for p in self._store.list_peers()}
        for nid, addr in desired.items():
            if nid not in actual:
                try:
                    self._store.connect_peer(addr)
                except TransportError:
                    pass  # unreachable now; retried next tick
                except Exception as exc:  # noqa: BLE001 — never kill the loop
                    logger.warning("cluster: membership connect to %s failed: %s", addr, exc)
        for nid in actual - set(desired):
            # Only drop members the roster says are gone — never a transiently
            # unreachable-but-still-wanted peer.
            if self.is_removed_or_absent(nid):
                self._store.disconnect_peer(nid)

    # ── internals ───────────────────────────────────────────────────────

    def _become_standalone(self) -> None:
        with self._lock:
            members = [r.node_id for r in self._roster.values() if r.node_id != self.node_id]
            self._roster = {}
        for nid in members:
            self._store.disconnect_peer(nid)
        self._persist()

    def _join_authorized(self, token: Optional[str]) -> bool:
        auth = self._store.auth_store
        if auth is None:
            return True
        if not token:
            return False
        try:
            ctx = auth.authenticate(token)
        except Exception:  # noqa: BLE001 — any auth failure → unauthorized
            return False
        return ctx is not None and ctx.is_admin

    def _peer_token(self, node_id: str) -> Optional[str]:
        for p in self._store.list_peers():
            if p.node_id == node_id:
                return p.token
        return None

    def gc_membership(self, now_ms: Optional[int] = None) -> int:
        """Drop removal tombstones older than the retention window so the
        roster overlay stays bounded. Retention ≥ the HOLD/eviction window, so
        every peer has seen the removal before we forget it (preventing a
        late-returning node from being resurrected). Returns the count pruned.
        Mirrors ``ClusterStore.gc_tombstones`` for the membership plane."""
        if now_ms is None:
            now_ms = time.time_ns() // 1_000_000
        retention_ms = int(self._store.config.tombstone_retention * 1000)
        with self._lock:
            stale = [
                nid for nid, r in self._roster.items()
                if r.removed and now_ms - r.version[0] > retention_ms
            ]
            for nid in stale:
                del self._roster[nid]
        return len(stale)

    def _persist(self) -> None:
        with self._lock:
            own = self._roster.get(self.node_id)
            cid = own.cluster_id if own else None
            # Persist live members AND recent tombstones (so a restart keeps
            # the removal window — a node removed while offline can't be
            # resurrected by a peer that restarted and forgot the tombstone).
            records = [
                r.to_dict() for r in self._roster.values()
                if r.node_id != self.node_id and r.cluster_id == cid
            ]
            mmv = list(own.version) if own else None
        self._state.cluster_id = cid
        self._state.last_roster = records
        self._state.my_membership_version = mmv
        self._store.save_state()
