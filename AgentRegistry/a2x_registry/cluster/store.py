"""ClusterStore — the cluster module's single stateful object.

Holds node identity + persisted local version/tombstone state, the
in-memory foreign-record overlay, and peer sessions. Exposes two kinds of
methods:

  - **handlers** (``handle_open`` / ``serve_digest`` / ``serve_pull`` /
    ``serve_updates``) — invoked by the local FastAPI router when a peer
    calls us. Tests invoke them directly through an in-process transport.
  - **orchestration** (``connect_peer`` / ``reconcile`` / ``disconnect_peer``)
    — invoked locally; reach out to peers through the injected ``Transport``.

Replication model: origin-only writes, so the global identity of a record
is ``(dataset, origin_id, service_id)`` and there are no write-write
conflicts — LWW (version ``(updated_at_ms, node_id)``) just dedups/orders
versions of the same record. Foreign records are read-only and memory-only.
"""

from __future__ import annotations

import logging
import secrets
import threading
import time
from concurrent.futures import ThreadPoolExecutor, wait
from typing import Callable, Dict, List, Optional, Set, Tuple

from . import merkle
from .auth_handshake import authorize_namespaces
from .config import ClusterConfig
from .envelope import SyncEnvelope, Version, version_newer
from .peer import Peer
from .state import ClusterState, Tombstone, make_key, split_key
from .transport import HttpTransport, Transport, TransportError

logger = logging.getLogger(__name__)

# In-memory foreign-overlay key: (dataset, origin_id, service_id).
_Key = Tuple[str, str, str]


