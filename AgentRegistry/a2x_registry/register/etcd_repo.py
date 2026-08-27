"""EtcdTableRepo -- etcd-backed implementation of the ``TableRepo`` interface.

Stores rows as JSON at ``{namespace}/{registry}/{service_id}`` and registry
metadata (name -> kind) at ``{namespace}/_meta/{registry}``. Behaviour is kept
equivalent to ``RegistryTableService`` (the SQL backend):

- ``register`` = whole-row JSON write (``created_at`` preservation is the
  caller's job -- the service layer reads the old row and merges before
  registering, matching A#3).
- ``patch`` = read -> merge fields -> write.
- ``query`` / ``query_paginated`` = range the registry prefix, then filter /
  sort / slice in the application layer (image/instance data are small, so a
  full scan in memory is fine; ``data.<key>`` sort fields read from the row's
  ``data`` dict, mirroring the SQL ``json_extract``).
- ``query_paginated.order_by`` is a ``Sequence[str]`` of ``"<field> <asc|desc>"``
  (see ``TableRepo``), ``exclude_nodes`` drops rows by node, ``limit/offset``
  paginate and ``total`` is the filtered count before slicing.

Composition (``set_default``, ``register_image`` created_at merge, …) stays in
the service layer using only these primitives, so the two backends share code.

Deviations from the plan (documented): ``txn``-based atomicity where it matters --
``create_registry`` uses ``create`` (put-if-not-exists) so a registry key is
never re-created; ``patch`` uses a ``mod_revision`` CAS optimistic lock so a
stale read cannot clobber a concurrent update, raising
:class:`~a2x_registry.register.etcd_client.EtcdError` on conflict.
Infrastructure errors surface as the same ``EtcdError``.
"""

from __future__ import annotations

import re
from typing import Dict, List, Optional, Sequence, Tuple

from ..common.ids import now_iso
from .errors import NotFoundError, ValidationError
from .etcd_client import DEFAULT_NAMESPACE, META_MARK, EtcdClient, EtcdError, _prefix_range_end
from .service import _KIND_PROMOTED, _VALID_KINDS

# registry names are user-chosen in practice but ``_`` is reserved for the
# ``_meta`` metadata segment (see ``META_MARK``), so exclude it to avoid a key
# collision between ``{ns}/_meta/...`` and ``{ns}/{registry}/...``.
_FORBIDDEN_REGISTRY_RE = re.compile(r"^_")

# order_by item         ::= field WS direction?
# field                 ::= promoted_column | "data." JSON key
_ORDER_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_.]*)(?:\s+(asc|desc))?\s*$", re.I)


def _parse_order_item(item: str) -> Tuple[str, bool]:
    """Parse a ``"<field> <asc|desc>"`` order item -> ``(field, desc)``.

    ``field`` may be a promoted column or a ``data.<key>`` reference. A missing
    / unknown direction defaults to ascending.
    """
    m = _ORDER_RE.match(item)
    if not m:
        raise ValidationError(f"invalid order_by item: {item!r}")
    field, direction = m.group(1), m.group(2)
    return field, (direction or "asc").lower() == "desc"


def _extract_value(row: dict, field: str):
    """Read a sort field from a row: top-level column or ``data.<key>``."""
    if field.startswith("data."):
        return (row.get("data") or {}).get(field[len("data."):])
    return row.get(field)


def _sort_tuple(value):
    """Orderable key for one sort field with ``None``-last-by-direction semantics.

    Returns a 2-tuple so that ``None`` never collides with real values, and
    lets ``reverse=desc`` place ``None`` last for DESC (SQL: NULL last) while
    ``None`` sorts first for ASC (SQL: NULL first).
    """
    if value is None:
        return 0, 0
    return 1, value


def _matches(row: dict, match_filter: Optional[dict]) -> bool:
    if not match_filter:
        return True
    return all(row.get(k) == v for k, v in match_filter.items())


