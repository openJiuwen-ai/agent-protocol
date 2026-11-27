from __future__ import annotations

import asyncio
import json
from dataclasses import replace
from datetime import datetime, timedelta, timezone
from types import SimpleNamespace

import pytest

pytest.importorskip("ath")

import ath.client.async_base as upstream
import httpx
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

from a4p.integrations.ath import (
    ATHClient,
    ATHConfig,
    ATHError,
    ATHOperation,
    AuthorizationRequired,
    Authorized,
    GatewaySession,
    MemoryRegistrationStore,
)


@pytest.fixture
def gateway(monkeypatch):
    config = ATHConfig(
        gateway_url="https://gateway.example",
        agent_id="https://agent.example/.well-known/agent.json",
        private_key=ec.generate_private_key(ec.SECP256R1()).private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption(),
        ),
        developer_id="test",
        developer_name="Test",
        purpose="Test ATH integration",
        provider_id="shop",
        operations={"add": ATHOperation("POST", "/cart/add", frozenset({"cart:write"}))},
    )
    requests = []
    state = SimpleNamespace(
        registration_status="approved", registered=False, exchange_error=None,
        exchange_timeout=False, token_scopes=["cart:write"],
    )

    async def handle(request):
        path = request.url.path
        requests.append(path)
        if path == "/.well-known/ath.json":
            assert "X-ATH-User-Token" not in request.headers
            return httpx.Response(200, json={
                "ath_version": "0.1", "gateway_id": config.gateway_url,
                "agent_registration_endpoint": f"{config.gateway_url}/ath/agents/register",
            })
        assert request.headers["X-ATH-User-Token"] == "gateway-session-token"
        if path == "/auth/me":
            return httpx.Response(200, json={"id": "user-1"})
        if path.startswith("/ath/agents/"):
            status = {
                "agent_status": state.registration_status,
                "client_id": "client-1",
                "approved_providers": [{"provider_id": "shop", "approved_scopes": ["cart:write"]}],
                "approval_expires": "2099-01-01T00:00:00.000Z",
            }
            if request.method == "POST":
                assert not state.registered
                state.registered = True
                payload = json.loads(request.content)
                assert payload["agent_attestation"]
                assert payload["requested_providers"] == [{"provider_id": "shop", "scopes": ["cart:write"]}]
                return httpx.Response(201, json={**status, "client_secret": "shop-registration-secret"})
            return httpx.Response(200, json={**status, "agent_id": config.agent_id})
        if path == "/ath/authorize":
            payload = json.loads(request.content)
            assert payload["state"] and payload["agent_attestation"]
            assert payload["user_redirect_uri"] == f"{config.gateway_url}/ui/callback"
            assert "Authorization" not in request.headers
            return httpx.Response(200, json={
                "authorization_url": "https://shop.example/consent?state=provider-state",
                "ath_session_id": "gateway-oauth-session",
            })
        if path == "/ath/token":
            payload = json.loads(request.content)
            assert payload["ath_session_id"] == "gateway-oauth-session"
            assert payload["code"] == "exchange"
            assert payload["client_secret"] == "shop-registration-secret"
            assert payload["agent_attestation"]
            if state.exchange_timeout:
                raise httpx.ReadTimeout("The Gateway response was lost", request=request)
            if state.exchange_error:
                return httpx.Response(400, json=state.exchange_error)
            return httpx.Response(200, json={
                "agent_id": config.agent_id, "provider_id": "shop", "expires_in": 60,
                "access_token": "shop-access-token", "effective_scopes": state.token_scopes,
                "scope_intersection": {},
            })
        if path == "/ath/proxy/shop/cart/add":
            assert request.headers["Authorization"] == "Bearer shop-access-token"
            assert request.headers["X-ATH-Agent-ID"] == config.agent_id
            assert json.loads(request.content) == {"quantity": 1}
            return httpx.Response(200, json={"added": True})
        if path == "/ath/revoke":
            assert json.loads(request.content)["token"] == "shop-access-token"
            return httpx.Response(200, json={})
        pytest.fail(f"Unexpected ATH endpoint {path}")

    def make_http_client(**kwargs):
        return httpx.AsyncClient(transport=httpx.MockTransport(handle), **kwargs)

    monkeypatch.setattr(upstream, "httpx", SimpleNamespace(AsyncClient=make_http_client))
    return config, GatewaySession("user-1", "gateway-session-token"), state, requests


