# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
"""Crypto 扩展入口契约与 CryptoProvider 协议.

来源：``jiuwenswarm/extensions/sdk/crypto_utility.py`` 与
``jiuwenswarm/common/security/base_crypto.py``（CryptoProvider Protocol，
纯协议定义随本包内联，避免依赖 common 实现）。
"""

from __future__ import annotations

from abc import abstractmethod
from typing import Protocol, runtime_checkable

from gateway_protocol.sdk.base import BaseExtension


@runtime_checkable
class CryptoProvider(Protocol):
    def encrypt(self, plaintext: str, **kwargs) -> str:
        ...

    def decrypt(self, ciphertext: str, **kwargs) -> str:
        ...


class CryptoUtility(BaseExtension):
    """扩展入口：持有真正的加解密实现，通过 `get_crypto()` 暴露。"""

    @abstractmethod
    def get_crypto(self) -> CryptoProvider:
        """返回实际执行 encrypt/decrypt 的实例。"""
        ...

    async def shutdown(self) -> None:
        """扩展关闭"""
        pass


__all__ = ["CryptoProvider", "CryptoUtility"]
