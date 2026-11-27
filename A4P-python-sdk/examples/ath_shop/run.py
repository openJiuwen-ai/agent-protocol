"""Browse ATH Shop, add an item to the cart, and optionally place a demo order."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import secrets
import stat
import sys
from contextlib import suppress
from getpass import getpass
from hashlib import sha256
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Any

import httpx

from a4p.integrations.ath import (
    ATHClient,
    ATHConfig,
    ATHError,
    ATHOperation,
    AuthorizationRequired,
    Authorized,
    GatewaySession,
    RegistrationCredentials,
    RegistrationKey,
)


GATEWAY_USERNAME = "a4p-user"


class DemoView:
    def show(self, step: int, message: str, data: Any = None) -> None:
        print(message, flush=True)
        if data is not None:
            print(json.dumps(data, ensure_ascii=False, indent=2), flush=True)

    def ask(self, kind: str, message: str) -> str:
        return input(message)


class FileRegistrationStore:
    """Keep the demo's registration credentials available between CLI runs."""

    def __init__(self, directory: Path) -> None:
        self._directory = directory
        try:
            directory.mkdir(mode=0o700, parents=True, exist_ok=True)
            mode = directory.lstat().st_mode
            if not stat.S_ISDIR(mode) or (os.name == "posix" and mode & 0o077):
                raise OSError("ATH registration directory must be private")
        except OSError:
            raise ATHError("registration_store_error", f"Cannot use private ATH registration directory {directory}") from None

    def _path(self, key: RegistrationKey) -> Path:
        identity = json.dumps((key.gateway_url, key.gateway_user_id, key.agent_id))
        digest = sha256(identity.encode()).hexdigest()
        return self._directory / f"{digest}.json"

    async def get(self, key: RegistrationKey) -> RegistrationCredentials | None:
        path = self._path(key)
        try:
            mode = path.lstat().st_mode
            if not stat.S_ISREG(mode) or (os.name == "posix" and mode & 0o077):
                raise OSError("ATH registration file must be private")
            data = json.loads(path.read_text(encoding="utf-8"))
            client_id, client_secret = data["client_id"], data["client_secret"]
            if not all(isinstance(value, str) and value for value in (client_id, client_secret)):
                raise ValueError("Invalid ATH registration credentials")
            return RegistrationCredentials(client_id, client_secret)
        except FileNotFoundError:
            return None
        except (OSError, ValueError, KeyError, TypeError):
            raise ATHError("registration_store_error", f"Cannot read ATH registration file {path}") from None

    async def put(self, key: RegistrationKey, credentials: RegistrationCredentials) -> None:
        path = self._path(key)
        temporary_path = None
        try:
            with NamedTemporaryFile(mode="w", encoding="utf-8", dir=self._directory, delete=False) as stream:
                temporary_path = Path(stream.name)
                json.dump({"client_id": credentials.client_id, "client_secret": credentials.client_secret}, stream)
            temporary_path.replace(path)
        except OSError:
            raise ATHError("registration_store_error", f"Cannot save ATH registration file {path}") from None
        finally:
            if temporary_path is not None:
                with suppress(OSError):
                    temporary_path.unlink(missing_ok=True)


async def gateway_login(
    config: ATHConfig, username: str, password: str, *, signup: bool,
) -> GatewaySession:
    path = "/auth/signup" if signup else "/auth/login"
    credentials = {"username": username, "password": password}
    try:
        async with httpx.AsyncClient(timeout=config.timeout) as http:
            response = await http.post(f"{config.gateway_url}{path}", json=credentials)
            if signup and response.status_code == 409:
                try:
                    conflict = response.json()
                except ValueError:
                    conflict = None
                if isinstance(conflict, dict) and conflict.get("code") == "USER_EXISTS":
                    path = "/auth/login"
                    response = await http.post(f"{config.gateway_url}{path}", json=credentials)
        if not response.is_success:
            if path == "/auth/login" and response.status_code == 401:
                raise ATHError(
                    "gateway_invalid_credentials", "Check the Gateway username and password", http_status=401,
                )
            operation = "signup" if path == "/auth/signup" else "login"
            raise ATHError(
                "gateway_login_failed", f"Gateway {operation} failed (HTTP {response.status_code})",
                http_status=response.status_code,
            )
        payload = response.json()
        user_id, token = payload["user"]["id"], payload["token"]
        if not isinstance(user_id, str) or not isinstance(token, str):
            raise ValueError("Invalid Gateway login response")
        return GatewaySession(user_id, token)
    except httpx.RequestError:
        raise ATHError("transport_error", "Could not reach the ATH Gateway") from None
    except (ValueError, KeyError, TypeError):
        raise ATHError("invalid_gateway_response", "Invalid Gateway login response") from None


