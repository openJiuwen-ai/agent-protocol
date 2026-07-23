"""InstanceService — instance management business logic.

Pure recorder: register / update / deregister / query / expire_node.
Does not invoke the runtime (元戎) or make decisions for the gateway.

Persistence goes through ``RegistryTableService`` (SQL backend); this
service does not hold a backend/store directly. The ``data`` JSON column
holds ``{address, created_at, last_active_at}`` — these are not promoted
columns, so ``update_instance`` must merge ``address`` into the existing
``data`` dict before patching.

``status`` (运行 / 异常) is never persisted — it is derived per-query
from a node-heartbeat callback injected via ``set_heartbeat_check``.
When no callback is injected (standalone, or heartbeat module not
loaded), all instances are considered healthy (运行).

``list_instances`` supports pagination (``size``/``page``),
deterministic ordering (``framework, "user", service_id``), and SQL-side
push-down for ``include_unhealthy=False`` via ``expired_nodes()``.
"""

from __future__ import annotations

import logging
from typing import Any, Callable, Dict, List, Optional, Tuple

from a2x_registry.common.ids import now_iso
from a2x_registry.register.service import RegistryTableService

from .errors import InstanceNotFoundError, InstanceValidationError

logger = logging.getLogger(__name__)

INSTANCE_REGISTRY = "instances"

# Accepted instance kinds (OpenAPI enum).
_VALID_KINDS = ("三方", "九问")

# Required fields for register_instance.
_REQUIRED_FIELDS = (
    "service_id", "kind", "framework", "framework_version",
    "node", "address", "user",
)

# Callback type: (node_ip) -> is_expired
NodeExpiredCheck = Callable[[str], bool]

# Deterministic sort order for instance listing (V2).
_INSTANCE_ORDER = 'framework ASC, "user" ASC, service_id ASC'


class InstanceService:
    """Instance management business layer."""

    __slots__ = ("_table_svc", "_is_node_expired", "_expired_nodes_provider")

    def __init__(self, table_svc: RegistryTableService) -> None:
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

        db_entry = {
            "service_id": sid,
            "kind": entry["kind"],
            "framework": entry["framework"],
            "framework_version": entry["framework_version"],
            "node": entry["node"],
            "user": entry["user"],
            "data": {
                "address": entry["address"],
                "created_at": created_at,
                "last_active_at": now,
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
        if not has_node and not has_address:
            raise InstanceValidationError(
                "at least one of node/address must be provided"
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
        if has_address:
            data["address"] = fields["address"]
            data["last_active_at"] = now_iso()
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
        excluded via SQL push-down: ``node NOT IN (expired_nodes())``.
        This ensures ``LIMIT/OFFSET`` and ``X-Total-Count`` are correct.

        Returns ``(entries, total)`` — total is the filtered count before
        pagination.
        """
        extra_where = ""
        extra_args: tuple = ()
        if not include_unhealthy and self._expired_nodes_provider is not None:
            dead = self._expired_nodes_provider()
            if dead:
                dead_list = sorted(dead)
                placeholders = ",".join("?" for _ in dead_list)
                extra_where = f"node NOT IN ({placeholders})"
                extra_args = tuple(dead_list)

        offset = max(0, (page - 1) * size) if size > 0 else 0
        rows, total = self._table_svc.query_paginated(
            INSTANCE_REGISTRY,
            filter=filter or None,
            extra_where=extra_where,
            extra_args=extra_args,
            order_by=_INSTANCE_ORDER,
            limit=size if size > 0 else -1,
            offset=offset,
        )
        entries = [self._to_entry(r) for r in rows]

        # Fallback: when _expired_nodes_provider is not set but
        # _is_node_expired is (e.g. tests using set_heartbeat_check
        # directly), filter unhealthy entries in memory.
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

    def _derive_status(self, node: str) -> str:
        if self._is_node_expired is not None and self._is_node_expired(node):
            return "异常"
        return "运行"

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
            "user": row["user"],
            "created_at": data.get("created_at", ""),
            "last_active_at": data.get("last_active_at", ""),
            "status": self._derive_status(node),
        }