class EtcdTableRepo:
    """etcd implementation of :class:`~a2x_registry.register.table_repo.TableRepo`."""

    __slots__ = ("_client", "_ns")

    def __init__(self, client: EtcdClient) -> None:
        self._client = client
        self._ns = client.namespace or DEFAULT_NAMESPACE

    # ------------------------------------------------------------------
    # key helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _row_key(registry: str, service_id: str) -> str:
        return f"{registry}/{service_id}"

    @staticmethod
    def _meta_key(registry: str) -> str:
        return f"{META_MARK}/{registry}"

    def _require_kind(self, name: str) -> str:
        kind = self.get_kind(name)
        if kind is None:
            raise NotFoundError(f"registry '{name}' not found")
        return kind

    def _validate_registry_name(self, name: str) -> None:
        if _FORBIDDEN_REGISTRY_RE.match(name):
            raise ValidationError(
                f"registry name must not start with '_' (reserved): {name!r}"
            )

    def _assert_service_id(self, entry: dict) -> str:
        sid = entry.get("service_id")
        if not sid:
            raise ValidationError("entry missing service_id")
        return sid

    # ------------------------------------------------------------------
    # registry metadata
    # ------------------------------------------------------------------

    def create_registry(self, name: str, kind: str) -> None:
        """Idempotently declare ``name -> kind`` in ``_meta/{name}``.

        Atomic put-if-not-exists via a ``txn`` ``create``: an existing registry
        is left untouched (and never re-created / overwritten). Unknown kind
        raises ValidationError; an ``_``-prefixed name is reserved.
        """
        if kind not in _VALID_KINDS:
            raise ValidationError(f"unknown kind: {kind!r}")
        self._validate_registry_name(name)
        self._client.create(self._meta_key(name), kind)

    def get_kind(self, name: str) -> Optional[str]:
        value = self._client.get(self._meta_key(name))
        return value if isinstance(value, str) else None

    def list_registries(self) -> Dict[str, str]:
        out: Dict[str, str] = {}
        for key, value in self._client.range(f"{META_MARK}/"):
            name = key[len(f"{META_MARK}/"):]
            if isinstance(value, str):
                out[name] = value
        return out

    # ------------------------------------------------------------------
    # row CRUD
    # ------------------------------------------------------------------

    def register(self, name: str, entry: dict) -> dict:
        _ = self._require_kind(name)
        sid = self._assert_service_id(entry)
        key = self._row_key(name, sid)
        existing = self.get(name, sid)
        merged = dict(entry)
        if existing is not None and isinstance(existing, dict):
            if "created_at" in merged or "created_at" in existing:
                merged["created_at"] = existing.get("created_at") or merged.get("created_at")
            if "updated_at" in merged:
                merged["updated_at"] = now_iso()
        self._client.put(key, merged)
        return self.get(name, sid)

    def get(self, name: str, service_id: str) -> Optional[dict]:
        value = self._client.get(self._row_key(name, service_id))
        return value if isinstance(value, dict) else None

    def patch(self, name: str, service_id: str, fields: dict) -> dict:
        kind = self._require_kind(name)
        if not fields:
            raise ValidationError("fields must not be empty")

        allowed = set(_KIND_PROMOTED[kind]) | {"data"}
        for col in fields:
            if col not in allowed:
                raise ValidationError(f"cannot patch unknown column: {col!r}")

        key = self._row_key(name, service_id)
        pair = self._client.get_rev(key)
        if pair is None:
            raise NotFoundError(
                f"{kind} '{service_id}' not found in registry '{name}'"
            )
        current, mod_revision = pair
        merged = dict(current)
        for col, value in fields.items():
            if col == "data":
                merged["data"] = value
            else:
                merged[col] = value
        committed = self._client.put(key, merged, mod_revision=mod_revision)
        if not committed:
            raise EtcdError(
                f"concurrent modification on {kind} '{service_id}' "
                f"in registry '{name}' (CAS aborted)"
            )
        return self.get(name, service_id)

    def deregister(self, name: str, service_id: str) -> bool:
        return self._client.delete(self._row_key(name, service_id))

    # ------------------------------------------------------------------
    # read
    # ------------------------------------------------------------------

    def _rows(self, name: str) -> List[dict]:
        return [v for _, v in self._client.range(f"{name}/") if isinstance(v, dict)]

    def query(self, name: str, query_filter: Optional[dict] = None) -> List[dict]:
        kind = self.get_kind(name)
        if kind is None:
            return []
        if query_filter is not None:
            allowed = set(_KIND_PROMOTED[kind]) | {"service_id"}
            for col in query_filter:
                if col not in allowed:
                    raise ValidationError(f"cannot filter on unknown column: {col!r}")
        return [row for row in self._rows(name) if _matches(row, query_filter)]

    def query_paginated(
        self,
        name: str,
        query_filter: Optional[dict] = None,
        exclude_nodes: Optional[List[str]] = None,
        only_status: Optional[str] = None,
        order_by: Sequence[str] = (),
        limit: int = -1,
        offset: int = 0,
    ) -> Tuple[List[dict], int]:
        kind = self.get_kind(name)
        if kind is None:
            return [], 0
        rows = self._rows(name)

        # filter (equality on promoted / service_id)
        if query_filter is not None:
            allowed = set(_KIND_PROMOTED[kind]) | {"service_id"}
            for col in query_filter:
                if col not in allowed:
                    raise ValidationError(f"cannot filter on unknown column: {col!r}")
            rows = [r for r in rows if _matches(r, query_filter)]

        # exclude unhealthy nodes
        if exclude_nodes:
            rows = [r for r in rows if (r.get("node") or "") not in set(exclude_nodes)]

        # keep only rows with the given persisted data.status (missing → 运行)
        if only_status is not None:
            rows = [
                r for r in rows
                if ((r.get("data") or {}).get("status") or "运行") == only_status
            ]

        total = len(rows)

        # structured ordering (stable sorts from least- to most-significant)
        for item in reversed(list(order_by)):
            field, desc = _parse_order_item(item)
            rows.sort(
                key=lambda r: _sort_tuple(_extract_value(r, field)),
                reverse=desc,
            )

        # paginate
        if limit > 0:
            rows = rows[offset:offset + limit]
        return rows, total


__all__ = ["EtcdTableRepo", "_prefix_range_end"]