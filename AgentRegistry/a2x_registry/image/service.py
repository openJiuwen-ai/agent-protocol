"""ImageService -- image management business logic.

Responsibilities:
- ``register_image``: insert one row (framework+version, idempotent
  upsert); write ``version_key`` and ``uploaded_by`` as promoted columns;
  store flat ``data`` JSON (no ``rootfs`` wrapper); auto-set ``is_default``
  when the framework has no default yet.
- ``query``: return **flat** rows (one row per framework version) with
  optional ``framework`` / ``uploaded_by`` filters and SQL-side pagination
  (``LIMIT/OFFSET``). Returns ``(rows, total)`` tuple.
- ``deregister``: verify no in-use instances; delete repo image file (stub)
  and delete the row; promote the latest remaining version to default.
- ``set_default`` / ``get_default_version``: default-version management.
- ``resolve_launch_spec``: assemble flat launch spec for the gateway.

Persistence goes through ``RegistryTableService``; this service does not
hold a backend/store directly.
"""

from __future__ import annotations

import logging
import os
from typing import Any, Dict, List, Optional, Tuple

from a2x_registry.common.ids import image_sid, now_iso
from a2x_registry.register.service import RegistryTableService

from .errors import ImageInUseError, ImageNotFoundError, ImageValidationError
from .version_key import version_key

logger = logging.getLogger(__name__)

IMAGE_REGISTRY = "images"
INSTANCE_REGISTRY = "instances"

# Image repo deletion env var.
_ENV_REPO_BASE = "A2X_REGISTRY_REPO_BASE"

# SQL sort order for image queries (flat, framework ASC, version_key DESC).
_IMAGE_ORDER = (
    'framework ASC, version_key DESC, '
    "json_extract(data, '$.created_at') DESC"
)


