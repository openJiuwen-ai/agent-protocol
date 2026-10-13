# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""SSH 认证结果/条目数据类。

背景：gateway 侧 SSH 通道（``channel_manager/protocol/ssh``）校验 SSH 公钥指纹，
KeyRegistry/认证实现属 jiuwenswarm 仓的 agentos 扩展。拆分后 gateway 不再 import
``extensions.agentos.auth.*``，改为依赖跨仓共享的 ``SshAuthPort`` 接口
（接口定义由各仓实现侧持有，本包只保留跨仓消费的数据形状）。

来源参考：``jiuwenswarm/extensions/agentos/auth/ssh_authenticator.py``
（SshPublicKeyAuthenticator.verify / lookup_entry）与 ``ssh_key_registry.py``
（KeyRegistryEntry）。结果形状与 AuthResult 对齐，仅保留跨仓消费字段。
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass(frozen=True)
class SshKeyEntry:
    """KeyRegistry 中登记的一条 SSH 公钥身份（跨仓只读视图）."""

    fingerprint: str
    username: str
    user_id: str
    source: str = ""
    session_id: str = ""


@dataclass(frozen=True)
class SshAuthResult:
    """SSH 认证结果（与 agentos AuthResult 的消费字段子集对齐）."""

    success: bool
    user_id: str = ""
    error: str = ""
    extensions: dict[str, Any] = field(default_factory=dict)


__all__ = ["SshAuthResult", "SshKeyEntry"]
