"""TableRepo -- abstract interface for named-registry row CRUD (dependency inversion).

The image / instance business layers depend only on this interface, never on
a concrete backend. Two implementations satisfy it:

- ``RegistryTableService`` (register/service.py) -- SQL-backed (sqlite /
  memory), the pre-existing production path.
- ``EtcdTableRepo`` (register/etcd_repo.py, later phase) -- etcd KV store,
  used for distributed sync across appliance nodes.

The two backends share the same method surface and the same row semantics
described below, so switching ``A2X_REGISTRY_DB_KIND`` only changes which
repo ``backend/startup.py`` injects; the image / instance callers stay
unchanged.

Semantics (per-kind upsert / timestamp rules):
- ``register`` upserts one row by ``(registry, service_id)``. For the
  ``service`` kind the schema has ``created_at`` / ``updated_at`` columns,
  so an upsert preserves the first ``created_at`` and refreshes
  ``updated_at``. The ``image`` / ``instance`` kinds have **no** timestamp
  columns -- their timestamps live inside the ``data`` JSON and a re-register
  fully replaces the row (the ``data`` blob is written as-is).
- Every row is stored with its ``registry`` name; ``service_id`` is the
  primary key within that registry. ``data`` is a free-form JSON dict.
- ``query`` / ``query_paginated`` filter on the promoted (hot) columns of
  the registry's kind (e.g. ``framework``, ``framework_version``,
  ``uploaded_by`` for image; ``node``, ``user``, ``kind`` for instance).

The interface is **structural** (a Protocol): any class with these methods
satisfies it automatically; no explicit inheritance required.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Protocol, Sequence, Tuple, runtime_checkable


@runtime_checkable
class TableRepo(Protocol):
    """Named-registry row CRUD contract (see module docstring for semantics)."""

    def create_registry(self, name: str, kind: str) -> None:
        """Idempotently declare a named registry (name -> kind).

        Used at startup to create ``images`` / ``instances``; re-creating the
        same (name, kind) is a no-op. An unknown kind raises ValidationError.
        """

    def get_kind(self, name: str) -> Optional[str]:
        """Return the kind of a named registry, or ``None`` if not declared."""

    def list_registries(self) -> Dict[str, str]:
        """Return ``{name: kind}`` for all declared registries."""

    def register(self, name: str, entry: dict) -> dict:
        """Upsert one row by ``entry["service_id"]`` in registry ``name``.

        Returns the stored row as a merged dict (promoted columns + parsed
        ``data``). Raises NotFoundError if the registry is unknown,
        ValidationError if ``entry`` lacks ``service_id``.
        """

    def get(self, name: str, service_id: str) -> Optional[dict]:
        """Fetch one row by ``(name, service_id)``; ``None`` if absent."""

    def patch(self, name: str, service_id: str, fields: dict) -> dict:
        """Partially update a subset of promoted columns / ``data``.

        Raises NotFoundError if the registry or row is absent, ValidationError
        on unknown keys. Returns the updated row.
        """

    def deregister(self, name: str, service_id: str) -> bool:
        """Delete one row by ``(name, service_id)``.

        Idempotent: returns True if a row was deleted, False if it did not
        exist (also False for an unknown registry).
        """

    def query(self, name: str, query_filter: Optional[dict] = None) -> List[dict]:
        """Return rows of registry ``name``, optionally equality-filtered by
        promoted columns (or ``service_id``). ``filter=None`` returns all."""

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
        """Return ``(rows, total)`` with optional ordering and pagination.

        - ``exclude_nodes``: instances listed on these node IPs are excluded
          (used for the "include_unhealthy" push-down). ``None`` = keep all.
        - ``only_status``: keep only rows whose persisted ``data.status``
          equals this value; a row without ``data.status`` (legacy schema)
          defaults to ``运行``. Used by the instance list's
          ``include_unhealthy=False`` push-down. ``None`` = keep all.
        - ``order_by``: a structured sequence of ``"<field> <asc|desc>"``
          items (e.g. ``["framework asc", "data.created_at desc"]``). Field is
          either a promoted column or a ``data.<key>`` reference (JSON-internal
          field); each backend resolves it (SQL -> ``json_extract``, etcd ->
          the flattened row dict).
        - ``limit > 0``: ``LIMIT/OFFSET`` applied; ``total`` is the filtered
          count before pagination. ``limit <= 0``: all rows returned and
          ``total == len(rows)``.

        This is a **required** method on every backend (image/instance list
        depends on it); a backend that cannot implement it must not be used.
        """