class ImageService:
    """Image management business layer."""

    __slots__ = ("_table_svc",)

    def __init__(self, table_svc: RegistryTableService) -> None:
        self._table_svc = table_svc

    # ------------------------------------------------------------------
    # register_image
    # ------------------------------------------------------------------

    def register_image(
        self,
        framework: str,
        framework_version: str,
        runtime_spec: Dict[str, Any],
        env_vars: Dict[str, str],
        workspace: Optional[str],
        mounts: List[Dict[str, Any]],
        image_module_version: Optional[str],
        uploaded_by: str,
    ) -> Dict[str, Any]:
        """Insert one row (framework+version, idempotent upsert).

        ``runtime_spec`` is stored as opaque JSON passthrough.
        ``data`` JSON = ``{runtime_spec, env_vars, workspace, mounts,
        image_module_version, created_at}``.
        """
        if not framework or not framework_version:
            raise ImageValidationError(
                "framework and framework_version must not be empty"
            )

        sid = image_sid(framework, framework_version)
        existing = self._table_svc.query(
            IMAGE_REGISTRY,
            {"framework": framework, "framework_version": framework_version},
        )
        if existing:
            is_default = bool(existing[0].get("is_default"))
            status = "updated"
            created_at = (existing[0].get("data") or {}).get(
                "created_at", now_iso()
            )
        else:
            is_default = not self._has_default(framework)
            status = "registered"
            created_at = now_iso()

        vk = version_key(framework_version)

        data = {
            "runtime_spec": runtime_spec,
            "env_vars": env_vars,
            "workspace": workspace,
            "mounts": mounts,
            "image_module_version": image_module_version,
            "created_at": created_at,
        }

        entry = {
            "service_id": sid,
            "framework": framework,
            "framework_version": framework_version,
            "version_key": vk,
            "is_default": 1 if is_default else 0,
            "uploaded_by": uploaded_by,
            "data": data,
        }
        self._table_svc.register(IMAGE_REGISTRY, entry)
        logger.info(
            "register_image %s@%s (is_default=%s, status=%s, by=%s)",
            framework, framework_version, is_default, status, uploaded_by,
        )
        return {
            "framework": framework,
            "framework_version": framework_version,
            "is_default": is_default,
            "status": status,
        }

    # ------------------------------------------------------------------
    # query (flat + paginated)
    # ------------------------------------------------------------------

    def query(
        self,
        framework: Optional[str] = None,
        uploaded_by: Optional[str] = None,
        size: int = -1,
        page: int = 1,
    ) -> Tuple[List[Dict[str, Any]], int]:
        """Return flat rows (one per version) with optional filters and pagination.

        Returns ``(rows, total)``. ``total`` is the filtered count before
        pagination.
        """
        flt: Dict[str, Any] = {}
        if framework:
            flt["framework"] = framework
        if uploaded_by:
            flt["uploaded_by"] = uploaded_by

        offset = max(0, (page - 1) * size) if size > 0 else 0
        rows, total = self._table_svc.query_paginated(
            IMAGE_REGISTRY,
            filter=flt or None,
            order_by=_IMAGE_ORDER,
            limit=size if size > 0 else -1,
            offset=offset,
        )
        return [self._row_to_entry(r) for r in rows], total

    # ------------------------------------------------------------------
    # deregister
    # ------------------------------------------------------------------

    def deregister(self, framework: str, framework_version: str) -> Dict[str, Any]:
        """Deregister an image version. Raises ``ImageInUseError`` if in use."""
        rows = self._table_svc.query(
            IMAGE_REGISTRY,
            {"framework": framework, "framework_version": framework_version},
        )
        if not rows:
            raise ImageNotFoundError(
                f"image {framework}@{framework_version} not found"
            )
        target = rows[0]
        was_default = bool(target.get("is_default"))

        in_use = self._table_svc.query(
            INSTANCE_REGISTRY,
            {"framework": framework, "framework_version": framework_version},
        )
        if in_use:
            raise ImageInUseError(
                f"image {framework}@{framework_version} still has "
                f"{len(in_use)} in-use instance(s); cannot deregister"
            )

        repo_deleted = self._delete_repo_image(target.get("data", {}))

        sid = image_sid(framework, framework_version)
        self._table_svc.deregister(IMAGE_REGISTRY, sid)

        if was_default:
            self._promote_latest_default(framework)

        logger.info(
            "deregister_image %s@%s (was_default=%s, repo_deleted=%s)",
            framework, framework_version, was_default, repo_deleted,
        )
        return {
            "framework": framework,
            "framework_version": framework_version,
            "status": "deregistered",
            "repo_deleted": repo_deleted,
        }

    # ------------------------------------------------------------------
    # set_default / get_default_version
    # ------------------------------------------------------------------

    def set_default(self, framework: str, framework_version: str) -> Dict[str, Any]:
        """Set the default version for a framework."""
        sid = image_sid(framework, framework_version)
        rows = self._table_svc.query(
            IMAGE_REGISTRY,
            {"framework": framework, "framework_version": framework_version},
        )
        if not rows:
            raise ImageNotFoundError(
                f"image {framework}@{framework_version} not found"
            )
        fw_rows = self._table_svc.query(IMAGE_REGISTRY, {"framework": framework})
        for r in fw_rows:
            if bool(r.get("is_default")):
                self._table_svc.patch(
                    IMAGE_REGISTRY, r["service_id"], {"is_default": 0}
                )
        self._table_svc.patch(IMAGE_REGISTRY, sid, {"is_default": 1})
        logger.info("set_default %s -> %s", framework, framework_version)
        return {
            "framework": framework,
            "default": framework_version,
            "status": "updated",
        }

    def get_default_version(self, framework: str) -> str:
        """Get the default version; fall back to the latest (by version_key DESC)."""
        rows = self._table_svc.query(IMAGE_REGISTRY, {"framework": framework})
        if not rows:
            raise ImageNotFoundError(f"framework {framework} has no image records")
        default = self._pick_default_version(rows)
        if default is not None:
            return default
        sorted_rows = self._sort_versions(rows)
        return sorted_rows[0]["framework_version"] if sorted_rows else ""

    # ------------------------------------------------------------------
    # resolve_launch_spec
    # ------------------------------------------------------------------

    def resolve_launch_spec(
        self, framework: str, version: Optional[str] = None
    ) -> Dict[str, Any]:
        """Assemble launch spec with runtime_spec passthrough."""
        ver = version or self.get_default_version(framework)
        rows = self._table_svc.query(
            IMAGE_REGISTRY,
            {"framework": framework, "framework_version": ver},
        )
        if not rows:
            raise ImageNotFoundError(f"image {framework}@{ver} not found")
        data = rows[0].get("data", {}) or {}
        return {
            "framework": framework,
            "framework_version": ver,
            "runtime_spec": data.get("runtime_spec"),
            "env_vars": data.get("env_vars", {}),
            "workspace": data.get("workspace"),
            "mounts": data.get("mounts", []),
            "image_module_version": data.get("image_module_version"),
        }

    # ------------------------------------------------------------------
    # internal helpers
    # ------------------------------------------------------------------

    def _has_default(self, framework: str) -> bool:
        rows = self._table_svc.query(
            IMAGE_REGISTRY, {"framework": framework, "is_default": 1}
        )
        return bool(rows)

    @staticmethod
    def _pick_default_version(rows: List[Dict[str, Any]]) -> Optional[str]:
        for r in rows:
            if bool(r.get("is_default")):
                return r["framework_version"]
        return None

    @staticmethod
    def _sort_versions(
        rows: List[Dict[str, Any]],
    ) -> List[Dict[str, Any]]:
        """Sort by version_key descending (latest first)."""
        return sorted(
            rows,
            key=lambda r: r.get("version_key", ""),
            reverse=True,
        )

    def _promote_latest_default(self, framework: str) -> None:
        rows = self._table_svc.query(IMAGE_REGISTRY, {"framework": framework})
        if not rows:
            return
        latest = self._sort_versions(rows)[0]
        self._table_svc.patch(
            IMAGE_REGISTRY, latest["service_id"], {"is_default": 1}
        )

    @staticmethod
    def _row_to_entry(row: Dict[str, Any]) -> Dict[str, Any]:
        """Convert a DB row (merged entry dict) into a image entry."""
        data = row.get("data", {}) or {}
        return {
            "framework": row["framework"],
            "framework_version": row["framework_version"],
            "is_default": bool(row.get("is_default")),
            "image_module_version": data.get("image_module_version"),
            "runtime_spec": data.get("runtime_spec"),
            "workspace": data.get("workspace"),
            "mounts": data.get("mounts", []),
            "env_vars": data.get("env_vars", {}),
            "uploaded_by": row.get("uploaded_by"),
            "created_at": data.get("created_at"),
        }

    def _delete_repo_image(self, data: Dict[str, Any]) -> bool:
        """Repo image file deletion stub."""
        repo_base = os.environ.get(_ENV_REPO_BASE, "").strip()
        if not repo_base:
            logger.warning(
                "A2X_REGISTRY_REPO_BASE not configured; skipping repo "
                "image file deletion (stub)"
            )
            return False
        imageurl = data.get("imageurl", "")
        logger.info(
            "[stub] repo image deletion not implemented: "
            "repo_base=%s imageurl=%s", repo_base, imageurl
        )
        return False
