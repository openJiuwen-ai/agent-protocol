"""etcd v3 HTTP/JSON gateway client (stdlib-only: urllib + ssl, no deps).

Talks to etcd's grpc-gateway JSON API (``/v3/kv/{range,put,deleterange,txn}``),
key/value base64-encoded on the wire (matches the yuanrong smoke test and the
``ETCD兼容分析.md`` C#11 choice of ``urllib``).

Atomicity:
- ``create`` = ``txn`` with ``create_revision == 0`` (put-if-not-exists), so
  ``create_registry`` neither re-creates nor races.
- ``put(mod_revision=...)`` = ``txn`` CAS guarded on the key's ``mod_revision``
  (optimistic lock for ``patch``).

Scope / simplifications (documented deliberately):
- **endpoint = a single URL**; no failover.
- **protocol**: config-driven. With ``ca+cert+key`` -> HTTPS + mutual TLS;
  without -> plain HTTP, no auth. Endpoint scheme and the cert config must
  agree (validated by the caller in ``startup._resolve_db_config``).
- All keys live under ``{namespace}/`` (prefix isolation, no RBAC).
"""

from __future__ import annotations

import base64
import json
import ssl
import urllib.error
import urllib.request
from typing import Any, List, Optional, Tuple

DEFAULT_TIMEOUT = 5.0
DEFAULT_NAMESPACE = "a2x-registry"

# Reserved first path segment under the namespace for registry metadata
# (``{namespace}/_meta/{registry}``). A registry whose name starts with ``_``
# would collide, so ``create_registry`` rejects such names.
META_MARK = "_meta"


class EtcdError(RuntimeError):
    """etcd infrastructure failure: unreachable / timeout / quota / bad request.

    Raised by :class:`EtcdClient` on network or HTTP errors; the repo lets it
    propagate so API-layer mapping (e.g. HTTP 502) can be added upstream.
    """


def _prefix_range_end(prefix: bytes) -> bytes:
    """Build an etcd ``range_end`` for a get-by-prefix by incrementing the last
    byte of ``prefix``.

    Works because keys under ``{namespace}/{registry}/`` are followed by
    alphanumeric service ids that sort *after* the ``/`` (0x2f) terminator, so
    ``prefix[:-1] + chr(prefix[-1]+1)`` bounds exactly the namespace-relative
    prefix. Callers pass UTF-8 bytes.
    """
    if not prefix:
        raise ValueError("prefix must be non-empty to compute range_end")
    return prefix[:-1] + bytes([prefix[-1] + 1])


