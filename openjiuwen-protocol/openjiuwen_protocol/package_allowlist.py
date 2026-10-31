# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
"""企业版专家包允许名单的线上契约（元数据键 / 内部 HTTP 头 / 规范化与编解码）。

来源：``jiuwenswarm/common/schema/package_allowlist.py``（原
``jiuwenswarm/server/runtime/package_allowlist.py``）。Gateway 把名单写进
E2A 信封的 ``channel_context``，并通过内部 HTTP 头转给 server；两侧必须使用
同一份键名、长度上限与规范化规则，否则会出现「配置了名单但请求被拒」的漂移。

边界：本模块只保留**无状态**部分——常量、``normalize_packages`` 等纯函数与
信封写入；``bind_request_subject`` / ``bind_outbound_allowlist`` 等 ContextVar
状态，以及 ``allows`` 依赖的 Hub/市场包查找，均依赖具体仓的运行时，留在实现侧。
``match_expr`` 只做结构规范化（是否可求值由企业版准入层用
``openjiuwen_protocol.match_expressions`` 校验），保证传输层不会因策略差异
改变报文形状。

**空名单语义（fail-closed）**：名单为空时，受控包（Hub/内置/resources）一律
不可用，只有用户 local 根下的自有包放行（见 ``allows``）。因此 encode / decode
在遇到非法、超长或损坏内容时降级成空名单是**拒绝**而非放行，不能把空名单
改读成「未配置、不限制」。空名单和「配置了但损坏」在线上不做区分（两者的安全
效果必须一致），只在日志上区分：缺失/空白不告警，解析失败或超长打 warning。
"""

from __future__ import annotations

import base64
import json
import logging
from typing import Any

logger = logging.getLogger(__name__)

#: E2A 信封 ``channel_context`` 中承载名单的键。
PACKAGE_ALLOWLIST_METADATA_KEY: str = "package_allowlist"
#: Gateway → server 内部 HTTP 头名。
PACKAGE_ALLOWLIST_HEADER_NAME: str = "X-Jiuwenswarm-Package-Allowlist"
#: 名单 JSON 序列化后的上限（16KiB）。
MAX_PACKAGE_ALLOWLIST_JSON_BYTES: int = 16 * 1024


def _hub_asset_id(encoded: str) -> str:
    """Decode a base64-wrapped Hub asset id (catalog key, not a secret)."""
    return base64.b64decode(encoded.encode("ascii")).hex()


#: 企业版目录不展示的 Hub asset id（公共层只保留这一份，不判断产品形态）。
HIDDEN_HUB_ASSET_IDS = frozenset(
    (
        _hub_asset_id("eU2xv+iITWiX1i01hIlKWQ=="),  # 鸿蒙 App 开发专家
        _hub_asset_id("9jOxRcOrTkK4njb6NpssAA=="),  # 健康生活顾问
        _hub_asset_id("MsIj1puDTDW0C8OTXG+fZw=="),  # 旅游规划专家团
        _hub_asset_id("NZ846g53QWuNltI64sEr5Q=="),  # 身材管理专家团
    )
)

#: 包种类别名 → 规范名。
PACKAGE_KIND_ALIASES: dict[str, str] = {
    "agent_template": "agent_template",
    "agent_templates": "agent_template",
    "agent_group": "agent_group",
    "agent_groups": "agent_group",
}
#: 规范包种类 → 本地目录名。
PACKAGE_KIND_DIRS: dict[str, str] = {
    "agent_template": "agent_templates",
    "agent_group": "agent_groups",
}


def canonical_kind(kind: str) -> str | None:
    return PACKAGE_KIND_ALIASES.get(str(kind or "").strip())


def normalize_packages(raw: Any) -> list[dict[str, Any]]:
    """收成 ``[{kind, id, match_expr?}]``。重复的 kind+id 去掉，非法项拒绝。"""
    if raw is None:
        raw = []
    if not isinstance(raw, list):
        raise ValueError("packages must be a list")
    seen: set[tuple[str, str]] = set()
    packages: list[dict[str, Any]] = []
    for item in raw:
        if not isinstance(item, dict):
            raise ValueError("package allowlist entry must be an object")
        kind = canonical_kind(str(item.get("kind") or ""))
        if kind is None:
            raise ValueError("kind must be agent_template or agent_group")
        package_id = str(item.get("id") or "").strip()
        has_separator = "/" in package_id or "\\" in package_id
        id_ok = (
            bool(package_id)
            and package_id not in {".", ".."}
            and not has_separator
            and len(package_id) <= 128
        )
        if not id_ok:
            raise ValueError("package id is invalid")
        key = (kind, package_id)
        if key in seen:
            continue
        seen.add(key)
        record: dict[str, Any] = {"kind": kind, "id": package_id}
        match_expr = _structural_match_expr(item.get("match_expr"))
        if match_expr is not None:
            record["match_expr"] = match_expr
        packages.append(record)
    return packages


