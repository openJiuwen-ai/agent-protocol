from __future__ import annotations

import asyncio
import importlib.util
import os
import stat
from pathlib import Path
from types import SimpleNamespace

import pytest

pytest.importorskip("ath")

import httpx

from a4p.integrations.ath import ATHError, RegistrationCredentials, RegistrationKey

_PATH = Path(__file__).resolve().parents[2] / "examples" / "ath_shop" / "run.py"
_SPEC = importlib.util.spec_from_file_location("ath_shop_demo", _PATH)
assert _SPEC is not None and _SPEC.loader is not None
demo = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(demo)


@pytest.mark.parametrize(
    ("conflict_code", "login_status", "expected_error"),
    [
        ("USER_EXISTS", 200, None),
        ("USER_EXISTS", 401, "gateway_invalid_credentials"),
        ("OTHER_CONFLICT", 200, "gateway_login_failed"),
    ],
)
def test_demo_signup_only_falls_back_for_existing_user(monkeypatch, conflict_code, login_status, expected_error):
    config = SimpleNamespace(gateway_url="https://gateway.example", timeout=5)
    paths = []

    def respond(request):
        paths.append(request.url.path)
        if request.url.path == "/auth/signup":
            return httpx.Response(409, json={"code": conflict_code})
        return httpx.Response(login_status, json={"user": {"id": "user-1"}, "token": "gateway-login-token"})

    def make_client(**kwargs):
        return httpx.AsyncClient(transport=httpx.MockTransport(respond), **kwargs)

    monkeypatch.setattr(demo, "httpx", SimpleNamespace(AsyncClient=make_client, RequestError=httpx.RequestError))
    request = demo.gateway_login(config, "test", "demo-password", signup=True)
    if expected_error:
        with pytest.raises(ATHError) as failure:
            asyncio.run(request)
        assert failure.value.code == expected_error
    else:
        assert asyncio.run(request).user_id == "user-1"
    assert paths == (["/auth/signup", "/auth/login"] if conflict_code == "USER_EXISTS" else ["/auth/signup"])


def test_demo_registration_persists_for_same_gateway_user_and_agent(tmp_path):
    cache = tmp_path / "registrations"
    key = RegistrationKey("https://gateway.example", "user-1", "https://agent.example")
    credentials = RegistrationCredentials("demo-client", "demo-client-secret")
    asyncio.run(demo.FileRegistrationStore(cache).put(key, credentials))

    store = demo.FileRegistrationStore(cache)
    assert asyncio.run(store.get(key)) == credentials
    assert asyncio.run(store.get(RegistrationKey(key.gateway_url, "user-2", key.agent_id))) is None

    if os.name == "posix":
        assert stat.S_IMODE(cache.stat().st_mode) == 0o700
        assert stat.S_IMODE(next(cache.iterdir()).stat().st_mode) == 0o600


@pytest.mark.parametrize(
    ("confirmation", "order_error", "expected_status"),
    [
        ("no", None, "added_to_cart"),
        ("yes", None, "order_created"),
        ("yes", "transport_error", "order_result_unknown"),
    ],
)
def test_shop_only_orders_after_confirmation_and_does_not_retry(
    monkeypatch, capsys, confirmation, order_error, expected_status,
):
    calls = []
    order = {"id": "order-1", "status": "confirmed", "total": 75}
    responses = {
        "profile": {"username": "shop-user", "email": "private@example.com"},
        "products": {"products": [{"id": "headphones-1", "name": "Headphones", "stock": 3}]},
        "cart_add": {"message": "Added to cart"},
        "cart": {"items": [{"name": "Cable", "quantity": 1}, {"name": "Headphones", "quantity": 2}], "total": 75},
        "orders": [{**order, "shipping_address": "private address"}],
        "order_create": order,
    }

    async def call_api(session, operation, *, body=None):
        assert session == "demo-session"
        calls.append(operation)
        if operation == "cart_add":
            assert body == {"productId": "headphones-1", "quantity": 2}
        if operation == "order_create":
            assert body["shippingAddress"] == "华为北京研究所"
            if order_error:
                raise ATHError(order_error, "Request outcome is unknown")
        return responses[operation]

    def confirm(prompt):
        assert confirmation is not None
        assert "entire cart" in prompt
        preview = capsys.readouterr().out
        assert all(value in preview for value in ("Cable", "Headphones", "75"))
        return confirmation

    monkeypatch.setattr("builtins.input", confirm)
    operations = demo.shop_operations()
    scopes = {scope for operation in operations.values() for scope in operation.scopes}
    assert scopes == {"profile", "products:read", "cart:write", "cart:read", "orders:read", "orders:write"}
    run = demo.shop(SimpleNamespace(call_api=call_api), "demo-session", "headphones")
    if order_error:
        with pytest.raises(ATHError) as failure:
            asyncio.run(run)
        assert failure.value.code == expected_status
        assert calls.count("order_create") == 1
        return

    summary = asyncio.run(run)
    assert summary["status"] == expected_status
    assert summary["shop_user"] == "shop-user"
    assert summary["recent_orders"] == [order]
    assert calls.count("order_create") == (1 if confirmation == "yes" else 0)
    assert calls.index("cart_add") < calls.index("cart") < calls.index("orders")
