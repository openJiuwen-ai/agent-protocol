"""Contract tests for the etcd backend (EtcdClient / EtcdTableRepo).

Three layers:

1. **Pure-function tests** (no network) — key helpers and order parsing.
2. **Repo contract tests** against an **in-memory fake client** — exercise all
   ``EtcdTableRepo`` logic (CRUD, ``_meta`` routing, ordering,
   pagination, idempotency) deterministically, without needing a live etcd. This
   guards the shared behaviour promised by ``TableRepo``.
3. **Live-client primitives** against a **real etcd** grpc-gateway — verify the
   wire protocol (``get/put/delete/range/ping``). Connect to
   ``A2X_TEST_ETCD_ENDPOINT`` (default ``http://127.0.0.1:2379``) and **skip**
   when unreachable (CI runs a docker etcd).
"""

from __future__ import annotations

import copy
import os
import uuid
from typing import Any, List, Optional, Tuple

import pytest


# ── 无网络：纯函数 ─────────────────────────────────────────────

def test_prefix_range_end_increments_last_byte():
    from a2x_registry.register.etcd_client import _prefix_range_end
    assert _prefix_range_end(b"images/") == b"images0"
    assert _prefix_range_end(b"a2x-registry/images/") == b"a2x-registry/images0"


def test_parse_order_item_field_and_direction():
    from a2x_registry.register.etcd_repo import _parse_order_item
    assert _parse_order_item("framework asc") == ("framework", False)
    assert _parse_order_item("version_key desc") == ("version_key", True)
    assert _parse_order_item("data.created_at desc") == ("data.created_at", True)
    assert _parse_order_item("node") == ("node", False)  # default asc
    with pytest.raises(Exception):
        _parse_order_item("framework asc extra")


def test_extract_value_top_level_and_data():
    from a2x_registry.register.etcd_repo import _extract_value
    row = {"framework": "opencode", "data": {"created_at": "2026-01-01T00:00:00Z"}}
    assert _extract_value(row, "framework") == "opencode"
    assert _extract_value(row, "data.created_at") == "2026-01-01T00:00:00Z"
    assert _extract_value(row, "data.missing") is None


# ── 无网络：txn wire 契约（防 etcd 3.4.x 兼容回归） ───────────
#
# ``create`` / CAS ``put`` 走 /v3/kv/txn 的 compare 字段。etcd proto 中
# create_revision / mod_revision 是 int64：JSON 表示必须是**非空**数字字符串
# （"0" / "42"）。若退化为空串 ""，etcd 3.5.x 的 grpc-gateway 可能宽容处理，
# 而 3.4.x 严格按 int64 解析会直接拒绝请求 —— fake client 测不出这类回归，
# 所以这里在 wire 层锁定请求体形状（不需要真实 etcd，恒定运行）。

def _capture_txn(monkeypatch):
    """EtcdClient that records every /v3/kv request instead of hitting etcd."""
    from a2x_registry.register import etcd_client as ec

    captured: dict = {}

    def fake_post(self, path, body):
        captured["path"] = path
        captured["body"] = body
        return {"succeeded": True}

    monkeypatch.setattr(ec.EtcdClient, "_post", fake_post)
    client = ec.EtcdClient(endpoint="http://etcd.invalid", namespace="wirens")
    return client, captured


def test_create_txn_wire_body_create_revision_non_empty_int64(monkeypatch):
    """create(): compare.create_revision 必须是 "0"（非空整数字符串）。"""
    import base64 as b64
    client, captured = _capture_txn(monkeypatch)

    assert client.create("images/s1", {"v": 1}) is True
    assert captured["path"] == "/v3/kv/txn"
    cmp = captured["body"]["compare"][0]
    # 空串 "" 会让 etcd 3.4.x 的 int64 解析失败；必须是 "0"
    assert cmp["create_revision"] == "0"
    assert cmp["target"] == "CREATE"
    assert cmp["result"] == "EQUAL"
    assert cmp["key"] == b64.b64encode(b"wirens/images/s1").decode()
    # success 分支是 put-if-not-exists；failure 为空（已存在时不写）
    assert captured["body"]["success"][0]["request_put"]["key"] == cmp["key"]
    assert captured["body"]["failure"] == []


