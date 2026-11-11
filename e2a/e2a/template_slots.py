# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
"""企业级配置 ``template_ref`` 槽位契约。

来源：``jiuwenswarm/enterprise/config/effective/schemas.py``。槽位名是
``agent_template.template_ref`` 的 JSON 键，gateway（配置写入/生效解析）与
AgentServer（按槽位加载）必须逐字一致，否则会出现「配置写进去了但读侧取不到」。

边界：本模块只保留槽位枚举、槽位 → 实体表映射与 ``normalize_template_ref``
这一零依赖纯函数；``RoutingContext`` / ``EffectiveEnterpriseConfig`` 等加载
快照依赖各仓的数据库与运行时，留在实现侧。
"""

from __future__ import annotations

from enum import StrEnum
from typing import Any


class TemplateRefSlot(StrEnum):
    """``template_ref`` JSON 键名（与 agent_template.template_ref 槽位一致）。"""

    DEFAULT_MODEL = "default_model"
    VIDEO_MODEL = "video_model"
    AUDIO_MODEL = "audio_model"
    VISION_MODEL = "vision_model"
    IMAGE_GEN_MODEL = "image_gen_model"
    EMBEDDING_MODEL = "embedding_model"
    SKILL_PREBUILT = "skill_prebuilt"
    EXTENSION_CONFIG = "extension_config"
    MCP = "mcp"
    PERMISSIONS = "permissions"
    A2A_ACCESS_POLICY = "a2a_access_policy"


#: 槽位 → 企业配置实体表名。
SLOT_ENTITY_TABLE: dict[TemplateRefSlot, str] = {
    TemplateRefSlot.DEFAULT_MODEL: "model_template",
    TemplateRefSlot.VIDEO_MODEL: "model_template",
    TemplateRefSlot.AUDIO_MODEL: "model_template",
    TemplateRefSlot.VISION_MODEL: "model_template",
    TemplateRefSlot.IMAGE_GEN_MODEL: "model_template",
    TemplateRefSlot.EMBEDDING_MODEL: "embedding_template",
    TemplateRefSlot.SKILL_PREBUILT: "skill_prebuilt_template",
    TemplateRefSlot.EXTENSION_CONFIG: "extension_config_template",
    TemplateRefSlot.MCP: "mcp_template",
    TemplateRefSlot.PERMISSIONS: "permissions_template",
    TemplateRefSlot.A2A_ACCESS_POLICY: "a2a_access_policy_template",
}

#: 模型类槽位（共用 ``model_template`` 表）。
MODEL_SLOT_KEYS = frozenset({
    TemplateRefSlot.DEFAULT_MODEL,
    TemplateRefSlot.VIDEO_MODEL,
    TemplateRefSlot.AUDIO_MODEL,
    TemplateRefSlot.VISION_MODEL,
    TemplateRefSlot.IMAGE_GEN_MODEL,
})

#: 默认加载的槽位集合。
DEFAULT_AGENT_LOAD_SLOTS = frozenset({
    *MODEL_SLOT_KEYS,
    TemplateRefSlot.EMBEDDING_MODEL,
    TemplateRefSlot.SKILL_PREBUILT,
    TemplateRefSlot.EXTENSION_CONFIG,
    TemplateRefSlot.MCP,
    TemplateRefSlot.PERMISSIONS,
    TemplateRefSlot.A2A_ACCESS_POLICY,
})


def normalize_template_ref(value: Any) -> dict[str, list[str]]:
    """将 ``template_ref`` 规范为 ``{slot: [ref_string, ...]}``；空值键省略，同槽位去重保序。"""
    if value is None:
        return {}
    if not isinstance(value, dict):
        raise ValueError("template_ref must be a JSON object")
    out: dict[str, list[str]] = {}
    for key, raw in value.items():
        slot = str(key).strip()
        if not slot or raw is None:
            continue
        if not isinstance(raw, list):
            raise ValueError(f"template_ref[{slot!r}] must be a list")
        refs: list[str] = []
        seen: set[str] = set()
        for item in raw:
            if item is None:
                continue
            text = str(item).strip()
            if not text or text in seen:
                continue
            seen.add(text)
            refs.append(text)
        if refs:
            out[slot] = refs
    return out


__all__ = [
    "DEFAULT_AGENT_LOAD_SLOTS",
    "MODEL_SLOT_KEYS",
    "SLOT_ENTITY_TABLE",
    "TemplateRefSlot",
    "normalize_template_ref",
]
