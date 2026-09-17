"""Synchronous client entry point.

``A2XRegistryClient`` composes an ``HTTPTransport`` + ``OwnershipStore`` and translates
each public method into one HTTP call plus (for mutating methods) an ownership
check / update. All business rules live in this module; network and
persistence concerns stay in their respective components.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Literal

import warnings

from . import _internal as _i
from .auth import resolve_credentials
from .errors import (
    A2XConnectionError,
    NotFoundError,
    NotOwnedError,
    ServerError,
    ValidationError,
)
from .heartbeat import HeartbeatRegistry, HeartbeatRenewer
from .models import (
    AgentDetail,
    DatasetCreateResponse,
    DatasetDeleteResponse,
    DeregisterResponse,
    PatchResponse,
    PrincipalCreateResponse,
    RegisterResponse,
    Reservation,
)
from .ownership import OwnershipStore
from .transport import HTTPTransport


class A2XRegistryClient:
    def __init__(
        self,
        base_url: str | None = None,
        timeout: float = 30.0,
        api_key: str | None = None,
        ownership_file: Path | str | Literal[False] | None = None,
    ) -> None:
        # Resolve credentials per the SDK's two-tier rule (see auth.py):
        # explicit args > cli_token.json. ``base_url=None`` causes the
        # config file's ``base_url`` to be used if present; else falls
        # back to the legacy default ``http://127.0.0.1:8000``.
        api_key, base_url = resolve_credentials(api_key, base_url)
        self._base_url = _i.normalize_base_url(base_url)
        self._timeout = timeout
        self._api_key = api_key
        self._transport = HTTPTransport(
            base_url=self._base_url,
            timeout=timeout,
            headers=_i.build_default_headers(api_key),
        )
        self._owned = OwnershipStore(
            file_path=_i.resolve_ownership_file(ownership_file),
            base_url=self._base_url,
        )
        # L1 cache for restore_to_blank: {(dataset, service_id): endpoint}.
        # Populated by register_blank_agent / restore_to_blank in the same
        # process; not persisted (by design). L2 fallback in
        # restore_to_blank reads the endpoint from the current card.
        self._blank_endpoints: dict[tuple[str, str], str] = {}
        # Heartbeat renewers managed by this client (only populated when
        # the user passes auto_renew=True on register and the server
        # grants a lease). Stopped on close().
        self._renewers = HeartbeatRegistry()

    # ── Read-only config exposure ────────────────────────────────────────────
    # Stored as underscore attributes because changing them at runtime would
    # not reconnect the transport or re-scope the ownership file — documenting
    # the immutability via property is clearer than a writable attribute.

    @property
    def base_url(self) -> str:
        return self._base_url

    @property
    def timeout(self) -> float:
        return self._timeout

    @property
    def api_key(self) -> str | None:
        return self._api_key

    # ── Lifecycle ────────────────────────────────────────────────────────────

    def close(self) -> None:
        """Close the transport + stop any background heartbeat renewers.

        Renewer shutdown is best-effort with a short timeout — the server
        will eventually expire the lease via TTL even if we can't reach
        it during close (e.g. network already down). NOT a deregister:
        the service stays registered on the server; only the local renewal
        thread is stopped. Use ``shutdown()`` for an explicit deregister.
        """
        self._renewers.shutdown_all(timeout=1.0)
        self._transport.close()

    def __enter__(self) -> "A2XRegistryClient":
        return self

    def __exit__(self, *_exc: Any) -> None:
        self.close()

    # ── Ownership guard ──────────────────────────────────────────────────────

    def _assert_owned(self, dataset: str, service_id: str) -> None:
        if not self._owned.contains(dataset, service_id):
            raise NotOwnedError(dataset, service_id)

    # ── Datasets ─────────────────────────────────────────────────────────────

    def create_dataset(
        self,
        name: str,
        embedding_model: str = _i.DEFAULT_EMBEDDING_MODEL,
        formats: Any = _i.UNSET,
        auth_required: bool = False,
        lease_config: dict[str, Any] | None = None,
    ) -> DatasetCreateResponse:
        """Create a dataset (namespace). Optional opt-in features:

        - ``auth_required=True``: only admin-token requests can register /
          mutate inside this namespace. Requires the server to have been
          bootstrapped (``a2x-registry auth init``); admin token must be
          supplied in this client's ``api_key``.
        - ``lease_config={"enabled": True, "min_ttl": 10, "max_ttl": 600,
          "grace_period": 60}``: per-namespace heartbeat. Services
          registered here MUST pass ``lease_ttl`` on register; the server
          marks them unhealthy then hard-deletes them after grace if the
          client stops sending heartbeats.

        Both fields default to off so legacy callers keep producing
        byte-equal request bodies and see the same response shape.
        """
        body = _i.build_create_dataset_body(
            name, embedding_model, formats, auth_required, lease_config,
        )
        resp = self._transport.request("POST", _i.DATASETS_ROOT, json=body)
        return DatasetCreateResponse.from_dict(resp.json())

    def create_principal(
        self,
        handle: str,
        role: str,
        namespaces: list[str] | None = None,
        note: str | None = None,
    ) -> PrincipalCreateResponse:
        """Create a new principal + return its first API key plaintext.

        Admin-only — caller's ``api_key`` must be an admin token. ``role``
        is ``"admin"`` / ``"provider"`` / ``"user"``; for non-admin roles,
        ``namespaces`` MUST list the dataset(s) the principal is scoped
        to (the namespaces must already exist server-side).

        The returned ``token`` is **plaintext** and appears only here;
        deliver it out-of-band (vault / secrets manager / encrypted IM)
        to the new principal — the server only keeps a sha256 hash after.
        """
        body = _i.build_create_principal_body(handle, role, namespaces, note)
        resp = self._transport.request("POST", _i.AUTH_PRINCIPALS_ROOT, json=body)
        return PrincipalCreateResponse.from_dict(resp.json())

    def delete_dataset(self, name: str) -> DatasetDeleteResponse:
        try:
            resp = self._transport.request("DELETE", _i.dataset_path(name))
        except ValidationError:
            # Backend 400 on dataset-missing is the only 400 case here;
            # clear local bookkeeping so subsequent calls stop failing. (D6)
            self._owned.remove_dataset(name)
            raise
        result = DatasetDeleteResponse.from_dict(resp.json())
        self._owned.remove_dataset(name)
        return result

    # ── Agents ───────────────────────────────────────────────────────────────

    def register_agent(
        self,
        dataset: str,
        agent_card: dict[str, Any],
        service_id: str | None = None,
        persistent: bool = True,
        lease_ttl: int | None = None,
        auto_renew: bool = False,
    ) -> RegisterResponse:
        """Register an A2A agent.

        Optional heartbeat:
        - ``lease_ttl`` — if the namespace has heartbeat enabled, the
          server validates this against ``[min_ttl, max_ttl]`` and grants
          a lease. ``None`` means "permanent service" (legacy behavior).
        - ``auto_renew`` — when True and the server granted a lease,
          spawn a background daemon thread that POSTs to the heartbeat
          endpoint every ``ttl/3`` seconds. The thread is stopped by
          ``client.close()`` / ``client.shutdown()``. Default False so the
          caller must explicitly opt in to background activity.

        Server-side rejection cases (matrix violations) raise the typed
        subclasses ``A2XHeartbeatNotSupportedError`` / ``A2XTTLRequiredError``
        / ``A2XTTLOutOfRangeError``, all of which inherit ``ValidationError``.
        """
        body = _i.build_register_agent_body(agent_card, service_id, persistent)
        if lease_ttl is not None:
            body["lease_ttl"] = int(lease_ttl)
        resp = self._transport.request("POST", _i.a2a_register_path(dataset), json=body)
        result = RegisterResponse.from_dict(resp.json())
        if persistent:
            # Backend discards non-persistent entries on restart, so persisting
            # ownership for them would cause later NotFoundError cascades. (D4)
            self._owned.add(dataset, result.service_id)
        # If the server granted a lease AND the caller wanted background
        # renewal, install a daemon thread. We use ``result.lease_ttl``
        # (server-echoed) rather than ``lease_ttl`` (request value) so a
        # future server-side clamp behavior wouldn't desync the renewer.
        if auto_renew and result.lease_ttl is not None:
            self._renewers.add(HeartbeatRenewer(
                dataset=dataset,
                service_id=result.service_id,
                ttl_seconds=result.lease_ttl,
                heartbeat_fn=lambda ds, sid: self.heartbeat(ds, sid),
            ))
        return result

    # ── Heartbeat / lifecycle ─────────────────────────────────────────────────

    def heartbeat(
        self,
        dataset: str,
        service_id: str,
        status: str | None = None,
    ) -> dict[str, Any]:
        """Extend the heartbeat lease for an owned service.

        Optional ``status`` piggybacks an ``agent_card.status`` update in
        the same RPC (server merges before extending the lease). Most
        callers won't invoke this directly — they'd use ``auto_renew=True``
        on register and let the renewer thread handle it. Manual use is
        for callers that prefer their own scheduling (e.g. asyncio
        without the SDK's thread-based renewer).
        """
        self._assert_owned(dataset, service_id)
        body: dict[str, Any] = {}
        if status is not None:
            body["status"] = status
        resp = self._transport.request(
            "POST", _i.heartbeat_path(dataset, service_id), json=body,
        )
        return resp.json()

    def drain(
        self,
        dataset: str,
        service_id: str,
        *,
        reason: str = "drain",
    ) -> PatchResponse:
        """Mark the service ``status=offline`` (stop receiving traffic) but
        keep it registered. Used by K8s preStop / graceful shutdown of
        Agent Team teammates: stop accepting new work, finish in-flight,
        THEN call ``shutdown()`` to fully remove. ``reason`` is for
        future audit-log use; currently unused server-side."""
        # status change is just a one-field update — reuses existing path.
        return self.update_agent(dataset, service_id, {"status": "offline"})

    def shutdown(
        self,
        *,
        sids: list[tuple[str, str]] | None = None,
        dataset: str | None = None,
        permanent: bool = False,
        reason: str = "explicit",
        timeout: float = 2.0,
        raise_on_error: bool = False,
    ) -> dict[str, list]:
        """Revoke heartbeat leases for owned services (default) or fully
        deregister them (``permanent=True``).

        Selection:
        - ``sids=[(ds, sid), ...]`` — explicit list
        - ``dataset="x"`` — every owned sid in that dataset
        - neither — every owned sid across all datasets

        Best-effort: each call has the given timeout; failures are logged
        (warning) but don't raise unless ``raise_on_error=True``. The
        heartbeat TTL is the ultimate safety net.

        Returns ``{"revoked": [(ds, sid)], "errors": [(ds, sid, err)]}``.
        """
        if sids is not None:
            targets = list(sids)
        elif dataset is not None:
            targets = [(dataset, s) for s in self._owned.list(dataset)]
        else:
            targets = [
                (ds, sid)
                for ds in self._owned.datasets()
                for sid in self._owned.list(ds)
            ]

        revoked: list = []
        errors: list = []
        # Stop renewers FIRST so they don't race the revoke with another
        # heartbeat. The shutdown call itself is best-effort regardless.
        for ds, sid in targets:
            self._renewers.remove(ds, sid)
        for ds, sid in targets:
            try:
                if permanent:
                    self.deregister_agent(ds, sid)
                else:
                    self._transport.request(
                        "DELETE", _i.heartbeat_path(ds, sid),
                        json={"permanent": False},
                    )
                revoked.append((ds, sid))
            except Exception as exc:  # noqa: BLE001 — best-effort
                errors.append((ds, sid, str(exc)))
                if raise_on_error:
                    raise
        if errors:
            import warnings as _w
            _w.warn(
                f"shutdown best-effort cleanup left {len(errors)} errors; "
                f"TTL will eventually clean up. First: {errors[0]}",
                stacklevel=2,
            )
        return {"revoked": revoked, "errors": errors}

    def update_agent(
        self,
        dataset: str,
        service_id: str,
        fields: dict[str, Any],
    ) -> PatchResponse:
        self._assert_owned(dataset, service_id)
        try:
            resp = self._transport.request(
                "PUT", _i.service_path(dataset, service_id), json=fields
            )
        except NotFoundError:
            self._owned.remove(dataset, service_id)  # D3
            raise
        return PatchResponse.from_dict(resp.json())

    def set_status(
        self,
        dataset: str,
        service_id: str,
        status: str,
    ) -> PatchResponse:
        """Update the agent's ``status`` field — Eureka-style intent
        (``online`` / ``busy`` / ``offline``).

        Validates the value locally before HTTP. Replaces the previous
        ``set_team_count`` removed in this version (which only ever expressed "0=idle, >0=busy" —
        now expressed directly as ``status``).
        """
        body = _i.build_status_body(status)
        self._assert_owned(dataset, service_id)
        try:
            resp = self._transport.request(
                "PUT", _i.service_path(dataset, service_id), json=body
            )
        except NotFoundError:
            self._owned.remove(dataset, service_id)  # D3
            raise
        return PatchResponse.from_dict(resp.json())

    def list_agents(
        self,
        dataset: str,
        *,
        page: int = 1,
        size: int = -1,
        **filters: Any,
    ) -> list[dict[str, Any]]:
        """List services, optionally filtered by field equality and paginated.

        Empty ``filters`` (default) → every service in the dataset.
        Each keyword argument becomes a query-param filter with AND semantics;
        values are coerced to strings (HTTP query params are strings; backend
        also string-coerces both sides).

        **Pagination**: ``size`` controls page size; default ``-1`` returns all
        results in one shot (no pagination headers). When ``size > 0``, the
        backend slices and returns the requested page (1-indexed via ``page``);
        the last page comes back short or empty when exhausted. Standard idiom:

            page = 1
            while True:
                batch = client.list_agents(ds, page=page, size=50, **filters)
                if not batch:
                    break
                for s in batch:
                    ...
                page += 1

        **Match target**: the backend matches against each entry's raw
        per-type data — ``agent_card`` for a2a (original, non-transformed
        ``description``), ``service_data`` for generic, ``skill_data`` for
        skill. Fields must exist **and** equal for a match.

        **Return shape** — flat ``list[dict]``, one dict per service:
        ``{id, type, name, description, ...card_fields}``. For a2a, card
        fields include ``endpoint``, ``status``, ``skills``, etc.
        Metadata fields take precedence on key conflict — e.g. for a2a the
        top-level ``description`` is the raw card value (not the taxonomy-
        facing ``build_description`` output).
        """
        params = _i.build_filter_params(filters)
        params = _i.apply_pagination(params, page, size)
        resp = self._transport.request("GET", _i.services_path(dataset), params=params)
        return _i.parse_agent_list(resp)

    def get_agent(self, dataset: str, service_id: str) -> AgentDetail:
        """Fetch a single service by sid via path-based ``GET /services/{sid}``.

        Returns ``AgentDetail`` for a2a / generic; raises
        ``UnexpectedServiceTypeError`` if the backend responds with a ZIP
        (i.e. the service is a skill).
        """
        resp = self._transport.request("GET", _i.service_path(dataset, service_id))
        return _i.parse_agent_detail(resp)

    def deregister_agent(self, dataset: str, service_id: str) -> DeregisterResponse:
        self._assert_owned(dataset, service_id)
        try:
            resp = self._transport.request("DELETE", _i.service_path(dataset, service_id))
        except NotFoundError:
            self._owned.remove(dataset, service_id)  # D3
            self._blank_endpoints.pop((dataset, service_id), None)
            raise
        result = DeregisterResponse.from_dict(resp.json())
        self._owned.remove(dataset, service_id)
        self._blank_endpoints.pop((dataset, service_id), None)
        return result

    # ── Team-agent helpers ───────────────────────────────────────────────────

    def register_blank_agent(
        self,
        dataset: str,
        endpoint: str,
        service_id: str | None = None,
        persistent: bool = True,
    ) -> RegisterResponse:
        """Register a blank/idle agent into the idle pool.

        The blank AgentCard is::

            {"name": "_BlankAgent_<endpoint>",
             "description": "__BLANK__",             # BLANK_DESCRIPTION_SENTINEL
             "endpoint": endpoint,
             "status": "online"}                     # STATUS_ONLINE

        The ``description`` sentinel is the discovery contract; ``status``
        is the availability gate. ``list_idle_blank_agents`` matches **both**.
        The ``name`` prefix is only there to make the deterministic
        ``generate_service_id("agent", name)`` yield a distinct sid per
        endpoint (re-registering the same endpoint is idempotent; the same
        sid → backend ``status="updated"`` response).
        """
        card = _i.build_blank_agent_card(endpoint)
        result = self.register_agent(
            dataset, card, service_id=service_id, persistent=persistent
        )
        self._blank_endpoints[(dataset, result.service_id)] = endpoint
        return result

    def list_idle_blank_agents(
        self,
        dataset: str,
        n: int = 1,
    ) -> list[dict[str, Any]]:
        """Return up to ``n`` idle-and-blank agents (default ``n=1``).

        Thin wrapper over ``list_agents`` filtering by **both** the blank
        sentinel (``description=__BLANK__``) **and** the availability gate
        (``status=online``). Backend treats absent ``status`` field as
        ``online`` (default-online rule), so pre-upgrade blanks without an
        explicit status field still match. Backend does the filtering; SDK
        just caps at ``n``.

        Return shape is identical to ``list_agents``: flat dicts with ``id`` +
        raw card fields (``endpoint``, ``status``, ...).
        """
        if not isinstance(n, int) or isinstance(n, bool) or n < 0:
            raise ValueError(f"n must be a non-negative int, got {n!r}")
        if n == 0:
            return []

        agents = self.list_agents(
            dataset,
            description=_i.BLANK_DESCRIPTION_SENTINEL,
            **{_i.STATUS_FIELD: _i.STATUS_ONLINE},
        )
        return agents[:n]

    def replace_agent_card(
        self,
        dataset: str,
        service_id: str,
        agent_card: dict[str, Any],
        release_lease: bool = True,
    ) -> RegisterResponse:
        """Fully replace an owned a2a agent's card (not a partial merge).

        Routes through ``POST /api/datasets/{ds}/services/a2a`` with the
        existing ``service_id``; ``_do_register`` replaces the whole entry
        (see ``src/register/service.py``), so omitted fields are dropped —
        the opposite of ``update_agent`` (PUT upsert).

        **Endpoint auto-fill**: if ``agent_card`` does not carry a non-empty
        ``endpoint`` field, the SDK auto-fills it from the last-known
        endpoint for this sid (L1 cache → L2 ``get_agent`` → L3 ``ValueError``).
        This means callers don't have to remember to thread the endpoint
        through every replace; the original card's endpoint is preserved
        unless explicitly overridden.

        After a successful POST, the L1 endpoint cache is refreshed with
        whatever endpoint ended up in the new card.

        **Auto lease release**: when ``release_lease=True`` (default), after
        a successful POST the SDK best-effort calls ``release_my_lease`` to
        drop any reservation lease on this sid. The customer's team-agent
        flow has the teammate explicitly releasing its lease right after
        committing the team-up — auto-hooking it makes that happen without
        extra caller bookkeeping. Failure of the lease-release is logged as
        a warning and does NOT fail the replace.
        """
        self._assert_owned(dataset, service_id)
        if not isinstance(agent_card, dict):
            raise ValueError(
                f"agent_card must be a dict, got {type(agent_card).__name__}: "
                f"{agent_card!r}"
            )

        endpoint = _i.extract_endpoint(agent_card)
        if endpoint is None:
            # Auto-fill — may issue an L2 GET and/or raise ValueError (L3)
            endpoint = self._resolve_endpoint(dataset, service_id)
            agent_card = {**agent_card, _i.ENDPOINT_FIELD: endpoint}

        body = _i.build_register_agent_body(agent_card, service_id, persistent=True)
        try:
            resp = self._transport.request(
                "POST", _i.a2a_register_path(dataset), json=body
            )
        except NotFoundError:
            self._owned.remove(dataset, service_id)  # D3 parity
            self._blank_endpoints.pop((dataset, service_id), None)
            raise
        result = RegisterResponse.from_dict(resp.json())
        self._owned.add(dataset, result.service_id)  # idempotent
        # Keep L1 cache in sync with backend state
        self._blank_endpoints[(dataset, result.service_id)] = endpoint

        if release_lease:
            try:
                self.release_my_lease(dataset, result.service_id)
            except (A2XConnectionError, ServerError, NotFoundError) as exc:
                # Best-effort: connection blip, 5xx, or older backend without
                # the lease route — lease will TTL-expire either way.
                warnings.warn(
                    f"replace_agent_card succeeded but release_my_lease failed "
                    f"for {dataset}/{result.service_id}: {exc}. "
                    f"Lease will TTL-expire.",
                    stacklevel=2,
                )

        return result

    def restore_to_blank(
        self,
        dataset: str,
        service_id: str,
    ) -> RegisterResponse:
        """Overwrite an owned agent with the blank card template.

        Endpoint resolution:
          - **L1**: in-memory cache populated at ``register_blank_agent`` /
            previous ``restore_to_blank`` — zero extra HTTP in the common
            single-process flow.
          - **L2**: ``get_agent`` reads ``endpoint`` from the current card
            (works across process restarts if the non-blank card preserved
            the field).
          - **L3**: ``ValueError`` if neither path yields an endpoint.
        """
        self._assert_owned(dataset, service_id)
        endpoint = self._resolve_endpoint(dataset, service_id)
        card = _i.build_blank_agent_card(endpoint)
        # replace_agent_card refreshes the L1 cache on success, so no manual
        # update needed here.
        return self.replace_agent_card(dataset, service_id, card)

    def _resolve_endpoint(self, dataset: str, service_id: str) -> str:
        """Look up the last-known endpoint for an owned service.

        L1 (in-memory cache, populated by ``register_blank_agent`` /
        ``replace_agent_card`` / ``restore_to_blank``) → L2 (``get_agent``
        reads endpoint from the current card's metadata) → L3 ``ValueError``.

        Used by:
          - ``restore_to_blank`` to construct the blank card
          - ``replace_agent_card`` to auto-fill ``endpoint`` when caller omits it
        """
        cached = self._blank_endpoints.get((dataset, service_id))
        if cached:
            return cached
        detail = self.get_agent(dataset, service_id)
        endpoint = _i.extract_endpoint(detail.metadata)
        if endpoint is None:
            raise ValueError(
                f"No 'endpoint' available for service {service_id!r} in dataset "
                f"{dataset!r}: not in local L1 cache and not in current Agent Card. "
                "Provide 'endpoint' explicitly, or call register_blank_agent "
                "first to seed the cache."
            )
        return endpoint

    # ── Reservations (leader-side + teammate-self) ───────────────────────────

    def reserve_blank_agents(
        self,
        dataset: str,
        n: int = 1,
        ttl_seconds: int = _i.DEFAULT_RESERVATION_TTL,
        holder_id: str | None = None,
        extra_filters: dict[str, Any] | None = None,
    ) -> Reservation:
        """Reserve up to ``n`` idle blank agents, locked for ``ttl_seconds``.

        Filter is ``description=__BLANK__ AND status=online`` plus any
        additional ``extra_filters`` the caller supplies. ``holder_id`` is
        auto-generated per call (``holder_<uuid>``) unless provided —
        leaders coordinating across processes can pass a stable ID.

        The returned ``Reservation`` is a context manager: on exit it best-
        effort releases the lease so a failed P2P negotiation doesn't leave
        agents reserved until TTL.

        Raises ``ValueError`` if ``n < 0`` or ``ttl_seconds < 1``.
        """
        if not isinstance(n, int) or isinstance(n, bool) or n < 0:
            raise ValueError(f"n must be a non-negative int, got {n!r}")
        if not isinstance(ttl_seconds, int) or ttl_seconds < 1:
            raise ValueError(f"ttl_seconds must be >= 1, got {ttl_seconds!r}")
        filters: dict[str, Any] = {
            "description": _i.BLANK_DESCRIPTION_SENTINEL,
            _i.STATUS_FIELD: _i.STATUS_ONLINE,
        }
        if extra_filters:
            filters.update(extra_filters)
        body: dict[str, Any] = {
            "filters": filters,
            "n": n,
            "ttl_seconds": ttl_seconds,
        }
        if holder_id is not None:
            body["holder_id"] = holder_id
        resp = self._transport.request(
            "POST", _i.reservations_path(dataset), json=body
        )
        return Reservation.from_dict(resp.json(), dataset=dataset, client=self)

    def release_reservation(
        self,
        reservation: Reservation,
        service_ids: list[str] | None = None,
    ) -> list[str]:
        """Release leases held by this Reservation.

        - ``service_ids=None`` → bulk release ALL of holder's leases
          (DELETE /reservations/{holder_id}).
        - ``service_ids=[...]`` → release only those sids
          (DELETE /reservations/{holder_id}/{sid}, one HTTP per sid).

        Idempotent: missing leases are silently skipped. Returns the list
        of sids actually released. Marks the Reservation as released so
        context-manager exit becomes a no-op.
        """
        released: list[str] = []
        if service_ids is None:
            resp = self._transport.request(
                "DELETE",
                _i.reservation_holder_path(reservation.dataset, reservation.holder_id),
            )
            released = list(resp.json().get("released") or [])
        else:
            for sid in service_ids:
                resp = self._transport.request(
                    "DELETE",
                    _i.reservation_holder_sid_path(
                        reservation.dataset, reservation.holder_id, sid,
                    ),
                )
                released.extend(resp.json().get("released") or [])
        reservation._released = True
        return released

    def extend_reservation(
        self,
        reservation: Reservation,
        ttl_seconds: int = _i.DEFAULT_RESERVATION_TTL,
    ) -> float:
        """Extend all leases under this Reservation by ``ttl_seconds``.

        Returns the new ``expires_at_unix``. Raises ``NotFoundError`` if
        the reservation has already expired (no live leases).
        """
        if not isinstance(ttl_seconds, int) or ttl_seconds < 1:
            raise ValueError(f"ttl_seconds must be >= 1, got {ttl_seconds!r}")
        resp = self._transport.request(
            "POST",
            _i.reservation_extend_path(reservation.dataset, reservation.holder_id),
            json={"ttl_seconds": ttl_seconds},
        )
        new_expires = float(resp.json()["expires_at_unix"])
        reservation.expires_at_unix = new_expires
        reservation.ttl_seconds = ttl_seconds
        return new_expires

    def release_my_lease(self, dataset: str, service_id: str) -> bool:
        """Release ANY lease on ``service_id`` regardless of original holder.

        Teammate-side path: the agent doesn't know who locked it (leaders
        pass leases via HTTP, not via P2P). The SDK's ``_owned`` check is
        the authorization gate — you can only release a lease on a sid YOU
        registered.

        Returns ``True`` if a lease was released, ``False`` if none was held
        (idempotent — no error).
        """
        self._assert_owned(dataset, service_id)
        resp = self._transport.request(
            "DELETE", _i.service_lease_path(dataset, service_id),
        )
        return bool(resp.json().get("released"))
