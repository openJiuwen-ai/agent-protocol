"""common/ids.py 契约测试：now_iso / image_sid / instance_sid。

确定性派生规则：
- image_sid(framework, framework_version) = "image_" + sha256(fw|fw_ver)[:16]
- instance_sid(user, framework)           = "generic_" + sha256(user|fw)[:8]
"""

from __future__ import annotations

import hashlib
import re

import pytest

from a2x_registry.common.ids import (
    image_sid,
    instance_sid,
    now_iso,
)


# ── now_iso ───────────────────────────────────────────────────

def test_now_iso_returns_iso8601_string():
    """now_iso() 返回 ISO8601 字符串（含时区标记）。"""
    s = now_iso()
    assert isinstance(s, str)
    # 容忍 'Z' 或 '+00:00' 两种 UTC 后缀写法
    assert re.match(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}", s), s


def test_now_iso_monotonic_non_decreasing():
    """连续两次调用，后者不早于前者（秒级粒度，允许相等）。"""
    a = now_iso()
    b = now_iso()
    assert b >= a


# ── image_sid ─────────────────────────────────────────────────

def test_image_sid_format_and_length():
    sid = image_sid("langchain", "0.2.0")
    assert sid.startswith("image_")
    # sha256 取前 16 个 hex 字符
    assert len(sid) == len("image_") + 16


def test_image_sid_deterministic():
    """同 (framework, version) 永远产出同一 service_id。"""
    assert image_sid("langchain", "0.2.0") == image_sid("langchain", "0.2.0")


def test_image_sid_differs_by_framework():
    assert image_sid("langchain", "0.2.0") != image_sid("llama_index", "0.2.0")


def test_image_sid_differs_by_version():
    assert image_sid("langchain", "0.1.0") != image_sid("langchain", "0.2.0")


def test_image_sid_matches_reference_formula():
    """与文档给定的公式严格一致：sha256(fw + '|' + fw_ver)[:16]。"""
    fw, ver = "opencode", "v0.2.0"
    expected = "image_" + hashlib.sha256(f"{fw}|{ver}".encode()).hexdigest()[:16]
    assert image_sid(fw, ver) == expected


# ── instance_sid ──────────────────────────────────────────────

def test_instance_sid_format_and_length():
    sid = instance_sid("alice", "langchain")
    assert sid.startswith("generic_")
    assert len(sid) == len("generic_") + 8


def test_instance_sid_deterministic():
    """同 (user, framework) 永远产出同一 service_id → 单例保证。"""
    assert instance_sid("alice", "langchain") == instance_sid("alice", "langchain")


def test_instance_sid_differs_by_user():
    assert instance_sid("alice", "langchain") != instance_sid("bob", "langchain")


def test_instance_sid_differs_by_framework():
    assert instance_sid("alice", "langchain") != instance_sid("alice", "llama_index")


def test_instance_sid_matches_reference_formula():
    user, fw = "alice", "langchain"
    expected = "generic_" + hashlib.sha256(f"{user}|{fw}".encode()).hexdigest()[:8]
    assert instance_sid(user, fw) == expected
