"""register/errors.py 错误类型契约测试。

错误类型映射 HTTP 404/400/403/409/502。
- NotFoundError            → 404（资源不存在）
- ValidationError          → 400（校验失败）
- NotOwnedError            → 403（越权）
- ImageInUseError          → 409（镜像在用，冲突）
- ExternalDependencyError  → 502（外部依赖失败）
"""

from __future__ import annotations

import pytest

from a2x_registry.register.errors import (
    ExternalDependencyError,
    ImageInUseError,
    NotFoundError,
    NotOwnedError,
    ValidationError,
)


def test_not_found_error_is_exception():
    assert issubclass(NotFoundError, Exception)


def test_validation_error_is_exception():
    assert issubclass(ValidationError, Exception)


def test_not_owned_error_is_exception():
    assert issubclass(NotOwnedError, Exception)


def test_image_in_use_error_is_exception():
    assert issubclass(ImageInUseError, Exception)


def test_external_dependency_error_is_exception():
    assert issubclass(ExternalDependencyError, Exception)


def test_errors_carry_message():
    for cls in (NotFoundError, ValidationError, NotOwnedError,
                ImageInUseError, ExternalDependencyError):
        exc = cls("boom")
        assert str(exc) == "boom"


def test_errors_distinct_classes():
    """各类互不相同，便于 router 按异常类型映射 HTTP 状态。"""
    classes = {
        NotFoundError, ValidationError, NotOwnedError,
        ImageInUseError, ExternalDependencyError,
    }
    assert len(classes) == 5