def test_cas_put_txn_wire_body_mod_revision_non_empty_int64(monkeypatch):
    """CAS put(): compare.mod_revision 必须是原样非空数字字符串。"""
    client, captured = _capture_txn(monkeypatch)

    assert client.put("images/s1", {"v": 2}, mod_revision="42") is True
    assert captured["path"] == "/v3/kv/txn"
    cmp = captured["body"]["compare"][0]
    assert cmp["mod_revision"] == "42"   # str(rev)，绝不可能是 ""
    assert cmp["target"] == "MOD"
    assert cmp["result"] == "EQUAL"


def test_plain_put_uses_kv_put_not_txn(monkeypatch):
    """无 mod_revision 的 put 走 /v3/kv/put（非 txn），value 为 JSON 的 base64。"""
    import base64 as b64
    import json as json_mod
    client, captured = _capture_txn(monkeypatch)

    client.put("images/s1", {"v": 1})
    assert captured["path"] == "/v3/kv/put"
    assert captured["body"]["key"] == b64.b64encode(b"wirens/images/s1").decode()
    assert json_mod.loads(b64.b64decode(captured["body"]["value"])) == {"v": 1}


# ── fake client（内存版，复现 namespace-relative 语义） ────────

class _FakeEtcd:
    """In-memory stand-in for ``EtcdClient`` with the same method surface.

    Values are JSON-encodable (dicts / str); ``get/put`` copy to avoid the
    tests mutating shared state. Implements the txn atomic semantics needed by
    the repo: ``create`` (put-if-not-exists) and ``put(mod_revision=...)`` CAS,
    tracking a per-key integer revision.
    """

    def __init__(self, namespace: str) -> None:
        self.data: dict = {}
        self._rev: dict = {}
        self.cas_fail_next = False   # test hook: force the next CAS write to abort
        self.namespace = namespace

    def _bump(self, key: str) -> int:
        rev = self._rev.get(key, 0) + 1
        self._rev[key] = rev
        return rev

    def ping(self) -> None:
        return

    def get(self, key: str) -> Optional[Any]:
        return copy.deepcopy(self.data.get(key))

    def get_rev(self, key: str) -> Optional[Tuple[Any, str]]:
        if key not in self.data:
            return None
        return copy.deepcopy(self.data[key]), str(self._rev.get(key, 0))

    def create(self, key: str, value: Any) -> bool:
        if key in self.data:
            return False
        self.data[key] = copy.deepcopy(value)
        self._bump(key)
        return True

    def put(self, key: str, value: Any, mod_revision: Optional[Any] = None):
        if mod_revision is not None:
            if self.cas_fail_next:
                self.cas_fail_next = False
                return False
            if str(self._rev.get(key, 0)) != str(mod_revision):
                return False
            self.data[key] = copy.deepcopy(value)
            self._bump(key)
            return True
        self.data[key] = copy.deepcopy(value)
        self._bump(key)
        return None

    def delete(self, key: str) -> bool:
        exists = key in self.data
        self.data.pop(key, None)
        if exists:
            self._bump(key)
        return exists

    def range(self, prefix: str) -> List[Tuple[str, Any]]:
        out = []
        for key, value in self.data.items():
            if key.startswith(prefix):
                out.append((key, copy.deepcopy(value)))
        return out


@pytest.fixture
def fake_client() -> _FakeEtcd:
    return _FakeEtcd(f"test_{uuid.uuid4().hex[:8]}")


@pytest.fixture
def repo(fake_client):
    from a2x_registry.register.etcd_repo import EtcdTableRepo
    return EtcdTableRepo(fake_client)


