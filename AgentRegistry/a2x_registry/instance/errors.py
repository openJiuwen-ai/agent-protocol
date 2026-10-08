"""Instance management domain exceptions.

Business-layer exceptions that carry no HTTP semantics. The router is
responsible for translating them into HTTP status codes:
- ``InstanceNotFoundError`` → 404
- ``InstanceValidationError`` → 400
- ``InstanceInProgressError`` → 409（同 service_id 在途创建 / 删除）
- ``RuntimeNotConfiguredError`` → 501（模式 B 但未配置元戎连接）
- ``YuanrongFailedError`` → 502（元戎创建 / 删除失败，含同名冲突透传）
- ``YuanrongTimeoutError`` → 504（元戎请求 / 等待 running 超时）
- ``InstanceStoreError`` → 500（元戎成功后条目写入 / 删除重试耗尽，
  孤儿实例 / 僵尸条目留日志待对账）
"""

from __future__ import annotations

from a2x_registry.register.errors import NotFoundError, ValidationError


class InstanceNotFoundError(NotFoundError):
    """Instance (service_id) not found. Router maps to 404."""


class InstanceValidationError(ValidationError):
    """Instance input validation failed (missing field / invalid kind).
    Router maps to 400."""


class InstanceInProgressError(Exception):
    """同 service_id 已有在途创建 / 删除，拒绝并发。Router maps to 409."""


class RuntimeNotConfiguredError(Exception):
    """模式 B 请求但未配置元戎连接。Router maps to 501."""


class YuanrongFailedError(Exception):
    """元戎创建 / 删除失败（外部依赖，含同名冲突透传）。Router maps to 502."""


class YuanrongTimeoutError(Exception):
    """元戎请求或等待 running 超时。Router maps to 504."""


class InstanceStoreError(Exception):
    """元戎成功后条目写入 / 删除重试耗尽（数据层故障）。Router maps to 500."""


__all__ = [
    "InstanceNotFoundError",
    "InstanceValidationError",
    "InstanceInProgressError",
    "RuntimeNotConfiguredError",
    "YuanrongFailedError",
    "YuanrongTimeoutError",
    "InstanceStoreError",
]
