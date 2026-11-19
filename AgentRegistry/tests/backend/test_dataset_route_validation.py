"""公开 GET 路由（auth-config / lease-config）的数据集名校验。

历史缺陷：这两个公开路由在进入服务层之前，直接拿 ``database_dir / dataset``
做 ``.exists()`` 探测。调用方可控的名字（``a\\b``、``..`` 等）会让这次探测
落在注册表根目录之外，形成任意相对路径的目录存在性探测。

修复后路由复用服务层 ``existing_dataset_dir()``：非法名 → 400，合法但不
存在 → 404，两者不能互相冒充。
"""
from __future__ import annotations

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