# ── repo 契约：registry 元数据 ─────────────────────────────────

def test_create_registry_and_kind(repo):
    repo.create_registry("images", "image")
    assert repo.get_kind("images") == "image"
    # idempotent re-declare
    repo.create_registry("images", "image")
    assert repo.get_kind("images") == "image"


def test_client_create_is_atomic(fake_client):
    """create is put-if-not-exists: a second create on the same key is a no-op."""
    assert fake_client.create("_meta/images", "image") is True
    assert fake_client.create("_meta/images", "image") is False
    # value not overwritten
    assert fake_client.get("_meta/images") == "image"


def test_client_put_cas(fake_client):
    """put with a matching mod_revision commits; a stale one is aborted."""
    fake_client.put("images/s1", {"v": 1})
    value, rev = fake_client.get_rev("images/s1")
    assert fake_client.put("images/s1", {"v": 2}, mod_revision=rev) is True
    assert fake_client.get("images/s1") == {"v": 2}
    # stale revision -> no write
    assert fake_client.put("images/s1", {"v": 3}, mod_revision="999") is False
    assert fake_client.get("images/s1") == {"v": 2}


def test_create_registry_rejects_unknown_kind(repo):
    from a2x_registry.register.errors import ValidationError
    with pytest.raises(ValidationError):
        repo.create_registry("bad", "nope")


def test_create_registry_rejects_underscore_prefix(repo):
    from a2x_registry.register.errors import ValidationError
    with pytest.raises(ValidationError):
        repo.create_registry("_meta", "image")  # reserved


def test_list_registries(repo):
    repo.create_registry("images", "image")
    repo.create_registry("instances", "instance")
    regs = repo.list_registries()
    assert regs.get("images") == "image"
    assert regs.get("instances") == "instance"


def test_get_kind_unknown(repo):
    assert repo.get_kind("nope") is None


# ── repo 契约：行 CRUD ────────────────────────────────────────

def test_register_get_roundtrip(repo):
    repo.create_registry("images", "image")
    entry = {
        "service_id": "opencode@v0.2.0",
        "framework": "opencode",
        "framework_version": "v0.2.0",
        "version_key": "0.2.0",
        "is_default": 1,
        "uploaded_by": "user-01",
        "data": {"created_at": "2026-01-01T00:00:00Z"},
    }
    stored = repo.register("images", entry)
    assert stored["service_id"] == entry["service_id"]
    assert stored["framework"] == "opencode"
    assert repo.get("images", "opencode@v0.2.0")["is_default"] == 1


def test_patch_promoted_and_data(repo):
    repo.create_registry("instances", "instance")
    base = {
        "service_id": "i1", "kind": "三方", "framework": "opencode",
        "framework_version": "v0.2.0", "node": "n1", "user": "u1",
        "data": {"address": "10.0.0.1:4000"},
    }
    repo.register("instances", base)
    updated = repo.patch("instances", "i1", {"data": {"address": "10.0.0.9:4000"}})
    assert updated["data"]["address"] == "10.0.0.9:4000"
    assert updated["node"] == "n1"
    # unknown column
    from a2x_registry.register.errors import ValidationError
    with pytest.raises(ValidationError):
        repo.patch("instances", "i1", {"bogus": 1})


def test_patch_missing_row_raises_not_found(repo):
    from a2x_registry.register.errors import NotFoundError
    repo.create_registry("images", "image")
    with pytest.raises(NotFoundError):
        repo.patch("images", "ghost", {"is_default": 1})


def test_patch_concurrent_modification_aborts(repo, fake_client):
    """If the CAS write is rejected (concurrent update), patch raises EtcdError."""
    from a2x_registry.register.etcd_client import EtcdError
    repo.create_registry("instances", "instance")
    repo.register("instances", {
        "service_id": "i1", "kind": "三方", "framework": "opencode",
        "framework_version": "v0.1.0", "node": "n1", "user": "u1", "data": {},
    })
    fake_client.cas_fail_next = True   # simulator: the CAS was aborted upstream
    with pytest.raises(EtcdError):
        repo.patch("instances", "i1", {"node": "n2"})