def test_exchange_is_session_bound_and_consumed_once(gateway):
    config, session, state, requests = gateway

    async def scenario():
        async with ATHClient(config, session) as client:
            assert (await client.discover()).ath_version == "0.1"
            assert (await client.ensure_registered()).client_id == "client-1"
            pending = await client.begin_authorization("app-a")
            assert isinstance(pending, AuthorizationRequired)
            assert await client.begin_authorization("app-a") == pending
            with pytest.raises(ATHError) as wrong_session:
                await client.resume_authorization("app-b", pending.transaction_id)
            assert wrong_session.value.code == "invalid_transaction"
            assert "/ath/token" not in requests

            state.exchange_error = {"code": "SESSION_NOT_FOUND", "message": "Authorization not yet completed"}
            assert await client.resume_authorization("app-a", pending.transaction_id) == pending
            state.exchange_error = None
            outcomes = await asyncio.gather(
                client.resume_authorization("app-a", pending.transaction_id),
                client.resume_authorization("app-a", pending.transaction_id),
                return_exceptions=True,
            )
            assert isinstance(outcomes[0], Authorized)
            assert isinstance(outcomes[1], ATHError)
            assert outcomes[1].code == "invalid_transaction"
            assert requests.count("/ath/token") == 2
            assert "shop-access-token" not in repr(outcomes[0])

            with pytest.raises(ATHError) as other_call:
                await client.call_api("app-b", "add", body={"quantity": 1})
            assert other_call.value.code == "authorization_required"
            assert await client.call_api("app-a", "add", body={"quantity": 1}) == {"added": True}
            await client.revoke("app-a")
            with pytest.raises(ATHError) as revoked:
                await client.call_api("app-a", "add", body={"quantity": 1})
            assert revoked.value.code == "authorization_required"
        with pytest.raises(ATHError) as closed:
            await client.discover()
        assert closed.value.code == "client_closed"
        assert requests.count("/ath/proxy/shop/cart/add") == 1

    asyncio.run(scenario())


def test_pending_registration_refreshes_and_denial_consumes_transaction(gateway):
    config, session, state, requests = gateway

    async def scenario():
        async with ATHClient(config, session) as client:
            state.registration_status = "pending"
            with pytest.raises(ATHError) as rejected:
                await client.begin_authorization("app-a")
            assert rejected.value.code == "registration_pending"
            assert "/ath/authorize" not in requests

            state.registration_status = "approved"
            pending = await client.begin_authorization("app-a")
            assert isinstance(pending, AuthorizationRequired)
            state.exchange_error = {"code": "INTERNAL_ERROR", "message": "User denied the authorization request"}
            with pytest.raises(ATHError) as denied:
                await client.resume_authorization("app-a", pending.transaction_id)
            assert denied.value.code == "authorization_denied"
            with pytest.raises(ATHError) as duplicate:
                await client.resume_authorization("app-a", pending.transaction_id)
            assert duplicate.value.code == "invalid_transaction"
            assert requests.count("/ath/token") == 1

    asyncio.run(scenario())


def test_wrong_gateway_user_and_insufficient_grant_do_not_reach_service(gateway):
    config, session, state, requests = gateway

    async def scenario():
        async with ATHClient(config, replace(session, user_id="user-2")) as wrong_client:
            with pytest.raises(ATHError) as wrong_user:
                await wrong_client.ensure_registered()
            assert wrong_user.value.code == "gateway_user_mismatch"
            assert "/ath/agents/register" not in requests

        async with ATHClient(config, session) as client:
            pending = await client.begin_authorization("app-a")
            assert isinstance(pending, AuthorizationRequired)
            state.token_scopes = []
            with pytest.raises(ATHError) as insufficient:
                await client.resume_authorization("app-a", pending.transaction_id)
            assert insufficient.value.code == "insufficient_scope"
            with pytest.raises(ATHError) as missing:
                await client.call_api("app-a", "add")
            assert missing.value.code == "authorization_required"
            assert "/ath/proxy/shop/cart/add" not in requests

    asyncio.run(scenario())