async def discover(config: ATHConfig, view: DemoView) -> None:
    from ath.types import DiscoveryDocument

    try:
        async with httpx.AsyncClient(timeout=config.timeout) as http:
            response = await http.get(f"{config.gateway_url}/.well-known/ath.json")
        response.raise_for_status()
        document = DiscoveryDocument.model_validate(response.json())
    except httpx.HTTPError:
        raise ATHError("discovery_failed", "Could not retrieve the Gateway discovery document") from None
    except ValueError:
        raise ATHError("invalid_gateway_response", "Invalid Gateway discovery document") from None

    view.show(1, f"ATH Gateway v{document.ath_version} ({config.gateway_url})", [
        {"服务商": provider.provider_id, "名称": provider.display_name,
         "可用权限": provider.available_scopes, "需要审批": provider.agent_approval_required}
        for provider in document.supported_providers
    ])
    if not any(
        provider.provider_id == config.provider_id and config.scopes <= set(provider.available_scopes)
        for provider in document.supported_providers
    ):
        raise ATHError("shop_provider_unavailable", "The Gateway has no ATH Shop provider with the required scopes")


async def register(client: ATHClient, view: DemoView) -> None:
    try:
        registration = await client.ensure_registered()
    except ATHError as error:
        if error.code == "insufficient_scope" and error.http_status is None:
            raise ATHError(
                "registration_scope_upgrade_required",
                "The Agent registration lacks required scopes. Check the approved scopes in the Gateway dashboard.",
            ) from None
        raise
    view.show(2, "代理注册已确认", {"Client ID": registration.client_id, "已批准权限": sorted(registration.approved_scopes)})


async def authorize(client: ATHClient, session: str, view: DemoView) -> Authorized:
    result = await client.begin_authorization(session)
    if isinstance(result, AuthorizationRequired):
        view.show(3, "请在商城完成授权", {"授权地址": result.authorization_url})
    while isinstance(result, AuthorizationRequired):
        if view.ask("consent", "After consent, press Enter to continue, or type q to cancel: ").strip().lower() == "q":
            await client.cancel_authorization(session, result.transaction_id)
            raise ATHError("authorization_cancelled", "The authorization transaction was cancelled locally")
        result = await client.resume_authorization(session, result.transaction_id)
        if isinstance(result, AuthorizationRequired):
            view.show(3, "网关仍在等待商城授权")

    return result


def select_product(catalog: Any, search: str) -> dict[str, Any]:
    products = catalog.get("products") if isinstance(catalog, dict) else None
    if not isinstance(products, list):
        raise ATHError("invalid_catalog", "The ATH Shop returned an invalid product catalog")
    # Standalone Gateway drops proxy query strings. Search the demo catalog page locally.
    match = search.casefold()
    available = [
        product for product in products
        if isinstance(product, dict)
        and isinstance(product.get("id"), str)
        and isinstance(product.get("name"), str)
        and isinstance(product.get("stock"), int)
        and product["stock"] >= 2
        and match in f"{product['name']} {product.get('description', '')}".casefold()
    ]
    if not available:
        raise ATHError("product_not_found", "No item with at least two units in stock matched the search on the demo catalog page")
    return min(available, key=lambda product: (product["name"].casefold(), product["id"]))


def shop_operations() -> dict[str, ATHOperation]:
    return {
        "profile": ATHOperation("GET", "/auth/me", frozenset({"profile"})),
        "products": ATHOperation("GET", "/products", frozenset({"products:read"})),
        "cart_add": ATHOperation("POST", "/cart/add", frozenset({"cart:write"})),
        "cart": ATHOperation("GET", "/cart", frozenset({"cart:read"})),
        "orders": ATHOperation("GET", "/orders", frozenset({"orders:read"})),
        "order_create": ATHOperation("POST", "/orders", frozenset({"orders:write"})),
    }


async def write_once(
    client: ATHClient, session: str, operation: str, body: dict[str, Any], unknown: ATHError,
) -> Any:
    try:
        return await client.call_api(session, operation, body=body)
    except ATHError as error:
        if error.code in {"transport_error", "invalid_gateway_response"} or (error.http_status or 0) >= 500:
            raise unknown from None
        raise


def summarize_cart(cart: Any) -> dict[str, Any]:
    if (
        not isinstance(cart, dict) or not isinstance(cart.get("items"), list)
        or type(cart.get("total")) not in (int, float)
    ):
        raise ATHError("invalid_cart", "The ATH Shop returned an invalid cart")
    items = []
    for item in cart["items"]:
        if (
            not isinstance(item, dict) or not isinstance(item.get("name"), str)
            or type(item.get("quantity")) is not int or item["quantity"] < 1
        ):
            raise ATHError("invalid_cart", "The ATH Shop returned an invalid cart item")
        items.append({"product": item["name"], "quantity": item["quantity"]})
    return {"items": items, "total": cart["total"]}


def summarize_order(order: Any, invalid: ATHError) -> dict[str, Any]:
    if (
        not isinstance(order, dict) or not isinstance(order.get("id"), str) or not order["id"]
        or not isinstance(order.get("status"), str) or type(order.get("total")) not in (int, float)
    ):
        raise invalid
    return {"id": order["id"], "status": order["status"], "total": order["total"]}


