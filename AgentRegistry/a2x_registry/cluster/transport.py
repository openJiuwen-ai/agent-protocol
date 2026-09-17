"""Peer transport — the RPC boundary between registry instances.

``Transport`` is a small interface with one method per cluster RPC. Two
implementations:

  - ``HttpTransport`` — production; calls the peer's ``/api/cluster/*``
    endpoints over HTTP (``trust_env=False`` so a system proxy can't
    intercept localhost).
  - tests provide an in-process transport that routes calls straight to
    the target ``ClusterStore``'s handler methods, so two instances can be
    exercised in one process without real servers or the module singleton.

Every method takes the peer's base ``address`` (e.g. ``http://host:port``)
and returns parsed JSON. Transport errors propagate as ``TransportError``
so the caller can treat an unreachable peer as best-effort.
"""

from __future__ import annotations

import threading
from typing import Any, List, Optional

# Header carrying the per-session secret on every authenticated RPC.
SESSION_HEADER = "X-Cluster-Session"

# Keep-alive socket pool ceiling. A full mesh holds ~N-1 connections per
# node; reusing them (vs. a new TCP handshake per call) is essential at
# scale. Operators with very large clusters can raise this + the OS fd limit.
_MAX_KEEPALIVE_CONNECTIONS = 512


class TransportError(Exception):
    """Raised when a peer call fails (unreachable / non-2xx)."""


class Transport:
    """Interface — see module docstring. ``token`` is the per-session secret
    (None on no-auth clusters)."""

    def open(self, address: str, body: dict) -> dict:
        raise NotImplementedError

    def merkle(self, address: str, from_node: str, namespaces: List[str],
               token: Optional[str] = None) -> dict:
        raise NotImplementedError

    def digest(self, address: str, from_node: str, namespaces: List[str],
               token: Optional[str] = None,
               buckets: Optional[List[int]] = None) -> list:
        raise NotImplementedError

    def pull(self, address: str, from_node: str, keys: List[list],
             token: Optional[str] = None) -> list:
        raise NotImplementedError

    def updates(self, address: str, from_node: str, envelopes: List[dict],
                token: Optional[str] = None) -> dict:
        raise NotImplementedError

    def keepalive(self, address: str, from_node: str,
                  token: Optional[str] = None) -> dict:
        raise NotImplementedError

    # ── membership control plane ────────────────────────────────────────

    def join(self, address: str, body: dict) -> dict:
        """Pull a node into a cluster (R → M). Carries cluster_id + roster."""
        raise NotImplementedError

    def evict(self, address: str, body: dict) -> dict:
        """Notify a node it has been removed from the cluster (R → M)."""
        raise NotImplementedError

    def evict_self(self, address: str, body: dict) -> dict:
        """Notify a peer that the sender is gracefully leaving (M → old)."""
        raise NotImplementedError

    def set_digest(self, address: str, from_node: str,
                   token: Optional[str] = None) -> dict:
        """Fetch a peer's roster version map (anti-entropy)."""
        raise NotImplementedError

    def set_pull(self, address: str, from_node: str, node_ids: List[str],
                 token: Optional[str] = None) -> list:
        """Fetch full membership records by node id (anti-entropy)."""
        raise NotImplementedError

    def set_sync(self, address: str, from_node: str, records: List[dict],
                 token: Optional[str] = None) -> dict:
        """Push membership records to a peer to LWW-merge (immediate push)."""
        raise NotImplementedError


class HttpTransport(Transport):
    """Production transport over the peer's REST endpoints.

    Holds a single long-lived ``httpx.Client`` with a keep-alive connection
    pool, reused across calls and threads (httpx clients are thread-safe).
    This avoids a fresh TCP (and TLS) handshake per RPC — the dominant cost
    in a full mesh that fans out to every peer each period.
    """

    def __init__(self, timeout: float = 5.0) -> None:
        self._timeout = timeout
        self._client = None
        self._client_lock = threading.Lock()
        self._closed = False

    def _get_client(self):
        client = self._client
        if client is None:
            with self._client_lock:
                if self._closed:
                    raise TransportError("transport closed")
                if self._client is None:
                    import httpx
                    # trust_env=False → ignore system proxies (localhost
                    # interception). Keep-alive pool sized for a full mesh.
                    self._client = httpx.Client(
                        trust_env=False, timeout=self._timeout,
                        limits=httpx.Limits(
                            max_connections=None,
                            max_keepalive_connections=_MAX_KEEPALIVE_CONNECTIONS,
                        ),
                    )
                client = self._client
        return client

    def _call(self, address: str, method: str, path: str,
              token: Optional[str] = None, **kw) -> Any:
        import httpx

        url = address.rstrip("/") + path
        if token is not None:
            kw.setdefault("headers", {})[SESSION_HEADER] = token
        try:
            resp = self._get_client().request(method, url, **kw)
        except httpx.HTTPError as exc:
            raise TransportError(f"{method} {url} failed: {exc}") from exc
        if resp.status_code // 100 != 2:
            raise TransportError(f"{method} {url} → {resp.status_code}: {resp.text}")
        return resp.json()

    def close(self) -> None:
        """Close the pooled client (server shutdown). Idempotent; blocks a
        post-close lazy re-init via the ``_closed`` flag."""
        with self._client_lock:
            self._closed = True
            if self._client is not None:
                self._client.close()
                self._client = None

    def open(self, address: str, body: dict) -> dict:
        return self._call(address, "POST", "/api/cluster/sessions", json=body)

    def merkle(self, address, from_node, namespaces, token=None) -> dict:
        return self._call(
            address, "GET", "/api/cluster/merkle", token=token,
            params={"from_node": from_node, "namespaces": ",".join(namespaces)},
        )

    def digest(self, address, from_node, namespaces, token=None, buckets=None) -> list:
        params = {"from_node": from_node, "namespaces": ",".join(namespaces)}
        if buckets is not None:
            params["buckets"] = ",".join(str(b) for b in buckets)
        return self._call(
            address, "GET", "/api/cluster/digest", token=token, params=params,
        )

    def pull(self, address, from_node, keys, token=None) -> list:
        return self._call(
            address, "POST", "/api/cluster/pulls", token=token,
            json={"from_node": from_node, "keys": keys},
        )

    def updates(self, address, from_node, envelopes, token=None) -> dict:
        return self._call(
            address, "POST", "/api/cluster/updates", token=token,
            json={"from_node": from_node, "envelopes": envelopes},
        )

    def keepalive(self, address, from_node, token=None) -> dict:
        return self._call(
            address, "POST", "/api/cluster/keepalives", token=token,
            json={"from_node": from_node},
        )

    # ── membership control plane ────────────────────────────────────────

    def join(self, address, body) -> dict:
        return self._call(address, "POST", "/api/cluster/join", json=body)

    def evict(self, address, body) -> dict:
        return self._call(address, "POST", "/api/cluster/evicted", json=body)

    def evict_self(self, address, body) -> dict:
        return self._call(address, "POST", "/api/cluster/leave", json=body)

    def set_digest(self, address, from_node, token=None) -> dict:
        return self._call(
            address, "GET", "/api/cluster/set/digest", token=token,
            params={"from_node": from_node},
        )

    def set_pull(self, address, from_node, node_ids, token=None) -> list:
        return self._call(
            address, "POST", "/api/cluster/set/pull", token=token,
            json={"from_node": from_node, "node_ids": node_ids},
        )

    def set_sync(self, address, from_node, records, token=None) -> dict:
        return self._call(
            address, "POST", "/api/cluster/set/sync", token=token,
            json={"from_node": from_node, "records": records},
        )
