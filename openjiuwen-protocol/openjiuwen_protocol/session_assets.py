# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""会话素材（session assets）共享数据结构。

来源：``jiuwenswarm/server/runtime/session_assets.py``。``session.assets.*``
是企业版新增的 E2A 方法族，请求/响应经 gateway 与 AgentServer 两侧解释，
记录形状必须一致；本包只固化数据结构与常量，落盘/校验逻辑留在实现侧。
"""

from __future__ import annotations

from dataclasses import dataclass

#: 素材来源：用户上传。
ASSET_SOURCE_UPLOAD: str = "upload"
#: 素材来源：Agent 生成。
ASSET_SOURCE_GENERATED: str = "generated"
#: 合法来源集合。
ASSET_SOURCES: tuple[str, ...] = (ASSET_SOURCE_UPLOAD, ASSET_SOURCE_GENERATED)

#: 素材类别（按扩展名归类，其余归 document）。
ASSET_KIND_IMAGE: str = "image"
ASSET_KIND_VIDEO: str = "video"
ASSET_KIND_AUDIO: str = "audio"
ASSET_KIND_DOCUMENT: str = "document"
ASSET_KINDS: tuple[str, ...] = (
    ASSET_KIND_IMAGE,
    ASSET_KIND_VIDEO,
    ASSET_KIND_AUDIO,
    ASSET_KIND_DOCUMENT,
)

#: 素材 ID 前缀（``asset_<8 hex>``）。
ASSET_ID_PREFIX: str = "asset_"
#: 素材名称最大长度。
ASSET_NAME_MAX_LENGTH: int = 60


@dataclass
class SessionAsset:
    """会话素材记录（``<sessions>/<session_id>/session_assets.json`` 的一条）。

    ``path`` 形状约定：AgentServer 侧注册时提交、写入记录的**绝对文件路径**
    （实现侧写入前校验文件存在），由写入进程解释。gateway 只透传与展示，不得
    按自身工作目录拼接、重写或解析该路径；实现侧不得接受相对路径、含 ``..``
    的路径段或指向会话目录之外的引用。协议层不做路径计算，两侧实现各自校验。
    """

    asset_id: str
    name: str
    kind: str
    path: str
    source: str = ASSET_SOURCE_UPLOAD
    created_at: float | None = None


__all__ = [
    "ASSET_ID_PREFIX",
    "ASSET_KINDS",
    "ASSET_KIND_AUDIO",
    "ASSET_KIND_DOCUMENT",
    "ASSET_KIND_IMAGE",
    "ASSET_KIND_VIDEO",
    "ASSET_NAME_MAX_LENGTH",
    "ASSET_SOURCES",
    "ASSET_SOURCE_GENERATED",
    "ASSET_SOURCE_UPLOAD",
    "SessionAsset",
]