def test_expired_or_cancelled_authorization_cannot_be_exchanged(gateway, monkeypatch):
    config, session, _, requests = gateway
    now = datetime.now(timezone.utc)
    clock = SimpleNamespace(now=now)
    monkeypatch.setattr("a4p.integrations.ath.client.utcnow", lambda: clock.now)
    monkeypatch.setattr("a4p.integrations.ath.sessions.utcnow", lambda: clock.now)

    async def scenario():
        async with ATHClient(config, session) as client:
            pending = await client.begin_authorization("app-a")
            assert isinstance(pending, AuthorizationRequired)
            clock.now += timedelta(seconds=config.authorization_ttl)
            with pytest.raises(ATHError) as expired:
                await client.resume_authorization("app-a", pending.transaction_id)
            assert expired.value.code == "authorization_expired"

            pending = await client.begin_authorization("app-a")
            assert isinstance(pending, AuthorizationRequired)
            await client.cancel_authorization("app-a", pending.transaction_id)
            with pytest.raises(ATHError) as cancelled:
                await client.resume_authorization("app-a", pending.transaction_id)
            assert cancelled.value.code == "invalid_transaction"
            assert "/ath/token" not in requests

    asyncio.run(scenario())


def test_new_grant_cleans_up_expired_grants_from_other_sessions(gateway, monkeypatch):
    config, session, _, _ = gateway
    clock = SimpleNamespace(now=datetime.now(timezone.utc))
    monkeypatch.setattr("a4p.integrations.ath.client.utcnow", lambda: clock.now)
    monkeypatch.setattr("a4p.integrations.ath.sessions.utcnow", lambda: clock.now)

    async def scenario():
        async with ATHClient(config, session) as client:
            for name in ("first", "second", "third"):
                pending = await client.begin_authorization(name)
                assert isinstance(await client.resume_authorization(name, pending.transaction_id), Authorized)
                clock.now += timedelta(seconds=30)
            assert set(client._grants) == {"second", "third"}

    asyncio.run(scenario())


def test_exchange_timeout_does_not_replay_token_request(gateway):
    config, session, state, requests = gateway

    async def scenario():
        async with ATHClient(config, session) as client:
            pending = await client.begin_authorization("app-a")
            assert isinstance(pending, AuthorizationRequired)
            state.exchange_timeout = True
            with pytest.raises(ATHError) as timeout:
                await client.resume_authorization("app-a", pending.transaction_id)
            assert timeout.value.code == "transport_error"
            with pytest.raises(ATHError) as retry:
                await client.resume_authorization("app-a", pending.transaction_id)
            assert retry.value.code == "invalid_transaction"
            assert requests.count("/ath/token") == 1

    asyncio.run(scenario())


def test_registration_can_be_saved_after_a_temporary_store_failure(gateway):
    config, session, _, requests = gateway

    class TemporaryFailureStore(MemoryRegistrationStore):
        attempt = 0

        async def put(self, key, credentials):
            self.attempt += 1
            if self.attempt == 1:
                raise OSError("Simulated temporary store failure")
            await super().put(key, credentials)

    async def scenario():
        store = TemporaryFailureStore()
        async with ATHClient(config, session, registration_store=store) as client:
            with pytest.raises(OSError):
                await client.ensure_registered()
            assert (await client.ensure_registered()).client_id == "client-1"
        assert requests.count("/ath/agents/register") == 1
        assert requests.count("/ath/agents/client-1") == 1

    asyncio.run(scenario())