async def shop(client: ATHClient, session: str, search: str, view: DemoView | None = None) -> dict[str, Any]:
    view = view or DemoView()
    profile = await client.call_api(session, "profile")
    if not isinstance(profile, dict) or not isinstance(profile.get("username"), str):
        raise ATHError("invalid_profile", "The ATH Shop returned an invalid profile")
    product = select_product(await client.call_api(session, "products"), search)
    view.show(5, f"查到商品 {product['name']}（{product['id']}），库存 {product['stock']}")
    cart_unknown = ATHError("cart_result_unknown", "Check the cart in ATH Shop before trying this write again")
    result = await write_once(client, session, "cart_add", {"productId": product["id"], "quantity": 2}, cart_unknown)
    if not isinstance(result, dict) or result.get("message") != "Added to cart":
        raise cart_unknown
    view.show(5, f"已加入购物车 × 2：{product['name']}")
    cart = summarize_cart(await client.call_api(session, "cart"))
    summary = {
        "shop_user": profile["username"], "product": product["name"], "quantity": 2,
        "status": "added_to_cart", "cart_after_add": cart,
    }
    if cart["items"]:
        view.show(5, "请核对整车商品", {"shop_user": profile["username"], "cart_to_order": cart})
        if view.ask("checkout", "Place a demo order for the entire cart? Type yes to confirm: ").strip().lower() == "yes":
            order_unknown = ATHError("order_result_unknown", "Check order history in ATH Shop before trying checkout again")
            order = await write_once(
                client, session, "order_create", {"shippingAddress": "华为北京研究所"}, order_unknown,
            )
            summary["order"] = summarize_order(order, order_unknown)
            view.show(5, "订单已创建", summary["order"])
            summary["status"] = "order_created"

    orders = await client.call_api(session, "orders")
    invalid_orders = ATHError("invalid_orders", "The ATH Shop returned invalid order history")
    if not isinstance(orders, list):
        raise invalid_orders
    summary["recent_orders"] = [summarize_order(order, invalid_orders) for order in orders[:5]]
    return summary


async def run(args: argparse.Namespace, view: DemoView | None = None, password: str | None = None,
              *, username: str = GATEWAY_USERNAME) -> None:
    view = view or DemoView()
    config = ATHConfig(
        gateway_url=args.gateway,
        agent_id=args.agent_id,
        private_key=args.agent_key.read_bytes(),
        developer_id="a4p-ath-shop",
        developer_name="A4P ATH Shop Demo",
        purpose="Browse products, add to cart, place a confirmed order, and view order history",
        provider_id="ath-shop",
        operations=shop_operations(),
        key_id=args.key_id,
        allow_insecure_http=args.allow_http,
    )
    await discover(config, view)
    registration_store = FileRegistrationStore(Path.home() / ".a4p" / "ath-shop" / "registrations")
    gateway_session = await gateway_login(
        config, username, password if password is not None else getpass("Gateway password: "), signup=args.signup,
    )
    session = secrets.token_urlsafe(24)
    async with ATHClient(config, gateway_session, registration_store=registration_store) as client:
        await register(client, view)
        grant = await authorize(client, session, view)
        try:
            view.show(4, "令牌交换成功", {"有效权限": sorted(grant.scopes), "到期时间": grant.expires_at.isoformat()})
            summary = await shop(client, session, args.search, view)
            view.show(5, "商城访问结果", summary)
        finally:
            active_error = sys.exc_info()[0]
            try:
                await client.revoke(session)
                view.show(6, "ATH Shop 访问令牌已撤销")
            except ATHError as error:
                if active_error is None:
                    raise
                view.show(6, f"撤销失败 · {error.code} — {error}")


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gateway", required=True, help="Gateway origin")
    parser.add_argument("--signup", action="store_true", help="Create a Gateway account or log in if it already exists")
    parser.add_argument("--agent-id", required=True, help="Canonical ATH Agent URI")
    parser.add_argument("--agent-key", required=True, type=Path, help="Path to the Agent's P-256 private PEM")
    parser.add_argument("--key-id", default="default", help="Key ID used in Agent attestations")
    parser.add_argument("--allow-http", action="store_true", help="Allow HTTP on isolated local demo deployments")
    parser.add_argument("--search", default="headphones", help="Search term in the demo catalog")
    return parser


def main() -> int:
    parser = argument_parser()
    args = parser.parse_args()
    if not args.search.strip():
        parser.error("--search must contain a search term")
    args.search = args.search.strip()
    try:
        asyncio.run(run(args))
    except ATHError as error:
        print(f"{error.code} — {error}", file=sys.stderr)
        if error.code == "registration_credentials_missing":
            print(
                "Restore the Agent registration credentials for this Gateway account.",
                file=sys.stderr,
            )
        return 1
    except (EOFError, KeyboardInterrupt):
        print("The demo was interrupted.", file=sys.stderr)
        return 1
    except (OSError, ValueError):
        print("Check the Gateway origin, Agent URI, and private key file.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