class ClusterStore:
    """Owns all cluster runtime state for this registry instance."""

    def __init__(
        self,
        state: ClusterState,
        config: Optional[ClusterConfig] = None,
        registry_svc=None,
        transport: Optional[Transport] = None,
        advertise: str = "",
        auth_store_getter=None,
        clock=None,
    ) -> None:
        self._state = state
        self._config = config or ClusterConfig()
        self._registry = registry_svc
        self._transport = transport or HttpTransport(self._config.http_timeout)
        self._advertise = advertise
        self._auth_store_getter = auth_store_getter or (lambda: None)
        # Monotonic clock for liveness timers (leases / HOLD / suppression).
        # Injectable so tests can drive eviction deterministically; defaults
        # to time.monotonic in production.
        self._clock = clock or time.monotonic
        self._lock = threading.RLock()
        # (dataset, origin_id, sid) -> envelope (live or tombstone)
        self._foreign: Dict[_Key, SyncEnvelope] = {}
        # peer node_id -> Peer
        self._sessions: Dict[str, Peer] = {}
        # node_id -> monotonic deadline until which records from that origin
        # are suppressed (rejected) after eviction. Full-mesh: a peer evicted
        # via HOLD on one node could otherwise be re-pulled by anti-entropy
        # from a peer that hasn't evicted it yet (ping-pong). The cooldown
        # blocks that; (re)establishing a session with the node clears it.
        self._evicted_until: Dict[str, float] = {}
        # Bounded pool for concurrent best-effort fan-out (broadcast /
        # keepalive / membership push). Concurrency caps a full-mesh fan-out
        # at ~one timeout window so a dead peer can't serialize the others.
        self._pool = ThreadPoolExecutor(
            max_workers=max(4, self._config.broadcast_workers),
            thread_name_prefix="ClusterFanout",
        )
        # Membership control plane (cluster-set feature). Attached after
        # construction (backend startup / tests) so the data plane stays
        # usable on its own. None → no declarative membership (manual
        # add-peer still works).
        self.membership = None

    @classmethod
    def load_or_none(
        cls,
        config: Optional[ClusterConfig] = None,
        registry_svc=None,
        transport: Optional[Transport] = None,
        advertise: str = "",
        auth_store_getter=None,
    ) -> Optional["ClusterStore"]:
        """Build the store from a persisted ``cluster_state.json``, or
        return ``None`` when the file is absent (cluster not initialized →
        feature stays dormant). This is what makes the module opt-in.

        Defensive: a missing OR unreadable/corrupt state file both yield
        ``None`` (the registry stays standalone) rather than crashing
        startup. The corrupt case is logged so an operator can fix it.
        """
        try:
            state = ClusterState.load()
        except Exception as exc:  # noqa: BLE001 — corrupt file must not crash boot
            logger.error(
                "cluster: failed to load cluster_state.json (%s); staying standalone", exc,
            )
            return None
        if state is None:
            return None
        return cls(
            state, config=config, registry_svc=registry_svc,
            transport=transport, advertise=advertise,
            auth_store_getter=auth_store_getter,
        )

    # ── identity / config ───────────────────────────────────────────────

    @property
    def node_id(self) -> str:
        return self._state.node_id

    @property
    def config(self) -> ClusterConfig:
        return self._config

    @property
    def advertise(self) -> str:
        """This node's peer-reachable base URL (empty until configured)."""
        return self._advertise

    @property
    def transport(self) -> Transport:
        return self._transport

    @property
    def state(self) -> ClusterState:
        """Persisted cluster state (shared with the membership control plane)."""
        return self._state

    @property
    def auth_store(self):
        """The active auth store, or None on an open cluster."""
        return self._auth_store_getter()

    def authed(self, from_node: str, token: Optional[str] = None) -> bool:
        """Public wrapper over the session-auth check, for the membership
        control plane's RPC handlers."""
        return self._authed(from_node, token)

    def next_version(self) -> Tuple[int, str]:
        """A fresh LWW version ``(updated_at_ms, node_id)`` drawn from the
        shared monotonic clock. Used by the membership control plane so its
        records stay monotonic with service records on this node."""
        with self._lock:
            return (self._next_ts(), self.node_id)

    def next_version_after(self, floor_ms: int = 0) -> Tuple[int, str]:
        """A fresh version guaranteed strictly newer than ``floor_ms`` under
        LWW. The normal monotonic guard only protects against our own clock
        step-back; when membership tombstones a record minted by *another*
        node (set-remove / graceful-leave), that record's timestamp can
        exceed ours and our node_id might lose the tiebreak. Bumping past it
        makes the tombstone always win."""
        with self._lock:
            now_ms = time.time_ns() // 1_000_000
            ts = max(now_ms, self._state.version_clock + 1, floor_ms + 1)
            self._state.version_clock = ts
            return (ts, self.node_id)

    def observe_version(self, ms: int) -> None:
        """Advance the version clock to at least ``ms`` (e.g. a membership
        version restored from disk) so future local mints stay monotonic
        against it."""
        with self._lock:
            if ms > self._state.version_clock:
                self._state.version_clock = ms

    def save_state(self) -> None:
        """Persist cluster state under the store lock. All writers go through
        here (or ``_state.save`` while already holding the lock), so a
        membership persist can't race a service-side ``to_dict`` mid-mutation
        and two writers can't lose each other's snapshot."""
        with self._lock:
            self._state.save()

    # ── versioning (monotonic, survives clock step-back) ────────────────

    def _next_ts(self) -> int:
        """Next version timestamp (ms). Caller holds ``self._lock``."""
        now_ms = time.time_ns() // 1_000_000
        ts = max(now_ms, self._state.version_clock + 1)
        self._state.version_clock = ts
        return ts

    def _ensure_local_versions(self) -> None:
        """Assign a version to every local-origin record that doesn't have
        one yet (e.g. records that existed before cluster init / before the
        mutation hook). Persists once if anything changed."""
        if self._registry is None:
            return
        with self._lock:
            changed = False
            for ds in self._registry.list_datasets():
                for entry in self._registry.list_entries(ds):
                    if entry.source == "ephemeral":
                        continue
                    k = make_key(ds, entry.service_id)
                    if k not in self._state.local_versions and k not in self._state.tombstones:
                        self._state.local_versions[k] = [self._next_ts(), self.node_id]
                        changed = True
            if changed:
                self._state.save()

    # ── index / envelope helpers ────────────────────────────────────────

    def _wrapped_map(self, dataset: str) -> Dict[str, dict]:
        if self._registry is None:
            return {}
        return {s["id"]: s for s in self._registry.list_services(dataset)}

    def _local_index(self, namespaces: Optional[List[str]]) -> Dict[_Key, Version]:
        """Versions of all local-origin live records + local tombstones,
        scoped to ``namespaces`` (None = all local datasets)."""
        self._ensure_local_versions()
        idx: Dict[_Key, Version] = {}
        if self._registry is None:
            return idx
        datasets = set(self._registry.list_datasets())
        scope = datasets if not namespaces else (datasets & set(namespaces))
        with self._lock:
            for ds in scope:
                for entry in self._registry.list_entries(ds):
                    if entry.source == "ephemeral":
                        continue
                    v = self._state.local_versions.get(make_key(ds, entry.service_id))
                    if v is not None:
                        idx[(ds, self.node_id, entry.service_id)] = tuple(v)
            for k, t in self._state.tombstones.items():
                ds, sid = split_key(k)
                if namespaces and ds not in namespaces:
                    continue
                idx[(ds, self.node_id, sid)] = tuple(t.version)
        return idx

    def _full_index(self, namespaces: Optional[List[str]]) -> Dict[_Key, Version]:
        """Local + foreign record versions, scoped to ``namespaces``."""
        idx = self._local_index(namespaces)
        with self._lock:
            for (ds, origin, sid), env in self._foreign.items():
                if namespaces and ds not in namespaces:
                    continue
                idx[(ds, origin, sid)] = tuple(env.version)
        return idx

    def _build_local_envelope(self, dataset: str, sid: str) -> Optional[SyncEnvelope]:
        with self._lock:
            k = make_key(dataset, sid)
            tomb = self._state.tombstones.get(k)
            if tomb is not None:
                return SyncEnvelope(
                    dataset=dataset, service_id=sid, origin_id=self.node_id,
                    version=tuple(tomb.version), tombstone=True, payload=None,
                )
            v = self._state.local_versions.get(k)
        if v is None or self._registry is None:
            return None
        entry = self._registry.get_entry(dataset, sid)
        if entry is None:
            return None
        wrapped = self._wrapped_map(dataset).get(sid)
        payload = {"entry": entry.model_dump(mode="json"), "wrapped": wrapped}
        return SyncEnvelope(
            dataset=dataset, service_id=sid, origin_id=self.node_id,
            version=tuple(v), tombstone=False, payload=payload,
        )

    def _build_envelope_for_key(self, key: _Key) -> Optional[SyncEnvelope]:
        ds, origin, sid = key
        if origin == self.node_id:
            return self._build_local_envelope(ds, sid)
        with self._lock:
            return self._foreign.get(key)

    # ── inbound apply (LWW dedup; no relay until M2) ────────────────────

    def apply_inbound(self, env: SyncEnvelope) -> bool:
        """Accept ``env`` into the foreign overlay iff strictly newer than
        what we have. Returns True if accepted (stored), else False.

        Self-origin envelopes are always ignored: our own state (including
        tombstones) is authoritative, so a peer can never reintroduce a
        record we own.
        """
        if env.origin_id == self.node_id:
            return False
        with self._lock:
            # Suppress re-learning a just-evicted origin until the cooldown
            # elapses (or it reconnects). Without this, a peer that hasn't
            # evicted yet would resurrect it via anti-entropy.
            until = self._evicted_until.get(env.origin_id)
            if until is not None and self._clock() < until:
                return False
            cur = self._foreign.get(env.key)
            cur_v = cur.version if cur is not None else None
            if not version_newer(env.version, cur_v):
                return False
            self._foreign[env.key] = env
        return True

    # ── handlers (peer → us) ────────────────────────────────────────────

    def handle_open(self, body: dict) -> dict:
        """Receive an OPEN: authorize per-namespace, record the session,
        return our node id + the accepted namespaces.

        The candidate namespace set is the union of what the caller offered
        (its own datasets) and our own datasets, so both sides' namespaces
        get synced (subject to auth).
        """
        from_node = body["node_id"]
        address = body.get("address", "")
        offered = set(body.get("namespaces") or [])
        token = body.get("token")
        local_ns = set(self._registry.list_datasets()) if self._registry else set()
        candidate = sorted(offered | local_ns)
        auth_store = self._auth_store_getter()
        accepted, ephemeral = authorize_namespaces(
            self._registry, auth_store, candidate, token,
        )
        # Issue a per-session secret ONLY when this registry has auth on, so
        # subsequent RPCs from this peer can be authenticated. No auth → no
        # token system (open cluster).
        session_token = secrets.token_hex(16) if auth_store is not None else None
        with self._lock:
            self._sessions[from_node] = Peer(
                from_node, address, set(accepted),
                last_seen=self._clock(), token=session_token,
            )
            self._evicted_until.pop(from_node, None)  # it's back → lift suppression
        logger.info("cluster: session opened with %s (ns=%s)", from_node, accepted)
        return {
            "node_id": self.node_id, "accepted": accepted,
            "ephemeral": ephemeral, "session_token": session_token,
        }

    def _touch_peer(self, node_id: str) -> None:
        """Refresh the direct-link HOLD timer for a peer we just heard from."""
        with self._lock:
            peer = self._sessions.get(node_id)
            if peer is not None:
                peer.last_seen = self._clock()

    def _public_namespaces(self) -> Set[str]:
        if self._registry is None:
            return set()
        auth_store = self._auth_store_getter()
        return {
            ds for ds in self._registry.list_datasets()
            if auth_store is None or not self._registry.is_auth_required(ds)
        }

    def _authed(self, from_node: str, token: Optional[str]) -> bool:
        """Is the ``from_node`` claim authenticated for this call?

        - No auth configured → True (open cluster; no token system).
        - Auth configured → a session must exist for ``from_node`` and the
          presented ``token`` must equal its established session secret.
        """
        if self._auth_store_getter() is None:
            return True
        with self._lock:
            sess = self._sessions.get(from_node)
        return sess is not None and sess.token is not None and token == sess.token

    def _allowed_for(self, from_node: str, requested: Optional[List[str]],
                     token: Optional[str] = None) -> Set[str]:
        authed = self._authed(from_node, token)
        with self._lock:
            sess = self._sessions.get(from_node)
        # Authenticated session → its negotiated namespaces; otherwise fall
        # back to only the publicly-syncable (non-auth-required) namespaces.
        if authed and sess is not None:
            base = set(sess.namespaces)
        else:
            base = self._public_namespaces()
        if requested:
            base &= set(requested)
        return base

    def serve_digest(self, from_node: str, namespaces: Optional[List[str]],
                     token: Optional[str] = None,
                     buckets: Optional[List[int]] = None) -> list:
        """Return ``[dataset, origin_id, service_id, version]`` rows for the
        records visible to ``from_node`` (session-scoped, authenticated).
        When ``buckets`` is given, only rows whose key falls in one of those
        Merkle buckets are returned (anti-entropy fetches just the differing
        buckets)."""
        self._touch_peer(from_node)
        allowed = self._allowed_for(from_node, namespaces, token)
        idx = self._full_index(sorted(allowed) if allowed else [])
        bset = set(buckets) if buckets is not None else None
        n = self._config.merkle_buckets
        return [
            [ds, origin, sid, list(ver)]
            for (ds, origin, sid), ver in idx.items()
            if ds in allowed
            and (bset is None or merkle.bucket_of((ds, origin, sid), n) in bset)
        ]

    def serve_merkle(self, from_node: str, namespaces: Optional[List[str]],
                     token: Optional[str] = None) -> dict:
        """Return ``{bucket_index: hash}`` for the records visible to
        ``from_node``. O(buckets) — the anti-entropy fast path."""
        self._touch_peer(from_node)
        allowed = self._allowed_for(from_node, namespaces, token)
        idx = {
            k: v for k, v in self._full_index(sorted(allowed) if allowed else []).items()
            if k[0] in allowed
        }
        return merkle.bucket_hashes(idx, self._config.merkle_buckets)

    def serve_pull(self, from_node: str, keys: List[list],
                   token: Optional[str] = None) -> list:
        """Return full envelopes for the requested keys (session-scoped)."""
        self._touch_peer(from_node)
        allowed = self._allowed_for(from_node, None, token)
        out = []
        for k in keys:
            ds, origin, sid = k[0], k[1], k[2]
            if ds not in allowed:
                continue
            env = self._build_envelope_for_key((ds, origin, sid))
            if env is not None:
                out.append(env.model_dump(mode="json"))
        return out

    def _may_accept(self, from_node: str, dataset: str, token: Optional[str]) -> bool:
        """Inbound authorization for a record's namespace.

        Mirrors the handshake gate so a peer can't bypass it by POSTing
        straight to /updates:
          - no auth configured here → open cluster, accept anything;
          - an authenticated session (valid token) → its negotiated
            namespaces (includes ephemeral ones);
          - otherwise only existing non-auth-required namespaces;
          - reject protected / unknown namespaces.
        """
        if self._auth_store_getter() is None:
            return True
        if self._authed(from_node, token):
            with self._lock:
                sess = self._sessions.get(from_node)
            if sess is not None and dataset in sess.namespaces:
                return True
        if self._registry is not None and dataset in self._registry.list_datasets():
            return not self._registry.is_auth_required(dataset)
        return False

    def serve_updates(self, from_node: str, envelopes: List[dict],
                      token: Optional[str] = None) -> dict:
        """Apply a batch of inbound envelopes (LWW dedup, namespace-gated).

        Full-mesh: every member is a direct peer of the origin, so there is
        no relay — each node receives a record directly from its origin's
        broadcast and simply stores the newer version.
        """
        self._touch_peer(from_node)
        accepted = 0
        rejected = 0
        for raw in envelopes:
            env = SyncEnvelope.model_validate(raw)
            if not self._may_accept(from_node, env.dataset, token):
                rejected += 1
                continue
            if self.apply_inbound(env):
                accepted += 1
        return {"accepted": accepted, "received": len(envelopes), "rejected": rejected}

    # ── outbound replication ────────────────────────────────────────────

    def fan_out(self, thunks: List[Callable[[], None]]) -> None:
        """Run best-effort peer calls concurrently and wait for them all.
        Concurrency (bounded by the pool) means one unreachable peer's
        timeout doesn't serialize the rest — a full-mesh fan-out costs ~one
        timeout window, not N. Exceptions are swallowed (best-effort);
        periodic anti-entropy heals anything dropped."""
        if not thunks:
            return
        futures = [self._pool.submit(self._guarded, t) for t in thunks]
        wait(futures)

    @staticmethod
    def _guarded(thunk: Callable[[], None]) -> None:
        try:
            thunk()
        except TransportError:
            pass
        except Exception as exc:  # noqa: BLE001 — best-effort; don't propagate
            logger.warning("cluster: fan-out task failed: %s", exc)

    def close(self) -> None:
        """Release the fan-out pool and any pooled transport connections.
        Called on server shutdown; safe to call more than once."""
        self._pool.shutdown(wait=False)
        closer = getattr(self._transport, "close", None)
        if callable(closer):
            closer()

    def _broadcast(self, env: SyncEnvelope) -> None:
        """Send ``env`` concurrently to every session that syncs its dataset.
        Best-effort; full-mesh means this reaches the whole cluster directly,
        with no onward relay."""
        payload = [env.model_dump(mode="json")]
        with self._lock:
            peers = [p for p in self._sessions.values() if env.dataset in p.namespaces]
        self.fan_out([
            (lambda p=p: self._transport.updates(p.address, self.node_id, payload, p.token))
            for p in peers
        ])

    # ── liveness: direct-link keepalive / HOLD ──────────────────────────

    def handle_keepalive(self, from_node: str, token: Optional[str] = None) -> dict:
        """Direct-link keepalive — refresh the HOLD timer (authenticated when
        auth is on)."""
        if not self._authed(from_node, token):
            return {"ok": False}
        self._touch_peer(from_node)
        return {"ok": True}

    def emit_keepalive(self) -> None:
        with self._lock:
            peers = list(self._sessions.values())
        self.fan_out([
            (lambda p=p: self._transport.keepalive(p.address, self.node_id, p.token))
            for p in peers
        ])

    def check_hold(self, now: Optional[float] = None) -> List[str]:
        """Drop sessions whose direct link has been silent past ``hold_timeout``.
        Returns the dropped node ids."""
        if now is None:
            now = self._clock()
        with self._lock:
            stale = [
                p.node_id for p in self._sessions.values()
                if now - p.last_seen > self._config.hold_timeout
            ]
        for node_id in stale:
            logger.info("cluster: peer %s HOLD expired; dropping session", node_id)
            self.disconnect_peer(node_id)
        return stale

    def prune_suppression(self, now: Optional[float] = None) -> None:
        """Drop expired post-eviction suppression entries (memory hygiene).
        An expired entry is already inert in ``apply_inbound``; this just
        keeps the dict from growing unbounded. Called by the anti-entropy
        sweeper alongside tombstone GC."""
        if now is None:
            now = self._clock()
        with self._lock:
            for o in [o for o, t in self._evicted_until.items() if now >= t]:
                del self._evicted_until[o]

    # ── orchestration (us → peer) ───────────────────────────────────────

    def connect_peer(
        self, address: str, namespaces: Optional[List[str]] = None, token: Optional[str] = None,
    ) -> Peer:
        """Initiate a session with the peer at ``address`` and run an
        initial full reconcile. ``namespaces`` defaults to our own datasets
        so the peer learns everything we host."""
        offered = list(namespaces) if namespaces else (
            list(self._registry.list_datasets()) if self._registry else []
        )
        body = {
            "node_id": self.node_id,
            "address": self._advertise,
            "namespaces": offered,
            "token": token,
        }
        resp = self._transport.open(address, body)
        peer = Peer(resp["node_id"], address, set(resp.get("accepted") or []),
                    last_seen=self._clock(), token=resp.get("session_token"))
        with self._lock:
            self._sessions[peer.node_id] = peer
            self._evicted_until.pop(peer.node_id, None)  # it's back → lift suppression
        logger.info("cluster: connected to %s (ns=%s)", peer.node_id, sorted(peer.namespaces))
        self.reconcile(peer)
        return peer

    def reconcile(self, peer: Peer) -> dict:
        """Bidirectional anti-entropy with ``peer``, Merkle fast-path first:
        compare bucket hashes (O(buckets)); if identical we're already in
        sync and stop. Otherwise fetch + diff only the differing buckets and
        pull what's newer / push what we have newer. Best-effort — transport
        errors propagate to the caller."""
        ns = sorted(peer.namespaces)
        local_index = self._full_index(ns)
        n = self._config.merkle_buckets

        # Fast path: only the bucket hashes cross the wire when nothing changed.
        local_buckets = merkle.bucket_hashes(local_index, n)
        remote_buckets = self._transport.merkle(peer.address, self.node_id, ns, peer.token)
        diff = merkle.differing_buckets(local_buckets, remote_buckets)
        if not diff:
            return {"pulled": 0, "pushed": 0}

        # Only the differing buckets transfer their rows.
        remote_rows = self._transport.digest(
            peer.address, self.node_id, ns, peer.token, buckets=sorted(diff),
        )
        remote_index: Dict[_Key, Version] = {
            (r[0], r[1], r[2]): tuple(r[3]) for r in remote_rows
        }
        local_sub = {
            k: v for k, v in local_index.items() if merkle.bucket_of(k, n) in diff
        }

        to_pull = [
            [d, o, s] for (d, o, s), rv in remote_index.items()
            if version_newer(rv, local_sub.get((d, o, s)))
        ]
        pulled = 0
        if to_pull:
            for raw in self._transport.pull(peer.address, self.node_id, to_pull, peer.token):
                if self.apply_inbound(SyncEnvelope.model_validate(raw)):
                    pulled += 1

        push_envs = []
        for key, lv in local_sub.items():
            if version_newer(lv, remote_index.get(key)):
                env = self._build_envelope_for_key(key)
                if env is not None:
                    push_envs.append(env.model_dump(mode="json"))
        pushed = 0
        if push_envs:
            res = self._transport.updates(peer.address, self.node_id, push_envs, peer.token)
            pushed = res.get("accepted", 0)

        logger.info("cluster: reconciled with %s (pulled=%d pushed=%d)",
                    peer.node_id, pulled, pushed)
        return {"pulled": pulled, "pushed": pushed}

    def list_peers(self) -> List[Peer]:
        with self._lock:
            return list(self._sessions.values())

    def gc_tombstones(self, now_ms: Optional[int] = None) -> int:
        """Drop tombstones older than the retention window
        (``tombstone_retention``) — local (persisted) and foreign (overlay).
        Returns the number removed.

        Retention ≥ the HOLD eviction window guarantees any peer that could
        still hold a stale copy has already evicted it before we forget the
        deletion, so GC can't cause a resurrection.
        """
        if now_ms is None:
            now_ms = time.time_ns() // 1_000_000
        retention_ms = int(self._config.tombstone_retention * 1000)
        removed = 0
        with self._lock:
            for k, t in list(self._state.tombstones.items()):
                if now_ms - t.deleted_at_ms > retention_ms:
                    del self._state.tombstones[k]
                    removed += 1
            if removed:
                self._state.save()
            for key, env in list(self._foreign.items()):
                if env.tombstone and now_ms - env.version[0] > retention_ms:
                    del self._foreign[key]
                    removed += 1
        return removed

    def disconnect_peer(self, node_id: str) -> bool:
        """The single eviction path. Drop the session, evict every record
        that originated at that peer, and start a suppression cooldown.

        Full-mesh: a peer's origin is always a direct session, so losing the
        link (explicit ``rm-peer`` or HOLD timeout) is exactly the signal to
        evict its records. The cooldown stops anti-entropy from re-pulling
        them from another node that hasn't evicted yet (which would ping-pong
        and never converge); a fresh session (``handle_open`` / ``connect_peer``)
        lifts it.
        """
        with self._lock:
            existed = self._sessions.pop(node_id, None) is not None
            for k in [k for k in self._foreign if k[1] == node_id]:
                del self._foreign[k]
            self._evicted_until[node_id] = (
                self._clock() + self._config.tombstone_retention
            )
        return existed

    # ── read seams (dataset router merge calls these; wired in M5) ───────

    def foreign_wrapped(self, dataset: str) -> List[dict]:
        """Wrapped-output dicts for replicated live records in ``dataset``,
        with a namespaced ``id`` (``origin_id:service_id``) + ``origin_id``."""
        out: List[dict] = []
        with self._lock:
            for (ds, origin, sid), env in self._foreign.items():
                if ds != dataset or env.tombstone or not env.payload:
                    continue
                wrapped = env.payload.get("wrapped")
                if not wrapped:
                    continue
                row = dict(wrapped)
                row["id"] = f"{origin}:{sid}"
                row["origin_id"] = origin
                out.append(row)
        return out

    def foreign_rows(self, dataset: str) -> List[dict]:
        """Replicated live records in ``dataset`` as ``{"entry", "wrapped"}``
        pairs for the list endpoint: ``entry`` (a RegistryEntry) feeds the
        existing filter pipeline; ``wrapped`` is the output row with a
        namespaced id + ``origin_id``."""
        from a2x_registry.register.models import RegistryEntry
        with self._lock:
            items = [
                (k, env) for k, env in self._foreign.items()
                if k[0] == dataset and not env.tombstone and env.payload
            ]
        out: List[dict] = []
        for (ds, origin, sid), env in items:
            try:
                entry = RegistryEntry.model_validate(env.payload["entry"])
            except Exception:  # noqa: BLE001 — skip a malformed replica
                continue
            wrapped = dict(env.payload["wrapped"])
            wrapped["id"] = f"{origin}:{sid}"
            wrapped["origin_id"] = origin
            out.append({"entry": entry, "wrapped": wrapped})
        return out

    def foreign_entry(self, dataset: str, display_id: str):
        """Resolve a namespaced ``origin_id:service_id`` to its replicated
        wrapped record, or None. Used by the single-get endpoint."""
        if ":" not in display_id:
            return None
        origin, _, sid = display_id.partition(":")
        with self._lock:
            env = self._foreign.get((dataset, origin, sid))
        if env is None or env.tombstone or not env.payload:
            return None
        wrapped = dict(env.payload["wrapped"])
        wrapped["id"] = display_id
        wrapped["origin_id"] = origin
        return wrapped

    # ── local mutation hook (wired via RegistryService.set_on_mutation) ──

    def on_local_mutation(self, dataset: str, service_id: str, op: str, entry) -> None:
        """Called after every successful local CRUD. Stamps a new version on
        the record (a tombstone on deregister), persists, and pushes the
        delta to all peers that sync this namespace.

        origin-only: we only ever stamp records we own, so versions stay
        monotonic per record and there's no write-write conflict.
        """
        with self._lock:
            ts = self._next_ts()
            k = make_key(dataset, service_id)
            if op == "deregister":
                self._state.tombstones[k] = Tombstone(
                    version=(ts, self.node_id), deleted_at_ms=ts,
                )
                self._state.local_versions.pop(k, None)
            else:  # register | update
                self._state.local_versions[k] = [ts, self.node_id]
                self._state.tombstones.pop(k, None)  # un-tombstone on re-register
            self._state.save()

        env = self._build_local_envelope(dataset, service_id)
        if env is not None:
            self._broadcast(env)

    # ── observability ───────────────────────────────────────────────────

    def state_summary(self) -> dict:
        with self._lock:
            foreign_by_ns: Dict[str, int] = {}
            for (ds, _o, _s), env in self._foreign.items():
                if not env.tombstone:
                    foreign_by_ns[ds] = foreign_by_ns.get(ds, 0) + 1
            return {
                "node_id": self.node_id,
                "advertise": self._advertise,
                "peers": [p.to_summary() for p in self._sessions.values()],
                "foreign_records": sum(foreign_by_ns.values()),
                "foreign_by_namespace": foreign_by_ns,
                "local_records": len(self._state.local_versions),
                "tombstones": len(self._state.tombstones),
            }
