# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""扩展通用数据类型（契约）。

来源：``jiuwenswarm/extensions/types.py``。纯 dataclass，零依赖；
实现侧（两仓各自扩展框架）与本包共用同一数据结构。
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass
class ExtensionMetadata:
    """扩展元数据"""

    id: str  # 扩展唯一标识
    name: str  # 扩展名称
    version: str  # 扩展版本
    description: str  # 扩展描述
    author: str  # 扩展作者
    min_jiuwenswarm_version: str  # 最小兼容版本
    dependencies: dict[str, str]  # 扩展依赖 {"extension_id": ">=1.0.0"}
    config_schema: dict | None  # 配置模式 (JSON Schema)
    package_type: str = "extension"
    permissions: tuple[str, ...] = ()
    frontend: tuple[dict[str, Any], ...] = ()


@dataclass
class ExtensionConfig:
    config: dict[str, Any]
    logger: Any


__all__ = ["ExtensionConfig", "ExtensionMetadata"]
