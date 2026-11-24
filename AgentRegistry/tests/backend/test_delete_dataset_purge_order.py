"""DELETE /{dataset} 的副作用顺序：先删成功，才清理搜索索引。

历史缺陷：路由先 ``search_service.purge_dataset(dataset)``，再调用服务层
``delete_dataset``。名字校验和"数据集是否存在"的判断都在服务层，因此一个
合法但并不存在（或最终删不掉）的名字会先被清掉 Chroma collection 与缓存
实例，随后接口返回 400 —— 索引没了、数据集还在（或本就不存在），且不可恢复。

修复后顺序反转：被拒绝的请求不产生任何搜索侧副作用。
"""
from __future__ import annotations

import uuid

import pytest

# 服务层 validate_dataset_name / reject_unsafe_path_segment 必然拒绝的名字
# （反斜杠、点段、NUL），用 URL 百分号编码传入。
BAD_NAMES = ["a%5Cb", "%2e%2e", "bad%00name"]


def _create_dataset(client) -> str:
    name = "ds_" + uuid.uuid4().hex[:8]
    r = client.post("/api/datasets", json={"name": name})
    assert r.status_code == 200, r.text
    return name


@pytest.fixture
def purge_spy(lite_app, monkeypatch):
    """把 ``search_service.purge_dataset`` 换成记录器，返回调用列表。"""
    from a2x_registry.backend.routers import dataset as dataset_router

    calls = []
    monkeypatch.setattr(
        dataset_router.search_service,
        "purge_dataset",
        lambda name: calls.append(name),
    )
    return calls


@pytest.mark.parametrize("bad", BAD_NAMES)
def test_refused_unsafe_name_does_not_purge_index(lite_app, purge_spy, bad):
    r = lite_app.delete(f"/api/datasets/{bad}")

    assert r.status_code == 400, f"{bad} -> {r.status_code} {r.text}"
    assert purge_spy == []


def test_missing_dataset_does_not_purge_index(lite_app, purge_spy):
    """合法名但数据集不存在：400，索引必须原样保留。"""
    r = lite_app.delete("/api/datasets/no_such_dataset")

    assert r.status_code == 400, r.text
    assert "does not exist" in r.json()["detail"]
    assert purge_spy == []


def test_purge_runs_only_after_the_dataset_directory_is_gone(lite_app, monkeypatch):
    """顺序即证据：``purge_dataset`` 被调用时目录已经不在了。"""
    from a2x_registry.backend.routers import dataset as dataset_router

    name = _create_dataset(lite_app)
    ds_dir = dataset_router.get_registry_service().dataset_dir(name)
    assert ds_dir.is_dir()

    seen = []

    def _spy(purged):
        seen.append((purged, ds_dir.exists()))

    monkeypatch.setattr(dataset_router.search_service, "purge_dataset", _spy)

    r = lite_app.delete(f"/api/datasets/{name}")

    assert r.status_code == 200, r.text
    assert seen == [(name, False)], seen
    assert not ds_dir.exists()


@pytest.fixture
def auth_client(lite_app, tmp_path):
    """``lite_app`` + 已初始化的 AuthStore（鉴权开启配置）。"""
    from a2x_registry.auth.deps import set_auth_store
    from a2x_registry.auth.store import AuthStore

    store, token = AuthStore.bootstrap(data_dir=tmp_path / "auth_data")
    set_auth_store(store)
    try:
        yield lite_app, token
    finally:
        set_auth_store(None)


def test_delete_denied_by_auth_does_not_purge_index(auth_client, purge_spy):
    """鉴权拒绝发生在 handler 之前，同样不得清索引。"""
    client, token = auth_client
    name = "authds_" + uuid.uuid4().hex[:8]
    headers = {"Authorization": f"Bearer {token}"}
    r = client.post(
        "/api/datasets", json={"name": name, "auth_required": True}, headers=headers
    )
    assert r.status_code == 200, r.text
    try:
        r = client.delete(f"/api/datasets/{name}")

        assert r.status_code == 401, r.text
        assert purge_spy == []
    finally:
        client.delete(f"/api/datasets/{name}", headers=headers)