def _structural_match_expr(raw: Any) -> Any:
    """空范围保持为空。非空范围只保留字符串或字符串列表，不求值。"""
    if raw is None or raw == "" or raw == []:
        return None
    if isinstance(raw, str):
        text = raw.strip()
        return text or None
    if isinstance(raw, list):
        items = []
        for item in raw:
            text = str(item or "").strip()
            if text:
                items.append(text)
        return items or None
    raise ValueError("match_expr is invalid")


def _packages_json(packages: list[dict[str, Any]]) -> bytes:
    return json.dumps(
        packages,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def ensure_packages_fit(packages: list[dict[str, Any]]) -> None:
    """名单 JSON 超过 16KiB 时拒绝写入。"""
    if len(_packages_json(packages)) > MAX_PACKAGE_ALLOWLIST_JSON_BYTES:
        raise ValueError("package allowlist exceeds 16384 bytes")


def encode_package_allowlist_header(packages: list[dict[str, Any]]) -> str:
    """把名单编成内部 HTTP 头。过大或非法时改发空名单。

    发空名单等价于「受控包全部拒绝」（fail-closed），不是「不限制」；这样网关侧
    配置异常时不会静默放开访问，代价是受控包不可用，需靠 warning 日志定位。
    """
    try:
        payload = normalize_packages(packages)
        raw = _packages_json(payload)
        if len(raw) > MAX_PACKAGE_ALLOWLIST_JSON_BYTES:
            raise ValueError("package allowlist exceeds 16384 bytes")
    except ValueError:
        logger.warning(
            "[package_allowlist] allowlist header encode failed, sending empty "
            "(controlled packages will be denied)"
        )
        raw = b"[]"
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def decode_package_allowlist_header(value: str | None) -> list[dict[str, Any]]:
    """还原内部 HTTP 头。缺失或损坏时为空名单。

    与 encode 对称：空名单即拒绝受控包，不是放行。缺失/空白是正常路径（静默），
    解码失败或超长才打 warning，据此可区分「未配置」与「配置了但损坏」。
    """
    if value is None or not str(value).strip():
        return []
    encoded = str(value).strip()
    try:
        encoded_bytes = encoded.encode("ascii")
        padding = b"=" * (-len(encoded_bytes) % 4)
        raw = base64.b64decode(encoded_bytes + padding, altchars=b"-_", validate=True)
        if len(raw) > MAX_PACKAGE_ALLOWLIST_JSON_BYTES:
            raise ValueError("package allowlist exceeds 16384 bytes")
        decoded = json.loads(raw.decode("utf-8"))
        return normalize_packages(decoded)
    except ValueError:
        # json.JSONDecodeError / UnicodeError 都是 ValueError 子类，勿并列捕获 (G.ERR.09)。
        logger.warning(
            "[package_allowlist] allowlist header rejected, treating as empty "
            "(controlled packages will be denied)"
        )
        return []


def apply_allowlist_to_envelope(envelope: Any, packages: list[dict[str, Any]]) -> None:
    """覆盖 channel_context 里的名单，不沿用客户端字段。"""
    ctx = getattr(envelope, "channel_context", None)
    copied = dict(ctx) if isinstance(ctx, dict) else {}
    copied[PACKAGE_ALLOWLIST_METADATA_KEY] = list(packages)
    envelope.channel_context = copied


__all__ = [
    "HIDDEN_HUB_ASSET_IDS",
    "MAX_PACKAGE_ALLOWLIST_JSON_BYTES",
    "PACKAGE_ALLOWLIST_HEADER_NAME",
    "PACKAGE_ALLOWLIST_METADATA_KEY",
    "PACKAGE_KIND_ALIASES",
    "PACKAGE_KIND_DIRS",
    "apply_allowlist_to_envelope",
    "canonical_kind",
    "decode_package_allowlist_header",
    "encode_package_allowlist_header",
    "ensure_packages_fit",
    "normalize_packages",
]