def test_deregister_idempotent(repo):
    repo.create_registry("images", "image")
    repo.register("images", {"service_id": "s1", "framework": "f", "data": {}})
    assert repo.deregister("images", "s1") is True
    assert repo.get("images", "s1") is None
    assert repo.deregister("images", "s1") is False


def test_register_unknown_registry_raises(repo):
    from a2x_registry.register.errors import NotFoundError
    with pytest.raises(NotFoundError):
        repo.register("nope", {"service_id": "x", "data": {}})


def test_query_filter_unknown_registry_returns_empty(repo):
    assert repo.query("nope") == []
    assert repo.query_paginated("nope") == ([], 0)


# ── repo 契约：query / query_paginated ────────────────────────

def _seed_instances(repo):
    repo.create_registry("instances", "instance")
    for sid, node, user, kind in [
        ("i_a", "n1", "u1", "三方"),
        ("i_b", "n1", "u2", "三方"),
        ("i_c", "n2", "u1", "九问"),
    ]:
        repo.register("instances", {
            "service_id": sid, "kind": kind, "framework": "opencode",
            "framework_version": "v0.1.0", "node": node, "user": user,
            "data": {},
        })


def test_query_equality_filter(repo):
    _seed_instances(repo)
    assert {r["service_id"] for r in repo.query("instances", {"node": "n1"})} == {"i_a", "i_b"}
    assert {r["service_id"] for r in repo.query("instances", {"user": "u1"})} == {"i_a", "i_c"}
    assert len(repo.query("instances")) == 3


def test_query_unknown_filter_column_raises(repo):
    from a2x_registry.register.errors import ValidationError
    _seed_instances(repo)
    with pytest.raises(ValidationError):
        repo.query("instances", {"bogus": 1})


def test_query_paginated_sort_and_pagination(repo):
    _seed_instances(repo)
    rows, total = repo.query_paginated(
        "instances", order_by=["framework asc", "service_id asc"],
    )
    assert total == 3
    assert [r["service_id"] for r in rows] == ["i_a", "i_b", "i_c"]
    rows, total = repo.query_paginated("instances", order_by=["service_id asc"], limit=2, offset=1)
    assert total == 3
    assert [r["service_id"] for r in rows] == ["i_b", "i_c"]


def test_query_paginated_only_status(repo):
    """only_status 下推：只保留 data.status 匹配的行（缺省视为 运行）。"""
    _seed_instances(repo)
    # 无 data.status 的种子行 → 视为 运行，only_status='运行' 全保留
    rows, total = repo.query_paginated(
        "instances", only_status="运行", order_by=["service_id asc"],
    )
    assert total == 3
    assert [r["service_id"] for r in rows] == ["i_a", "i_b", "i_c"]

    # PATCH 语义：i_a 置 停止 后被 only_status='运行' 过滤
    repo.register("instances", {
        "service_id": "i_a", "kind": "三方", "framework": "opencode",
        "framework_version": "v0.1.0", "node": "n1", "user": "u1",
        "data": {"status": "停止"},
    })
    rows, total = repo.query_paginated("instances", only_status="运行")
    assert total == 2
    assert "i_a" not in [r["service_id"] for r in rows]
    rows, total = repo.query_paginated("instances", only_status="停止")
    assert total == 1
    assert rows[0]["service_id"] == "i_a"


