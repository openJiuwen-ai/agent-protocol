"""ImageService -- image management business logic (name 主键).

Responsibilities:
- ``register_image``: insert one row (name+version, idempotent upsert);
  write ``version_key`` and ``uploaded_by`` as promoted columns; store flat
  ``data`` JSON (no ``rootfs`` wrapper); auto-set ``is_default`` when the
  name has no default yet. New §6 fields (``description`` /
  ``package_path`` / ``image_archive_path`` / ``access_mode``) live inside
  ``data``.
- ``query``: return **flat** rows (one row per name version) with optional
  ``name`` / ``framework`` / ``uploaded_by`` filters and SQL-side pagination
  (``LIMIT/OFFSET``). Returns ``(rows, total)`` tuple.
- ``update_image``: partially update mutable fields of one row (§8) -
  ``framework`` as a promoted column, the rest merged into ``data``
  read-modify-write; primary key / ``is_default`` untouched.
- ``deregister``: verify no in-use instances; delete repo image file (stub)
  and delete the row; promote the latest remaining version to default.
- ``set_default`` / ``get_default_version``: default-version management
  (one default per name).
- ``resolve_launch_spec``: assemble flat launch spec for the gateway.

In-use check: instance rows still carry ``framework`` / ``framework_version``
(instance contract unchanged this batch), so an image is "in use" when an
instance references its display ``framework`` + ``version``.

Persistence goes through ``RegistryTableService``; this service does not
hold a backend/store directly.
"""

from __future__ import annotations

import logging
import os
from typing import Any, Dict, List, Optional, Tuple

from a2x_registry.common.ids import image_sid, now_iso
from a2x_registry.register.table_repo import TableRepo

from .errors import ImageInUseError, ImageNotFoundError, ImageValidationError
from .version_key import version_key

logger = logging.getLogger(__name__)

IMAGE_REGISTRY = "images"
INSTANCE_REGISTRY = "instances"

# Image repo deletion env var.
_ENV_REPO_BASE = "A2X_REGISTRY_REPO_BASE"

# Structured sort spec for image queries (flat, name ASC, version_key DESC).
_IMAGE_ORDER = (
    "name asc",
    "version_key desc",
    "data.created_at desc",
)

# Mutable ``data``-JSON fields patchable via ``update_image`` (§8).
# Everything else inside ``data`` (image_module_version / created_at) is
# not patchable via the image PATCH endpoint.
_MUTABLE_DATA_FIELDS = (
    "description",
    "package_path",
    "image_archive_path",
    "runtime_spec",
    "access_mode",
    "env_vars",
    "workspace",
    "mounts",
)


