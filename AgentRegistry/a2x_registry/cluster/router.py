"""FastAPI router for cluster endpoints (``/api/cluster/*``), RESTful.

Every route depends on ``require_cluster_store`` so the whole surface
returns 404 when the cluster module isn't initialized. Handlers are thin:
they delegate to ``ClusterStore`` methods (the same methods the in-process
test transport calls), keeping the HTTP layer free of sync logic.
"""

from __future__ import annotations

import logging
from typing import List, Optional

from fastapi import APIRouter, Depends, Header, HTTPException, Query
from pydantic import BaseModel

from .deps import require_cluster_store
from .store import ClusterStore
from .transport import SESSION_HEADER, TransportError

# FastAPI maps the X-Cluster-Session header to this dependency name.
_SessionToken = Header(default=None, alias=SESSION_HEADER)

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/api/cluster", tags=["cluster"])


# ── request models ───────────────────────────────────────────────────────

class AddPeerRequest(BaseModel):
    address: str
    namespaces: Optional[List[str]] = None
    token: Optional[str] = None


class OpenRequest(BaseModel):
    node_id: str
    address: str = ""
    namespaces: List[str] = []
    token: Optional[str] = None


class PullRequest(BaseModel):
    from_node: str
    keys: List[list]


class UpdatesRequest(BaseModel):
    from_node: str
    envelopes: List[dict]


class KeepaliveRequest(BaseModel):
    from_node: str


class SetAddRequest(BaseModel):
    members: List[dict]          # [{address, node_id?}]
    token: Optional[str] = None


class SetRemoveRequest(BaseModel):
    members: List[dict]          # [{node_id}]


class JoinRequest(BaseModel):
    cluster_id: str
    roster: List[dict] = []
    token: Optional[str] = None
    from_node: str = ""
    from_address: str = ""


class EvictRequest(BaseModel):
    from_node: str
    cluster_id: Optional[str] = None
    token: Optional[str] = None


class LeaveRequest(BaseModel):
    from_node: str
    token: Optional[str] = None


class SetPullRequest(BaseModel):
    from_node: str
    node_ids: List[str]


class SetSyncRequest(BaseModel):
    from_node: str
    records: List[dict]


def _require_membership(store: ClusterStore):
    m = store.membership
    if m is None:
        raise HTTPException(status_code=404, detail="Membership control plane not available")
    return m


# ── trigger / session management ─────────────────────────────────────────

@router.post("/peers")
def add_peer(req: AddPeerRequest, store: ClusterStore = Depends(require_cluster_store)):
    """Discover-and-connect trigger: open a session with the peer at
    ``address`` and run an initial reconcile. Called by the local CLI
    (``cluster add-peer``) or the link-layer daemon."""
    try:
        peer = store.connect_peer(req.address, req.namespaces, req.token)
    except TransportError as exc:
        raise HTTPException(status_code=502, detail=f"peer unreachable: {exc}")
    return {"peer": peer.to_summary()}


@router.get("/peers")
def list_peers(store: ClusterStore = Depends(require_cluster_store)):
    return {"peers": store.state_summary()["peers"]}


@router.delete("/peers/{node_id}")
def remove_peer(node_id: str, store: ClusterStore = Depends(require_cluster_store)):
    removed = store.disconnect_peer(node_id)
    return {"node_id": node_id, "removed": removed}


# ── peer-facing sync endpoints ───────────────────────────────────────────

@router.post("/sessions")
def open_session(req: OpenRequest, store: ClusterStore = Depends(require_cluster_store)):
    """Receive an OPEN handshake from a peer (per-namespace authorization)."""
    return store.handle_open(req.model_dump())


@router.get("/merkle")
def get_merkle(
    from_node: str = Query(...),
    namespaces: str = Query(""),
    session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    """Bucket-hash digest for anti-entropy (O(buckets) fast path)."""
    ns = [n for n in namespaces.split(",") if n] if namespaces else None
    return store.serve_merkle(from_node, ns, session)


@router.get("/digest")
def get_digest(
    from_node: str = Query(...),
    namespaces: str = Query(""),
    buckets: str = Query(""),
    session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    ns = [n for n in namespaces.split(",") if n] if namespaces else None
    bk = [int(b) for b in buckets.split(",") if b] if buckets else None
    return store.serve_digest(from_node, ns, session, buckets=bk)


@router.post("/pulls")
def post_pulls(
    req: PullRequest, session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    return store.serve_pull(req.from_node, req.keys, session)


@router.post("/updates")
def post_updates(
    req: UpdatesRequest, session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    return store.serve_updates(req.from_node, req.envelopes, session)


@router.post("/keepalives")
def post_keepalive(
    req: KeepaliveRequest, session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    return store.handle_keepalive(req.from_node, session)


# ── membership control plane (user-facing) ───────────────────────────────

@router.post("/set/add")
def set_add(req: SetAddRequest, store: ClusterStore = Depends(require_cluster_store)):
    """Declaratively add members to this node's cluster (primary user
    trigger). Mints a cluster on first use; bootstraps each new member."""
    return _require_membership(store).set_add(req.members, req.token)


@router.post("/set/remove")
def set_remove(req: SetRemoveRequest, store: ClusterStore = Depends(require_cluster_store)):
    return _require_membership(store).set_remove(req.members)


@router.get("/set")
def get_set(store: ClusterStore = Depends(require_cluster_store)):
    return _require_membership(store).show()


# ── membership protocol (peer → us) ──────────────────────────────────────

@router.post("/join")
def post_join(req: JoinRequest, store: ClusterStore = Depends(require_cluster_store)):
    """A peer pulls us into its cluster (auth-gated adopt)."""
    return _require_membership(store).handle_join(req.model_dump())


@router.post("/evicted")
def post_evicted(req: EvictRequest, store: ClusterStore = Depends(require_cluster_store)):
    """We were removed from the cluster → revert to standalone."""
    return _require_membership(store).handle_evicted(req.model_dump())


@router.post("/leave")
def post_leave(req: LeaveRequest, store: ClusterStore = Depends(require_cluster_store)):
    """A peer is gracefully leaving our cluster → tombstone + drop it."""
    return _require_membership(store).handle_evict_self(req.model_dump())


@router.get("/set/digest")
def get_set_digest(
    from_node: str = Query(...), session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    return _require_membership(store).serve_set_digest(from_node, session)


@router.post("/set/pull")
def post_set_pull(
    req: SetPullRequest, session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    return _require_membership(store).serve_set_pull(req.from_node, req.node_ids, session)


@router.post("/set/sync")
def post_set_sync(
    req: SetSyncRequest, session: str = _SessionToken,
    store: ClusterStore = Depends(require_cluster_store),
):
    return _require_membership(store).serve_set_sync(req.from_node, req.records, session)


# ── observability ────────────────────────────────────────────────────────

@router.get("/state")
def get_state(store: ClusterStore = Depends(require_cluster_store)):
    """Return this instance's node id and a snapshot of sync state."""
    return store.state_summary()