def test_query_paginated_data_field_sort(repo):
    repo.create_registry("images", "image")
    repo.register("images", {
        "service_id": "a@v1", "framework": "a", "framework_version": "v1",
        "version_key": "1", "is_default": 0, "uploaded_by": "sys",
        "data": {"created_at": "2026-01-01"},
    })
    repo.register("images", {
        "service_id": "a@v2", "framework": "a", "framework_version": "v2",
        "version_key": "2", "is_default": 1, "uploaded_by": "sys",
        "data": {"created_at": "2026-02-01"},
    })
    rows, _ = repo.query_paginated("images", order_by=["data.created_at desc"])
    assert [r["framework_version"] for r in rows] == ["v2", "v1"]


# ── 真实 etcd 客户端原语（未起 etcd 则 skip） ─────────────────

def _etcd_endpoint() -> str:
    return os.environ.get("A2X_TEST_ETCD_ENDPOINT", "http://127.0.0.1:2379")


@pytest.fixture(scope="module")
def etcd_live():
    from a2x_registry.register.etcd_client import EtcdClient, EtcdError
    client = EtcdClient(endpoint=_etcd_endpoint(), namespace=f"test_{uuid.uuid4().hex[:8]}")
    try:
        client.ping()
    except EtcdError:
        pytest.skip(f"etcd not reachable at {_etcd_endpoint()}; skipping live client tests")
    yield client
    for rel, _ in client.range(""):
        client.delete(rel)


def test_etcd_client_primitives(etcd_live):
    k = "images/s1"
    etcd_live.put(k, {"framework": "opencode", "data": {}})
    assert etcd_live.get(k) == {"framework": "opencode", "data": {}}
    assert etcd_live.get("nope") is None
    assert etcd_live.range("images/") == [(k, {"framework": "opencode", "data": {}})]
    # raw-string metadata value
    etcd_live.put("_meta/images", "image")
    assert etcd_live.get("_meta/images") == "image"
    assert etcd_live.delete(k) is True
    assert etcd_live.delete(k) is False
    assert etcd_live.get(k) is None


def test_etcd_create_atomic_and_idempotent(etcd_live):
    """create 走 /v3/kv/txn（compare create_revision==0）：首次写并 True，
    已存在不写并 False —— 真实 etcd 上验证 put-if-not-exists 原子语义。"""
    k = "images/create_s1"
    assert etcd_live.create(k, {"v": 1}) is True
    # 已存在 -> 不覆盖、返回 False
    assert etcd_live.create(k, {"v": 999}) is False
    assert etcd_live.get(k) == {"v": 1}
    # 删除后可再次创建
    assert etcd_live.delete(k) is True
    assert etcd_live.create(k, {"v": 2}) is True
    assert etcd_live.get(k) == {"v": 2}


def test_etcd_cas_put_with_mod_revision(etcd_live):
    """get_rev + put(mod_revision=...) 的 CAS 乐观锁：revision 匹配才写，
    过期 revision 被拒且值保持不变。"""
    k = "images/cas_s1"
    etcd_live.put(k, {"v": 1})

    value, rev = etcd_live.get_rev(k)
    assert value == {"v": 1}
    assert rev  # 非空字符串；存在的 key 其 mod_revision != "0"
    assert rev != "0"

    # 匹配的 revision -> 提交成功
    assert etcd_live.put(k, {"v": 2}, mod_revision=rev) is True
    assert etcd_live.get(k) == {"v": 2}

    # 过期的 revision（CAS 冲突）-> 拒绝写入，值保持
    assert etcd_live.put(k, {"v": 3}, mod_revision=rev) is False
    assert etcd_live.get(k) == {"v": 2}

    # 新 revision 再次成功
    _, rev2 = etcd_live.get_rev(k)
    assert etcd_live.put(k, {"v": 4}, mod_revision=rev2) is True
    assert etcd_live.get(k) == {"v": 4}


def test_etcd_missing_endpoint_value_error():
    from a2x_registry.register.etcd_client import EtcdClient
    with pytest.raises(ValueError):
        EtcdClient(endpoint="127.0.0.1:2379")  # no scheme