"""InstanceService — instance management business logic.

Pure recorder: register / update / deregister / query / expire_node.
Does not invoke the runtime (元戎) or make decisions for the gateway.

Persistence goes through ``RegistryTableService`` (SQL backend); this
service does not hold a backend/store directly. The ``data`` JSON column
holds ``{address, instance_id, created_at, last_active_at, status}`` —
these are not promoted columns, so ``update_instance`` must merge
``address`` / ``instance_id`` / ``status`` into the existing ``data``
dict before patching.

``status`` (运行 / 停止 / 异常) is persisted inside ``data`` — written by
the gateway via PATCH (据元戎 List), defaulting to 运行 on register. At
query time a node-heartbeat callback injected via ``set_heartbeat_check``
can still override it to 异常 when the node's lease is UNHEALTHY (see
InstanceService._derive_status); when no callback is injected
(standalone, or heartbeat module not loaded) the persisted value is
shown as-is.

``list_instances`` supports pagination (``size``/``page``),
deterministic ordering (``framework, "user", service_id``), and SQL-side
push-down for ``include_unhealthy=False`` via ``only_status`` /
``expired_nodes()``.
"""

from __future__ import annotations

import logging
from typing import Any, Callable, Dict, List, Optional, Tuple

from a2x_registry.common.ids import now_iso
from a2x_registry.register.table_repo import TableRepo

from .errors import InstanceNotFoundError, InstanceValidationError

logger = logging.getLogger(__name__)

INSTANCE_REGISTRY = "instances"

# Accepted instance kinds (OpenAPI enum).
_VALID_KINDS = ("三方", "九问")

# Accepted lifecycle status values (OpenAPI enum). Persisted inside the
# ``data`` JSON and written by the gateway via PATCH (据元戎 List).
_VALID_STATUSES = ("运行", "停止", "异常")

# Required fields for register_instance.
_REQUIRED_FIELDS = (
    "service_id", "kind", "framework", "framework_version",
    "node", "address", "user",
)

# Callback type: (node_ip) -> is_expired
NodeExpiredCheck = Callable[[str], bool]

# Deterministic sort order for instance listing (V2).
_INSTANCE_ORDER = (
    "framework asc",
    "user asc",
    "service_id asc"
)


