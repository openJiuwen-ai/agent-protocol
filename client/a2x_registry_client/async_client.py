"""Asynchronous client entry point.

``AsyncA2XRegistryClient`` mirrors ``A2XRegistryClient`` one-to-one: same methods, same
parameters, same return types, same exceptions — only every method is a
coroutine and HTTP flows through ``httpx.AsyncClient``. ``OwnershipStore`` is
synchronous by design, so its writes are dispatched via
``asyncio.to_thread`` to keep the event loop unblocked.
"""

from __future__ import annotations

import asyncio
import warnings
from pathlib import Path
from typing import Any, Literal

from . import _internal as _i
from .auth import resolve_credentials
from .errors import (
    A2XConnectionError,
    NotFoundError,
    NotOwnedError,
    ServerError,
    ValidationError,
)
from .heartbeat import AsyncHeartbeatRegistry, AsyncHeartbeatRenewer
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
from .transport import AsyncHTTPTransport


class AsyncA2XRegistryClient:
    def __init__(
        self,
        base_url: str | None = None,
        timeout: float = 30.0,
        api_key: str | None = None,
        ownership_file: Path | str | Literal[False] | None = None,
    ) -> None:
        # Same credential resolution as the sync client — see
        # ``A2XRegistryClient.__init__`` for the two-tier precedence rule.
        api_key, base_url = resolve_credentials(api_key, base_url)
        self._base_url = _i.normalize_base_url(base_url)
        self._timeout = timeout
        self._api_key = api_key
        self._transport = AsyncHTTPTransport(
            base_url=self._base_url,
            timeout=timeout,
            headers=_i.build_default_headers(api_key),
        )
        self._owned = OwnershipStore(
            file_path=_i.resolve_ownership_file(ownership_file),
            base_url=self._base_url,
        )
        # L1 cache for restore_to_blank (see A2XRegistryClient.__init__ for rationale).
        # Pure in-memory dict; the event loop serialises access so no lock needed.
        self._blank_endpoints: dict[tuple[str, str], str] = {}
        # Async heartbeat renewers (one asyncio.Task per (ds, sid)).
        # Created on register_agent(auto_renew=True) when the server grants
        # a lease; cancelled on aclose() / shutdown().
        self._renewers = AsyncHeartbeatRegistry()

    # ── Read-only config exposure ────────────────────────────────────────────

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

    async def aclose(self) -> None:
        """Close transport + cancel background renewers (best-effort)."""
        await self._renewers.shutdown_all()
        await self._transport.aclose()

    async def __aenter__(self) -> "AsyncA2XRegistryClient":
        return self

    async def __aexit__(self, *_exc: Any) -> None:
        await self.aclose()

    # ── Ownership guard ──────────────────────────────────────────────────────

    def _assert_owned(self, dataset: str, service_id: str) -> None:
        # Pure in-memory check — no need to leave the event loop.
        if not self._owned.contains(dataset, service_id):
            raise NotOwnedError(dataset, service_id)

    # ── Datasets ─────────────────────────────────────────────────────────────

    async def create_dataset(
        self,
        name: str,
        embedding_model: str = _i.DEFAULT_EMBEDDING_MODEL,
        formats: Any = _i.UNSET,
        auth_required: bool = False,
        lease_config: dict[str, Any] | None = None,
    ) -> DatasetCreateResponse:
        """Async mirror of ``A2XRegistryClient.create_dataset``."""
        body = _i.build_create_dataset_body(
            name, embedding_model, formats, auth_required, lease_config,
        )
        resp = await self._transport.request("POST", _i.DATASETS_ROOT, json=body)
        return DatasetCreateResponse.from_dict(resp.json())

    async def create_principal(
        self,
        handle: str,
        role: str,
        namespaces: list[str] | None = None,
        note: str | None = None,
    ) -> PrincipalCreateResponse:
        """Async mirror of ``A2XRegistryClient.create_principal``."""
        body = _i.build_create_principal_body(handle, role, namespaces, note)
        resp = await self._transport.request("POST", _i.AUTH_PRINCIPALS_ROOT, json=body)
        return PrincipalCreateResponse.from_dict(resp.json())

    async def delete_dataset(self, name: str) -> DatasetDeleteResponse:
        try:
            resp = await self._transport.request("DELETE", _i.dataset_path(name))
        except ValidationError:
            await asyncio.to_thread(self._owned.remove_dataset, name)  # D6
            raise
        result = DatasetDeleteResponse.from_dict(resp.json())
        await asyncio.to_thread(self._owned.remove_dataset, name)
        return result

    # ── Agents ───────────────────────────────────────────────────────────────

    async def register_agent(
        self,
        dataset: str,
        agent_card: dict[str, Any],
        service_id: str | None = None,
        persistent: bool = True,
        lease_ttl: int | None = None,
        auto_renew: bool = False,
    ) -> RegisterResponse:
        """See ``A2XRegistryClient.register_agent`` — async mirror.

        ``auto_renew=True`` schedules an ``asyncio.Task`` on the current
        event loop (not a thread). The task is cancelled by ``aclose()``
        and ``shutdown()``.
        """
        body = _i.build_register_agent_body(agent_card, service_id, persistent)
        if lease_ttl is not None:
            body["lease_ttl"] = int(lease_ttl)
        resp = await self._transport.request("POST", _i.a2a_register_path(dataset), json=body)
        result = RegisterResponse.from_dict(resp.json())
        if persistent:  # D4
            await asyncio.to_thread(self._owned.add, dataset, result.service_id)
        if auto_renew and result.lease_ttl is not None:
            self._renewers.add(AsyncHeartbeatRenewer(
                dataset=dataset,
                service_id=result.service_id,
                ttl_seconds=result.lease_ttl,
                heartbeat_fn=lambda ds, sid: self.heartbeat(ds, sid),
            ))
        return result

    # ── Heartbeat / lifecycle (async mirror) ──────────────────────────────────

    async def heartbeat(
        self,
        dataset: str,
        service_id: str,
        status: str | None = None,
    ) -> dict[str, Any]:
        """Extend the lease. See sync ``A2XRegistryClient.heartbeat`` for
        full semantics."""
        self._assert_owned(dataset, service_id)
        body: dict[str, Any] = {}
        if status is not None:
            body["status"] = status
        resp = await self._transport.request(
            "POST", _i.heartbeat_path(dataset, service_id), json=body,
        )
        return resp.json()

    async def drain(
        self,
        dataset: str,
        service_id: str,
        *,
        reason: str = "drain",
    ) -> PatchResponse:
        """status=offline, keep entry. See sync ``drain``."""
        return await self.update_agent(dataset, service_id, {"status": "offline"})

    async def shutdown(
        self,
        *,
        sids: list[tuple[str, str]] | None = None,
        dataset: str | None = None,
        permanent: bool = False,
        reason: str = "explicit",
        timeout: float = 2.0,
        raise_on_error: bool = False,
    ) -> dict[str, list]:
        """Revoke heartbeat leases (or full deregister with permanent=True).
        See sync ``shutdown`` for selection semantics."""
        if sids is not None:
            targets = list(sids)
        elif dataset is not None:
            targets = [
                (dataset, s) for s in
                await asyncio.to_thread(self._owned.list, dataset)
            ]
        else:
            datasets = await asyncio.to_thread(self._owned.datasets)
            targets = [
                (ds, sid)
                for ds in datasets
                for sid in await asyncio.to_thread(self._owned.list, ds)
            ]

        # Cancel renewers first so they don't race the revoke.
        for ds, sid in targets:
            await self._renewers.remove(ds, sid)

        revoked: list = []
        errors: list = []
        for ds, sid in targets:
            try:
                if permanent:
                    await self.deregister_agent(ds, sid)
                else:
                    await self._transport.request(
                        "DELETE", _i.heartbeat_path(ds, sid),
                        json={"permanent": False},
                    )
                revoked.append((ds, sid))
            except Exception as exc:  # noqa: BLE001
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

    async def update_agent(
        self,
        dataset: str,
        service_id: str,
        fields: dict[str, Any],
    ) -> PatchResponse:
        self._assert_owned(dataset, service_id)
        try:
            resp = await self._transport.request(
                "PUT", _i.service_path(dataset, service_id), json=fields
            )
        except NotFoundError:
            await asyncio.to_thread(self._owned.remove, dataset, service_id)  # D3
            raise
        return PatchResponse.from_dict(resp.json())

    async def set_status(
        self,
        dataset: str,
        service_id: str,
        status: str,
    ) -> PatchResponse:
        """See ``A2XRegistryClient.set_status``."""
        body = _i.build_status_body(status)
        self._assert_owned(dataset, service_id)
        try:
            resp = await self._transport.request(
                "PUT", _i.service_path(dataset, service_id), json=body
            )
        except NotFoundError:
            await asyncio.to_thread(self._owned.remove, dataset, service_id)  # D3
            raise
        return PatchResponse.from_dict(resp.json())

    async def list_agents(
        self,
        dataset: str,
        *,
        page: int = 1,
        size: int = -1,
        **filters: Any,
    ) -> list[dict[str, Any]]:
        """See ``A2XRegistryClient.list_agents``."""
        params = _i.build_filter_params(filters)
        params = _i.apply_pagination(params, page, size)
        resp = await self._transport.request(
            "GET", _i.services_path(dataset), params=params
        )
        return _i.parse_agent_list(resp)

    async def get_agent(self, dataset: str, service_id: str) -> AgentDetail:
        """See ``A2XRegistryClient.get_agent``."""
        resp = await self._transport.request(
            "GET", _i.service_path(dataset, service_id)
        )
        return _i.parse_agent_detail(resp)

    async def deregister_agent(self, dataset: str, service_id: str) -> DeregisterResponse:
        self._assert_owned(dataset, service_id)
        try:
            resp = await self._transport.request(
                "DELETE", _i.service_path(dataset, service_id)
            )
        except NotFoundError:
            await asyncio.to_thread(self._owned.remove, dataset, service_id)  # D3
            self._blank_endpoints.pop((dataset, service_id), None)
            raise
        result = DeregisterResponse.from_dict(resp.json())
        await asyncio.to_thread(self._owned.remove, dataset, service_id)
        self._blank_endpoints.pop((dataset, service_id), None)
        return result

    # ── Team-agent helpers ───────────────────────────────────────────────────

    async def register_blank_agent(
        self,
        dataset: str,
        endpoint: str,
        service_id: str | None = None,
        persistent: bool = True,
    ) -> RegisterResponse:
        """See ``A2XRegistryClient.register_blank_agent``."""
        card = _i.build_blank_agent_card(endpoint)
        result = await self.register_agent(
            dataset, card, service_id=service_id, persistent=persistent
        )
        self._blank_endpoints[(dataset, result.service_id)] = endpoint
        return result

    async def list_idle_blank_agents(
        self,
        dataset: str,
        n: int = 1,
    ) -> list[dict[str, Any]]:
        """See ``A2XRegistryClient.list_idle_blank_agents``. One HTTP call."""
        if not isinstance(n, int) or isinstance(n, bool) or n < 0:
            raise ValueError(f"n must be a non-negative int, got {n!r}")
        if n == 0:
            return []

        agents = await self.list_agents(
            dataset,
            description=_i.BLANK_DESCRIPTION_SENTINEL,
            **{_i.STATUS_FIELD: _i.STATUS_ONLINE},
        )
        return agents[:n]

    async def replace_agent_card(
        self,
        dataset: str,
        service_id: str,
        agent_card: dict[str, Any],
        release_lease: bool = True,
    ) -> RegisterResponse:
        """See ``A2XRegistryClient.replace_agent_card``."""
        self._assert_owned(dataset, service_id)
        if not isinstance(agent_card, dict):
            raise ValueError(
                f"agent_card must be a dict, got {type(agent_card).__name__}: "
                f"{agent_card!r}"
            )

        endpoint = _i.extract_endpoint(agent_card)
        if endpoint is None:
            endpoint = await self._resolve_endpoint(dataset, service_id)
            agent_card = {**agent_card, _i.ENDPOINT_FIELD: endpoint}

        body = _i.build_register_agent_body(agent_card, service_id, persistent=True)
        try:
            resp = await self._transport.request(
                "POST", _i.a2a_register_path(dataset), json=body
            )
        except NotFoundError:
            await asyncio.to_thread(self._owned.remove, dataset, service_id)
            self._blank_endpoints.pop((dataset, service_id), None)
            raise
        result = RegisterResponse.from_dict(resp.json())
        await asyncio.to_thread(self._owned.add, dataset, result.service_id)
        self._blank_endpoints[(dataset, result.service_id)] = endpoint

        if release_lease:
            try:
                await self.release_my_lease(dataset, result.service_id)
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

    async def restore_to_blank(
        self,
        dataset: str,
        service_id: str,
    ) -> RegisterResponse:
        """See ``A2XRegistryClient.restore_to_blank``."""
        self._assert_owned(dataset, service_id)
        endpoint = await self._resolve_endpoint(dataset, service_id)
        card = _i.build_blank_agent_card(endpoint)
        # replace_agent_card refreshes the L1 cache on success
        return await self.replace_agent_card(dataset, service_id, card)

    async def _resolve_endpoint(self, dataset: str, service_id: str) -> str:
        """See ``A2XRegistryClient._resolve_endpoint``."""
        cached = self._blank_endpoints.get((dataset, service_id))
        if cached:
            return cached
        detail = await self.get_agent(dataset, service_id)
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

    async def reserve_blank_agents(
        self,
        dataset: str,
        n: int = 1,
        ttl_seconds: int = _i.DEFAULT_RESERVATION_TTL,
        holder_id: str | None = None,
        extra_filters: dict[str, Any] | None = None,
    ) -> Reservation:
        """See ``A2XRegistryClient.reserve_blank_agents``."""
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
        resp = await self._transport.request(
            "POST", _i.reservations_path(dataset), json=body
        )
        return Reservation.from_dict(resp.json(), dataset=dataset, client=self)

    async def release_reservation(
        self,
        reservation: Reservation,
        service_ids: list[str] | None = None,
    ) -> list[str]:
        """See ``A2XRegistryClient.release_reservation``."""
        released: list[str] = []
        if service_ids is None:
            resp = await self._transport.request(
                "DELETE",
                _i.reservation_holder_path(reservation.dataset, reservation.holder_id),
            )
            released = list(resp.json().get("released") or [])
        else:
            for sid in service_ids:
                resp = await self._transport.request(
                    "DELETE",
                    _i.reservation_holder_sid_path(
                        reservation.dataset, reservation.holder_id, sid,
                    ),
                )
                released.extend(resp.json().get("released") or [])
        reservation._released = True
        return released

    async def extend_reservation(
        self,
        reservation: Reservation,
        ttl_seconds: int = _i.DEFAULT_RESERVATION_TTL,
    ) -> float:
        """See ``A2XRegistryClient.extend_reservation``."""
        if not isinstance(ttl_seconds, int) or ttl_seconds < 1:
            raise ValueError(f"ttl_seconds must be >= 1, got {ttl_seconds!r}")
        resp = await self._transport.request(
            "POST",
            _i.reservation_extend_path(reservation.dataset, reservation.holder_id),
            json={"ttl_seconds": ttl_seconds},
        )
        new_expires = float(resp.json()["expires_at_unix"])
        reservation.expires_at_unix = new_expires
        reservation.ttl_seconds = ttl_seconds
        return new_expires

    async def release_my_lease(self, dataset: str, service_id: str) -> bool:
        """See ``A2XRegistryClient.release_my_lease``."""
        self._assert_owned(dataset, service_id)
        resp = await self._transport.request(
            "DELETE", _i.service_lease_path(dataset, service_id),
        )
        return bool(resp.json().get("released"))
