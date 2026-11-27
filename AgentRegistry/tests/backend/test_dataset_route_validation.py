"""公开 GET 路由（auth-config / lease-config）的数据集名校验。

历史缺陷：这两个公开路由在进入服务层之前，直接拿 ``database_dir / dataset``
做 ``.exists()`` 探测。调用方可控的名字（``a\\b``、``..`` 等）会让这次探测
落在注册表根目录之外，形成任意相对路径的目录存在性探测。

修复后路由复用服务层 ``existing_dataset_dir()``：非法名 → 400，合法但不
存在 → 404，两者不能互相冒充。
"""
from __future__ import annotations

import uuid

import pytest

# 通过 URL 百分号编码传入：反斜杠、点段、NUL —— 都是服务层
# validate_dataset_name / reject_unsafe_path_segment 必然拒绝的关键字。
# （``%2F`` 不在此列：Starlette 会先把它当作路径分隔符，请求在路由层就是
#  404，压根到不了 handler，测不到本次修复的校验点。）
BAD_NAMES = ["a%5Cb", "%2e%2e", "bad%00name"]


@pytest.mark.parametrize("route", ["auth-config", "lease-config"])
@pytest.mark.parametrize("bad", BAD_NAMES)
def test_public_get_routes_reject_unsafe_dataset_names(lite_app, route, bad):
    r = lite_app.get(f"/api/datasets/{bad}/{route}")

    assert r.status_code == 400, f"{bad} -> {r.status_code} {r.text}"
    assert "invalid dataset name" in r.json()["detail"]


@pytest.mark.parametrize("route", ["auth-config", "lease-config"])
def test_public_get_routes_keep_404_for_missing_dataset(lite_app, route):
    """合法名但数据集不存在仍是 404（不能被校验改写成 400）。"""
    r = lite_app.get(f"/api/datasets/no_such_dataset/{route}")

    assert r.status_code == 404, r.text
    assert "not found" in r.json()["detail"]


@pytest.mark.parametrize("route", ["auth-config", "lease-config"])
def test_public_get_routes_still_serve_existing_dataset(lite_app, dataset, route):
    r = lite_app.get(f"/api/datasets/{dataset}/{route}")

    assert r.status_code == 200, r.text
    assert r.json()["dataset"] == dataset


# ---------------------------------------------------------------------------
# register-config / vector-config：直接调用服务层，校验必须在路由入口完成
# ---------------------------------------------------------------------------


@pytest.fixture
def auth_client(lite_app, tmp_path):
    """``lite_app`` with an initialized AuthStore (auth-on configuration)."""
    from a2x_registry.auth.deps import set_auth_store
    from a2x_registry.auth.store import AuthStore

    store, token = AuthStore.bootstrap(data_dir=tmp_path / "auth_data")
    set_auth_store(store)
    try:
        yield lite_app, token
    finally:
        set_auth_store(None)


READ_ROUTES = ["register-config", "vector-config"]


@pytest.mark.parametrize("route", READ_ROUTES)
@pytest.mark.parametrize("bad", BAD_NAMES)
def test_direct_service_reads_reject_unsafe_names_with_auth_off(lite_app, route, bad):
    """auth 未初始化时 ``authorize`` 曾直接返回 None，非法名一路走到服务层变成 500。"""
    r = lite_app.get(f"/api/datasets/{bad}/{route}")

    assert r.status_code == 400, f"{bad} -> {r.status_code} {r.text}"
    assert "invalid dataset name" in r.json()["detail"]


@pytest.mark.parametrize("route", READ_ROUTES)
@pytest.mark.parametrize("bad", BAD_NAMES)
def test_direct_service_reads_reject_unsafe_names_with_auth_on(auth_client, route, bad):
    """auth 已初始化时，非法名必须是 400 —— 不能被当成匿名命名空间后 500。"""
    client, _ = auth_client

    r = client.get(f"/api/datasets/{bad}/{route}")

    assert r.status_code == 400, f"{bad} -> {r.status_code} {r.text}"
    assert "invalid dataset name" in r.json()["detail"]


@pytest.mark.parametrize("route", READ_ROUTES)
@pytest.mark.parametrize("bad", BAD_NAMES)
def test_direct_service_reads_reject_unsafe_names_before_401(auth_client, route, bad):
    """鉴权开启 + 不存在的非法名：入口校验先于鉴权，返回 400 而不是 401。"""
    client, _ = auth_client

    r = client.get(f"/api/datasets/{bad}/{route}", headers={"Authorization": "Bearer nope"})

    assert r.status_code == 400, r.text


@pytest.mark.parametrize("route", READ_ROUTES)
def test_direct_service_reads_still_serve_anonymous_dataset(lite_app, dataset, route):
    r = lite_app.get(f"/api/datasets/{dataset}/{route}")

    assert r.status_code == 200, r.text
    assert r.json()["dataset"] == dataset


@pytest.mark.parametrize("route", READ_ROUTES)
def test_direct_service_reads_still_serve_anonymous_dataset_with_auth_enabled(auth_client, dataset, route):
    """auth 开启但该数据集仍是 anonymous：合法匿名请求照常 200。"""
    client, _ = auth_client

    r = client.get(f"/api/datasets/{dataset}/{route}")

    assert r.status_code == 200, r.text
    assert r.json()["dataset"] == dataset


@pytest.mark.parametrize("route", READ_ROUTES)
def test_direct_service_reads_keep_401_for_auth_required_dataset(auth_client, route):
    """校验通过后不改变原有鉴权语义：auth_required 数据集无 token 仍 401。"""
    client, token = auth_client
    name = "authds_" + uuid.uuid4().hex[:8]
    r = client.post(
        "/api/datasets",
        json={"name": name, "auth_required": True},
        headers={"Authorization": f"Bearer {token}"},
    )
    assert r.status_code == 200, r.text
    try:
        r = client.get(f"/api/datasets/{name}/{route}")
        assert r.status_code == 401, r.text
    finally:
        client.delete(f"/api/datasets/{name}", headers={"Authorization": f"Bearer {token}"})