class EtcdClient:
    """Minimal etcd KV client over the v3 grpc-gateway JSON API.

    All public methods take a **namespace-relative key** (e.g. ``"images/abc"``
    or ``"_meta/images"``); the client attaches the ``{namespace}/`` prefix.
    Values are JSON-serialized: rows are dicts, registry metadata is a bare
    string (kind).
    """

    __slots__ = ("endpoint", "namespace", "timeout", "_context")

    def __init__(
        self,
        endpoint: str,
        namespace: str = DEFAULT_NAMESPACE,
        ca: str = "",
        cert: str = "",
        key: str = "",
        timeout: float = DEFAULT_TIMEOUT,
    ) -> None:
        if not endpoint.startswith(("http://", "https://")):
            raise ValueError(
                f"etcd endpoint must be http(s)://, got {endpoint!r}"
            )
        self.endpoint = endpoint.rstrip("/")
        self.namespace = namespace.strip("/") or DEFAULT_NAMESPACE
        self.timeout = timeout
        self._context = self._build_context(endpoint, ca, cert, key)

    @staticmethod
    def _build_context(endpoint: str, ca: str, cert: str, key: str) -> Optional[ssl.SSLContext]:
        """Return an SSLContext for ``https://`` endpoints, else ``None``.

        - ``ca+cert+key`` all set -> mutual TLS (verify server CA + present a
          client cert).
        - ``https`` without cert/key -> server-cert verification only.
        - ``http`` -> ``None`` (plain).
        """
        if not endpoint.startswith("https://"):
            return None
        if cert and key:
            if not ca:
                raise ValueError("etcd https + client cert requires ca too")
            ctx = ssl.create_default_context(cafile=ca)
            ctx.load_cert_chain(certfile=cert, keyfile=key)
            return ctx
        return ssl.create_default_context()

    # ------------------------------------------------------------------
    # low-level request
    # ------------------------------------------------------------------

    def _post(self, path: str, body: dict) -> dict:
        url = f"{self.endpoint}{path}"
        data = json.dumps(body).encode("utf-8")
        req = urllib.request.Request(
            url, data=data, method="POST",
            headers={"Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(
                req, timeout=self.timeout, context=self._context,
            ) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            raise EtcdError(
                f"etcd HTTP {exc.code} on {path}: "
                f"{exc.read().decode('utf-8', errors='replace')}"
            ) from exc
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            raise EtcdError(
                f"etcd unreachable on {self.endpoint}{path}: {exc}"
            ) from exc

    def _get_kv(self, key: str) -> Optional[dict]:
        """Fetch a single key's raw ``{key,value,mod_revision,...}`` KV or None."""
        resp = self._post("/v3/kv/range", {"key": self._b64(self._full_key(key))})
        kvs = resp.get("kvs") or []
        return kvs[0] if kvs else None

    def _txn(self, compare: list, success: list, failure: list) -> dict:
        """Run a ``/v3/kv/txn`` transaction and return the response dict."""
        return self._post("/v3/kv/txn", {
            "compare": compare, "success": success, "failure": failure,
        })

    def _b64(self, raw: bytes) -> str:
        return base64.b64encode(raw).decode("ascii")

    def _full_key(self, key: str) -> bytes:
        return f"{self.namespace}/{key}".encode("utf-8")

    @staticmethod
    def _decode_value(raw: bytes) -> Any:
        return json.loads(raw.decode("utf-8"))

    # ------------------------------------------------------------------
    # public KV primitives (namespace-relative keys)
    # ------------------------------------------------------------------

    def ping(self) -> None:
        """Fail-fast connectivity / TLS / auth probe.

        Ranges the ``_meta/`` prefix; raises ``EtcdError`` if the server is
        unreachable or rejects the request. Used at startup.
        """
        self._post("/v3/kv/range", {"key": self._b64(self._full_key(f"{META_MARK}/"))})

    def get(self, key: str) -> Optional[Any]:
        """Fetch one value by key; ``None`` if the key does not exist.

        Returned value is JSON-decoded (a dict for rows, a bare kind string
        for ``_meta`` entries).
        """
        kv = self._get_kv(key)
        if kv is None:
            return None
        return self._decode_value(base64.b64decode(kv["value"]))

    def get_rev(self, key: str) -> Optional[Tuple[Any, str]]:
        """Return ``(value, mod_revision)`` for one key, or ``None`` if absent.

        ``mod_revision`` is etcd's modification revision (a string), used as
        the ``compare`` guard for optimistic CAS in :meth:`put`.
        """
        kv = self._get_kv(key)
        if kv is None:
            return None
        value = self._decode_value(base64.b64decode(kv["value"]))
        return value, kv["mod_revision"]

    def create(self, key: str, value: Any) -> bool:
        """**Atomically** create ``key`` only if it does not yet exist.

        Uses ``/v3/kv/txn`` with ``create_revision == 0``: writes and returns
        True on first creation; returns False (no write) if the key already
        exists. This makes ``create_registry`` idempotent without a
        read-then-write race.
        """
        k_b64 = self._b64(self._full_key(key))
        v_b64 = self._b64(json.dumps(value).encode("utf-8"))
        resp = self._txn(
            compare=[{
                "key": k_b64,
                "target": "CREATE",
                "create_revision": "0",
                "result": "EQUAL",
            }],
            success=[{"request_put": {"key": k_b64, "value": v_b64}}],
            failure=[],
        )
        return bool(resp.get("succeeded"))

    def put(self, key: str, value: Any, mod_revision: Optional[Any] = None):
        """Store ``value`` (JSON-encoded) at ``key``.

        - ``mod_revision is None`` -> plain ``/v3/kv/put`` (overwrite).
        - ``mod_revision`` given -> ``/v3/kv/txn`` CAS: write only if the key's
          current mod_revision matches, and return whether it succeeded. This
          guards ``patch`` against clobbering a concurrent update.
        """
        k_b64 = self._b64(self._full_key(key))
        v_b64 = self._b64(json.dumps(value).encode("utf-8"))
        if mod_revision is None:
            self._post("/v3/kv/put", {"key": k_b64, "value": v_b64})
            return None
        resp = self._txn(
            compare=[{
                "key": k_b64,
                "target": "MOD",
                "mod_revision": str(mod_revision),
                "result": "EQUAL",
            }],
            success=[{"request_put": {"key": k_b64, "value": v_b64}}],
            failure=[],
        )
        return bool(resp.get("succeeded"))

    def delete(self, key: str) -> bool:
        """Delete a single key; return True if a value was removed."""
        resp = self._post("/v3/kv/deleterange", {"key": self._b64(self._full_key(key))})
        deleted = (resp.get("deleted") or 0)
        return int(deleted) > 0

    def range(self, prefix: str) -> List[Tuple[str, Any]]:
        """Return all keys under ``prefix`` as ``(rel_key, value)`` pairs.

        ``rel_key`` is the namespace-relative key (``prefix`` included), so a
        caller can derive the registry / service id from it. Values are
        JSON-decoded.
        Large result sets are paged transparently: etcd truncates a single
        ``/v3/kv/range`` response to the server's ``--max-request-bytes``
        (default 1.5 MiB) and signals the remainder via ``more`` / ``next_key``,
        so this loops until the whole range is consumed. ``more`` set without a
        ``next_key`` raises ``EtcdError`` rather than returning a silently
        truncated list.
        """
        full_prefix = self._full_key(prefix)
        range_end = _prefix_range_end(full_prefix)
        out: List[Tuple[str, Any]] = []
        ns_prefix = f"{self.namespace}/"
        key = full_prefix
        while True:
            resp = self._post("/v3/kv/range", {
                "key": self._b64(key),
                "range_end": self._b64(range_end),
            })
            for kv in resp.get("kvs") or []:
                abs_key = base64.b64decode(kv["key"]).decode("utf-8")
                rel = abs_key[len(ns_prefix):] if abs_key.startswith(ns_prefix) else abs_key
                out.append((rel, self._decode_value(base64.b64decode(kv["value"]))))
            if not resp.get("more"):
                return out
            next_key = resp.get("next_key")
            if not next_key:
                raise EtcdError(
                    f"etcd range: more=true but next_key missing under prefix {prefix!r}"
                )
            key = base64.b64decode(next_key)