class ImageService:
    """Image management business layer."""

    __slots__ = ("_table_svc",)

    def __init__(self, table_svc: TableRepo) -> None:
        self._table_svc = table_svc

    # ------------------------------------------------------------------
    # register_image
    # ------------------------------------------------------------------

    def register_image(
        self,
        name: str,
        version: str,
        runtime_spec: Dict[str, Any],
        uploaded_by: str,
        framework: Optional[str] = None,
        description: Optional[str] = None,
        package_path: Optional[str] = None,
        image_archive_path: Optional[str] = None,
        access_mode: Optional[List[Dict[str, Any]]] = None,
        env_vars: Optional[Dict[str, str]] = None,
        workspace: Optional[str] = None,
        mounts: Optional[List[Dict[str, Any]]] = None,
        image_module_version: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Insert one row (name+version, idempotent upsert).

        ``runtime_spec`` is stored as opaque JSON passthrough. ``data``
        JSON = ``{runtime_spec, access_mode, env_vars, workspace, mounts,
        description, package_path, image_archive_path,
        image_module_version, created_at}``.
        """
        if not name or not version:
            raise ImageValidationError("name and version must not be empty")

        sid = image_sid(name, version)
        existing = self._table_svc.query(
            IMAGE_REGISTRY,
            {"name": name, "version": version},
        )
        if existing:
            is_default = bool(existing[0].get("is_default"))
            status = "updated"
            created_at = (existing[0].get("data") or {}).get(
                "created_at", now_iso()
            )
        else:
            is_default = not self._has_default(name)
            status = "registered"
            created_at = now_iso()

        vk = version_key(version)

        data = {
            "runtime_spec": runtime_spec,
            "access_mode": access_mode or [],
            "env_vars": env_vars or {},
            "workspace": workspace,
            "mounts": mounts or [],
            "description": description,
            "package_path": package_path,
            "image_archive_path": image_archive_path,
            "image_module_version": image_module_version,
            "created_at": created_at,
        }

        entry = {
            "service_id": sid,
            "name": name,
            "framework": framework,
            "version": version,
            "version_key": vk,
            "is_default": 1 if is_default else 0,
            "uploaded_by": uploaded_by,
            "data": data,
        }
        self._table_svc.register(IMAGE_REGISTRY, entry)
        logger.info(
            "register_image %s@%s (is_default=%s, status=%s, by=%s)",
            name, version, is_default, status, uploaded_by,
        )
        return {
            "name": name,
            "framework": framework,
            "version": version,
            "status": status,
        }

    # ------------------------------------------------------------------
    # query (flat + paginated)
    # ------------------------------------------------------------------

    def query(
        self,
        name: Optional[str] = None,
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
        if name:
            flt["name"] = name
        if framework:
            flt["framework"] = framework
        if uploaded_by:
            flt["uploaded_by"] = uploaded_by

        offset = max(0, (page - 1) * size) if size > 0 else 0
        rows, total = self._table_svc.query_paginated(
            IMAGE_REGISTRY,
            query_filter=flt or None,
            order_by=_IMAGE_ORDER,
            limit=size if size > 0 else -1,
            offset=offset,
        )
        return [self._row_to_entry(r) for r in rows], total

    # ------------------------------------------------------------------
    # update_image (§8 partial update)
    # ------------------------------------------------------------------

    def update_image(
        self, name: str, version: str, fields: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Partially update mutable fields of one image row (name@version).

        ``fields`` keys ⊆ ``{"framework"} ∪ _MUTABLE_DATA_FIELDS``; at
        least one non-``None`` value required. Primary key ``name`` /
        ``version`` and ``is_default`` are not patchable (the request
        model omits them). ``framework`` is a promoted column; the rest
        live inside the ``data`` JSON and are merged read-modify-write
        (the whole ``data`` blob is patched). Returns the updated entry.
        """
        if not any(v is not None for v in fields.values()):
            raise ImageValidationError(
                "at least one field must be provided"
            )

        rows = self._table_svc.query(
            IMAGE_REGISTRY, {"name": name, "version": version}
        )
        if not rows:
            raise ImageNotFoundError(f"image {name}@{version} not found")
        row = rows[0]

        patch_fields: Dict[str, Any] = {}
        if fields.get("framework") is not None:
            patch_fields["framework"] = fields["framework"]

        data = dict(row.get("data", {}) or {})
        dirty = False
        for key in _MUTABLE_DATA_FIELDS:
            if fields.get(key) is not None:
                data[key] = fields[key]
                dirty = True
        if dirty:
            patch_fields["data"] = data

        updated = self._table_svc.patch(
            IMAGE_REGISTRY, row["service_id"], patch_fields
        )
        logger.info(
            "update_image %s@%s (fields=%s)",
            name, version,
            sorted(k for k, v in fields.items() if v is not None),
        )
        return self._row_to_entry(updated)

    # ------------------------------------------------------------------
    # deregister
    # ------------------------------------------------------------------

    def deregister(self, name: str, version: str) -> Dict[str, Any]:
        """Deregister an image version. Raises ``ImageInUseError`` if in use."""
        rows = self._table_svc.query(
            IMAGE_REGISTRY,
            {"name": name, "version": version},
        )
        if not rows:
            raise ImageNotFoundError(f"image {name}@{version} not found")
        target = rows[0]
        was_default = bool(target.get("is_default"))
        framework = target.get("framework")

        in_use = self._in_use_instances(framework, version, name)
        if in_use:
            raise ImageInUseError(
                f"image {name}@{version} still has "
                f"{len(in_use)} in-use instance(s); cannot deregister"
            )

        self._delete_repo_image(target.get("data", {}))

        sid = image_sid(name, version)
        self._table_svc.deregister(IMAGE_REGISTRY, sid)

        if was_default:
            self._promote_latest_default(name)

        logger.info(
            "deregister_image %s@%s (was_default=%s)",
            name, version, was_default,
        )
        return {
            "name": name,
            "framework": framework,
            "version": version,
            "status": "deregistered",
        }

    # ------------------------------------------------------------------
    # set_default / get_default_version
    # ------------------------------------------------------------------

    def set_default(self, name: str, version: str) -> Dict[str, Any]:
        """Set the default version for a name."""
        sid = image_sid(name, version)
        rows = self._table_svc.query(
            IMAGE_REGISTRY,
            {"name": name, "version": version},
        )
        if not rows:
            raise ImageNotFoundError(f"image {name}@{version} not found")
        framework = rows[0].get("framework")
        name_rows = self._table_svc.query(IMAGE_REGISTRY, {"name": name})
        for r in name_rows:
            if bool(r.get("is_default")):
                self._table_svc.patch(
                    IMAGE_REGISTRY, r["service_id"], {"is_default": 0}
                )
        self._table_svc.patch(IMAGE_REGISTRY, sid, {"is_default": 1})
        logger.info("set_default %s -> %s", name, version)
        return {
            "name": name,
            "framework": framework,
            "default": version,
            "status": "updated",
        }

    def get_default_version(self, name: str) -> str:
        """Get the default version; fall back to the latest (by version_key DESC)."""
        rows = self._table_svc.query(IMAGE_REGISTRY, {"name": name})
        if not rows:
            raise ImageNotFoundError(f"name {name} has no image records")
        default = self._pick_default_version(rows)
        if default is not None:
            return default
        sorted_rows = self._sort_versions(rows)
        return sorted_rows[0]["version"] if sorted_rows else ""

    # ------------------------------------------------------------------
    # resolve_launch_spec
    # ------------------------------------------------------------------

    def resolve_launch_spec(
        self, name: str, version: Optional[str] = None
    ) -> Dict[str, Any]:
        """Assemble launch spec with runtime_spec passthrough."""
        ver = version or self.get_default_version(name)
        rows = self._table_svc.query(
            IMAGE_REGISTRY,
            {"name": name, "version": ver},
        )
        if not rows:
            raise ImageNotFoundError(f"image {name}@{ver} not found")
        row = rows[0]
        data = row.get("data", {}) or {}
        return {
            "name": name,
            "framework": row.get("framework"),
            "version": ver,
            "runtime_spec": data.get("runtime_spec"),
            "access_mode": data.get("access_mode", []),
            "env_vars": data.get("env_vars", {}),
            "workspace": data.get("workspace"),
            "mounts": data.get("mounts", []),
            "image_module_version": data.get("image_module_version"),
        }

    # ------------------------------------------------------------------
    # internal helpers
    # ------------------------------------------------------------------

    def _in_use_instances(
        self, framework: Optional[str], version: str, name: str
    ) -> List[Dict[str, Any]]:
        """Instances referencing this image.

        Instance rows carry ``framework`` / ``framework_version`` (instance
        contract unchanged), so the join key is the image's
        display ``framework`` + ``version``. An image without a ``framework``
        cannot be referenced by any instance.
        """
        if not framework:
            logger.info(
                "in-use check for image %s@%s falls back to name match "
                "(image framework is empty)", name, version,
            )
            return self._table_svc.query(
                INSTANCE_REGISTRY,
                {"framework": name, "framework_version": version},
            )
        return self._table_svc.query(
            INSTANCE_REGISTRY,
            {"framework": framework, "framework_version": version},
        )

    def _has_default(self, name: str) -> bool:
        rows = self._table_svc.query(
            IMAGE_REGISTRY, {"name": name, "is_default": 1}
        )
        return bool(rows)

    @staticmethod
    def _pick_default_version(rows: List[Dict[str, Any]]) -> Optional[str]:
        for r in rows:
            if bool(r.get("is_default")):
                return r["version"]
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

    def _promote_latest_default(self, name: str) -> None:
        rows = self._table_svc.query(IMAGE_REGISTRY, {"name": name})
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
        version = row["version"]
        return {
            "name": row["name"],
            "framework": row.get("framework"),
            "description": data.get("description"),
            "package_path": data.get("package_path"),
            "image_archive_path": data.get("image_archive_path"),
            "version": version,
            "framework_version": version,  # deprecated alias, 过渡期保留
            "is_default": bool(row.get("is_default")),
            "runtime_spec": data.get("runtime_spec"),
            "access_mode": data.get("access_mode", []),
            "workspace": data.get("workspace"),
            "mounts": data.get("mounts", []),
            "env_vars": data.get("env_vars", {}),
            "image_module_version": data.get("image_module_version"),
            "uploaded_by": row.get("uploaded_by"),
            "created_at": data.get("created_at"),
        }

    def _delete_repo_image(self, data: Dict[str, Any]) -> bool:
        """Repo image file deletion stub.

        The imageurl lives inside the opaque ``runtime_spec`` passthrough
        (``data.runtime_spec.rootfs.imageurl``) — never at the data top
        level; ``rootfs`` / ``imageurl`` may be absent (passthrough is not
        validated), so the chain falls back to ``""``.
        """
        repo_base = os.environ.get(_ENV_REPO_BASE, "").strip()
        if not repo_base:
            logger.warning(
                "A2X_REGISTRY_REPO_BASE not configured; skipping repo "
                "image file deletion (stub)"
            )
            return False
        imageurl = (
            ((data.get("runtime_spec") or {}).get("rootfs") or {})
            .get("imageurl")
            or ""
        )
        logger.info(
            "[stub] repo image deletion not implemented: "
            "repo_base=%s imageurl=%s", repo_base, imageurl
        )
        return False