class InstanceService:
    """Instance management business layer."""

    __slots__ = ("_table_svc", "_is_node_expired", "_expired_nodes_provider")

    def __init__(self, table_svc: TableRepo) -> None:
        self._table_svc = table_svc
        self._is_node_expired: Optional[NodeExpiredCheck] = None
        # Optional provider returning a set of expired node IPs (read-only)
        # for SQL push-down. Set by set_heartbeat_service.
        self._expired_nodes_provider: Optional[Callable[[], set]] = None

    # ------------------------------------------------------------------
    # Heartbeat injection
    # ------------------------------------------------------------------

    def set_heartbeat_check(self, callback: Optional[NodeExpiredCheck]) -> None:
        self._is_node_expired = callback

    def set_heartbeat_service(self, hb) -> None:
        """Inject (or clear) a HeartbeatManager for status derivation."""
        if hb is None:
            self.set_heartbeat_check(None)
            self._expired_nodes_provider = None
        else:
            self.set_heartbeat_check(hb.is_expired)
            self._expired_nodes_provider = hb.expired_nodes

    # ------------------------------------------------------------------
    # register_instance
    # ------------------------------------------------------------------

    def register_instance(self, entry: Dict[str, Any]) -> Dict[str, Any]:
        self._validate_entry(entry)

        sid = entry["service_id"]
        now = now_iso()

        existing = self._table_svc.query(
            INSTANCE_REGISTRY, {"service_id": sid}
        )
        if existing:
            old_data = existing[0].get("data", {}) or {}
            created_at = old_data.get("created_at", now)
        else:
            created_at = now

        # instance_id is the 元戎 instance ID (optional, never a key);
        # status starts at 运行 (registered = launched, per OpenAPI).
        db_entry = {
            "service_id": sid,
            "kind": entry["kind"],
            "framework": entry["framework"],
            "framework_version": entry["framework_version"],
            "node": entry["node"],
            "user": entry["user"],
            "data": {
                "address": entry["address"],
                "instance_id": entry.get("instance_id") or "",
                "created_at": created_at,
                "last_active_at": now,
                "status": "运行",
            },
        }
        stored = self._table_svc.register(INSTANCE_REGISTRY, db_entry)
        logger.info("register_instance %s (node=%s)", sid, entry["node"])
        return self._to_entry(stored)

    # ------------------------------------------------------------------
    # update_instance
    # ------------------------------------------------------------------

    def update_instance(
        self, service_id: str, fields: Dict[str, Any]
    ) -> Dict[str, Any]:
        has_node = fields.get("node") is not None
        has_address = fields.get("address") is not None
        has_instance_id = fields.get("instance_id") is not None
        has_status = fields.get("status") is not None
        if not (has_node or has_address or has_instance_id or has_status):
            raise InstanceValidationError(
                "at least one of node/address/instance_id/status "
                "must be provided"
            )
        if has_status and fields["status"] not in _VALID_STATUSES:
            raise InstanceValidationError(
                f"invalid status: {fields['status']!r}, "
                f"must be one of {_VALID_STATUSES}"
            )

        existing = self._table_svc.query(
            INSTANCE_REGISTRY, {"service_id": service_id}
        )
        if not existing:
            raise InstanceNotFoundError(
                f"instance '{service_id}' not found"
            )

        row = existing[0]
        data = dict(row.get("data", {}) or {})

        patch_fields: Dict[str, Any] = {}
        if has_node:
            patch_fields["node"] = fields["node"]
        if has_instance_id:
            data["instance_id"] = fields["instance_id"]
        if has_status:
            data["status"] = fields["status"]
        if has_address:
            data["address"] = fields["address"]
            data["last_active_at"] = now_iso()
        if has_instance_id or has_status or has_address:
            patch_fields["data"] = data

        updated = self._table_svc.patch(INSTANCE_REGISTRY, service_id, patch_fields)
        logger.info("update_instance %s (fields=%s)", service_id, sorted(patch_fields))
        return self._to_entry(updated)

    # ------------------------------------------------------------------
    # deregister_instance
    # ------------------------------------------------------------------

    def deregister_instance(self, service_id: str) -> Dict[str, Any]:
        deleted = self._table_svc.deregister(INSTANCE_REGISTRY, service_id)
        logger.info(
            "deregister_instance %s (deleted=%s)", service_id, deleted
        )
        return {"service_id": service_id, "deleted": deleted}

    # ------------------------------------------------------------------
    # list_instances (paginated + SQL push-down)
    # ------------------------------------------------------------------

    def list_instances(
        self,
        filter: Optional[Dict[str, Any]] = None,
        include_unhealthy: bool = False,
        size: int = -1,
        page: int = 1,
    ) -> Tuple[List[Dict[str, Any]], int]:
        """Query instances with optional filters, pagination, and status.

        When ``include_unhealthy=False`` (default), unhealthy instances are
        excluded via two push-downs so ``LIMIT/OFFSET`` and
        ``X-Total-Count`` stay correct across backends:
        - ``only_status='运行'`` — rows whose persisted ``data.status`` was
          set to 停止/异常 by the gateway are dropped (legacy rows without
          a stored status default to 运行);
        - ``exclude_nodes`` — instances on heartbeat-UNHEALTHY nodes are
          dropped (derived 异常).

        Returns ``(entries, total)`` — total is the filtered count before
        pagination.
        """
        exclude_nodes: Optional[List[str]] = None
        if not include_unhealthy and self._expired_nodes_provider is not None:
            dead = self._expired_nodes_provider()
            if dead:
                exclude_nodes = sorted(dead)

        only_status: Optional[str] = None if include_unhealthy else "运行"

        offset = max(0, (page - 1) * size) if size > 0 else 0
        rows, total = self._table_svc.query_paginated(
            INSTANCE_REGISTRY,
            query_filter=filter or None,
            exclude_nodes=exclude_nodes,
            only_status=only_status,
            order_by=_INSTANCE_ORDER,
            limit=size if size > 0 else -1,
            offset=offset,
        )
        entries = [self._to_entry(r) for r in rows]

        # Fallback: when _expired_nodes_provider is not set but
        # _is_node_expired is (e.g. tests using set_heartbeat_check
        # directly), filter heartbeat-unhealthy entries in memory.
        # (Persisted 停止/异常 rows are already excluded by only_status.)
        if (not include_unhealthy
                and self._expired_nodes_provider is None
                and self._is_node_expired is not None):
            entries = [e for e in entries if e["status"] != "异常"]
            total = len(entries) if size <= 0 else total
        return entries, total

    # ------------------------------------------------------------------
    # expire_node
    # ------------------------------------------------------------------

    def expire_node(self, node: str) -> None:
        rows = self._table_svc.query(INSTANCE_REGISTRY, {"node": node})
        for row in rows:
            self._table_svc.deregister(INSTANCE_REGISTRY, row["service_id"])
        logger.info("expire_node %s (removed=%d)", node, len(rows))

    # ------------------------------------------------------------------
    # distinct_nodes
    # ------------------------------------------------------------------

    def distinct_nodes(self) -> List[str]:
        rows = self._table_svc.query(INSTANCE_REGISTRY)
        return sorted({r["node"] for r in rows if r.get("node")})

    # ------------------------------------------------------------------
    # internal helpers
    # ------------------------------------------------------------------

    def _derive_status(self, node: str, persisted: Optional[str]) -> str:
        """Merge heartbeat liveness with the persisted lifecycle status.

        A heartbeat-UNHEALTHY node always shows 异常 (liveness wins);
        otherwise the gateway-written persisted status is shown (运行 /
        停止 / 异常, defaulting to 运行 for legacy rows).
        """
        if self._is_node_expired is not None and self._is_node_expired(node):
            return "异常"
        return persisted or "运行"

    @staticmethod
    def _validate_entry(entry: Dict[str, Any]) -> None:
        for field in _REQUIRED_FIELDS:
            val = entry.get(field)
            if val is None or val == "":
                raise InstanceValidationError(
                    f"missing required field: {field}"
                )
        if entry["kind"] not in _VALID_KINDS:
            raise InstanceValidationError(
                f"invalid kind: {entry['kind']!r}, must be one of {_VALID_KINDS}"
            )

    def _to_entry(self, row: Dict[str, Any]) -> Dict[str, Any]:
        data = row.get("data", {}) or {}
        node = row.get("node", "")
        return {
            "service_id": row["service_id"],
            "kind": row["kind"],
            "framework": row["framework"],
            "framework_version": row["framework_version"],
            "node": node,
            "address": data.get("address", ""),
            "instance_id": data.get("instance_id", "") or "",
            "user": row["user"],
            "created_at": data.get("created_at", ""),
            "last_active_at": data.get("last_active_at", ""),
            "status": self._derive_status(node, data.get("status")),
        }