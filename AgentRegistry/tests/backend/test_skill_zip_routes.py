"""技能 ZIP 下载路由的错误映射。

历史缺陷：``GET /{ds}/services/{id}``（技能单查）和
``GET /{ds}/skills/{name}/download`` 只捕获 ``FileNotFoundError``。
``get_skill_zip`` 里的名字校验（单段 / 包含性检查）抛的是 ``ValueError``，
于是非法名会被兜底的 ``except Exception`` 变成 500 —— 客户端错误却报服务端故障。
"""
from __future__ import annotations

import io
import zipfile

import pytest


def _skill_zip(name: str) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        zf.writestr("SKILL.md", f"---\nname: {name}\ndescription: demo\n---\n")
        zf.writestr("main.py", "print('hi')\n")
    return buf.getvalue()


def _upload_skill(lite_app, dataset: str, name: str = "good-skill") -> str:
    r = lite_app.post(
        f"/api/datasets/{dataset}/skills",
        files={"file": (f"{name}.zip", _skill_zip(name), "application/zip")},
    )
    assert r.status_code == 200, r.text
    return r.json()["service_id"]


@pytest.mark.parametrize("bad", ["%2e%2e", "a%5Cb"])
def test_download_route_maps_name_validation_error_to_400(lite_app, dataset, bad):
    r = lite_app.get(f"/api/datasets/{dataset}/skills/{bad}/download")

    assert r.status_code == 400, f"{bad} -> {r.status_code} {r.text}"
    assert "invalid skill name" in r.json()["detail"]


def test_single_service_zip_maps_name_validation_error_to_400(
    lite_app, dataset, monkeypatch
):
    """``GET /services/{id}`` 对技能类型返回 ZIP；校验收到的 ValueError 要是 400。"""
    from a2x_registry.backend.routers.dataset import get_registry_service

    skill_id = _upload_skill(lite_app, dataset)
    svc = get_registry_service()

    def boom(ds, name):
        raise ValueError(f"invalid skill name: {name!r}")

    monkeypatch.setattr(svc, "get_skill_zip", boom)

    r = lite_app.get(f"/api/datasets/{dataset}/services/{skill_id}")

    assert r.status_code == 400, r.text
    assert "invalid skill name" in r.json()["detail"]


def test_valid_skill_download_still_returns_zip(lite_app, dataset):
    """映射改动不能把正常下载也变成 400/404。"""
    _upload_skill(lite_app, dataset)

    r = lite_app.get(f"/api/datasets/{dataset}/skills/good-skill/download")

    assert r.status_code == 200, r.text
    with zipfile.ZipFile(io.BytesIO(r.content)) as zf:
        assert "SKILL.md" in zf.namelist()
