# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""E2AEnvelope：常用请求字段、序列化与旧载荷兼容。"""

from datetime import datetime, timezone

import pytest

from e2a import E2AAuth, E2AEnvelope


def test_envelope_roundtrip_keeps_common_request_fields():
    envelope = E2AEnvelope(
        request_id="req-1",
        session_id="sess-1",
        message_id="msg-1",
        channel="web",
        user_id="u-1",
        method="chat.send",
        is_stream=True,
        params={"content": "你好", "mode": "agent", "work_mode": "work"},
        agent_ref={"mode": "local", "id": "agent-1"},
        traceparent="00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
    )

    parsed = E2AEnvelope.from_dict(envelope.to_dict())

    assert parsed.protocol_version == "1.0"
    assert parsed.request_id == "req-1"
    assert parsed.session_id == "sess-1"
    assert parsed.message_id == "msg-1"
    assert parsed.channel == "web"
    assert parsed.user_id == "u-1"
    assert parsed.method == "chat.send"
    assert parsed.is_stream is True
    assert parsed.params == {"content": "你好", "mode": "agent", "work_mode": "work"}
    assert parsed.agent_ref == {"mode": "local", "id": "agent-1"}
    assert parsed.traceparent.startswith("00-")


def test_envelope_from_legacy_keys_migrates_fields():
    parsed = E2AEnvelope.from_dict(
        {
            "request_id": "req-legacy",
            "channel_id": "feishu",
            "req_method": "chat.send",
            "timestamp": 1774524781.15,
            "params": {"content": "hi"},
            "payload": {"content": "ignored", "query": "hi"},
            "metadata": {"feishu_open_id": "ou_x", "message_id": "om_x"},
            "binding": "acp",
        }
    )

    # 旧键迁移：channel_id / req_method / metadata / binding；payload 合并进 params 且不覆盖
    assert parsed.channel == "feishu"
    assert parsed.method == "chat.send"
    assert parsed.params == {"content": "hi", "query": "hi"}
    assert parsed.channel_context == {"feishu_open_id": "ou_x", "message_id": "om_x"}
    assert parsed.provenance.source_protocol == "acp"
    assert parsed.provenance.details["migrated_from_binding"] == "acp"
    # 纪元秒时间戳规范化为 RFC 3339 UTC
    assert parsed.timestamp == datetime.fromtimestamp(1774524781.15, tz=timezone.utc).isoformat()


def test_envelope_keeps_iso_timestamp_and_default_identity():
    timestamp = "2026-03-28T12:00:00+00:00"

    parsed = E2AEnvelope.from_dict({"request_id": "req-1", "timestamp": timestamp})

    assert parsed.timestamp == timestamp
    assert parsed.identity_origin.value == "user"
    assert parsed.provenance.source_protocol == "e2a"


def test_envelope_roundtrips_auth_and_file_refs():
    envelope = E2AEnvelope(
        request_id="req-2",
        params={
            "files": [
                {"uri": "s3://bucket/a.png", "name": "a.png", "mime_type": "image/png", "size": 3}
            ]
        },
        auth=E2AAuth(credential_ref="cred-1", extra_headers={"x-trace": "1"}),
    )

    wire = envelope.to_dict()

    assert wire["auth"]["credential_ref"] == "cred-1"
    assert wire["auth"]["extra_headers"] == {"x-trace": "1"}
    assert wire["params"]["files"][0]["uri"] == "s3://bucket/a.png"

    parsed = E2AEnvelope.from_dict(wire)

    assert parsed.auth == E2AAuth(credential_ref="cred-1", extra_headers={"x-trace": "1"})
    assert parsed.params["files"][0]["mime_type"] == "image/png"


def test_envelope_from_dict_keeps_deeply_nested_params():
    params: dict = {}
    cursor = params
    for _ in range(20):
        cursor["nested"] = {}
        cursor = cursor["nested"]
    cursor["leaf"] = "v"

    parsed = E2AEnvelope.from_dict({"request_id": "req-deep", "params": params})

    # 解析层不做深度限制：超过 16 层的 JSON 原样保留，不截断也不报错
    assert parsed.params == params


def test_envelope_from_dict_keeps_large_params_payload():
    text = "x" * (100 * 1024)

    parsed = E2AEnvelope.from_dict({"request_id": "req-big", "params": {"text": text}})

    # 解析层不做体积限制：超过 96 KiB 的 params 原样保留
    assert parsed.params["text"] == text


def test_envelope_from_dict_timestamp_none_is_kept_and_nan_is_rejected():
    assert E2AEnvelope.from_dict({"request_id": "req-1", "timestamp": None}).timestamp is None

    with pytest.raises(ValueError):
        E2AEnvelope.from_dict({"request_id": "req-1", "timestamp": float("nan")})


def test_envelope_from_dict_drops_unknown_top_level_keys():
    parsed = E2AEnvelope.from_dict({"request_id": "req-1", "unknown_field": "x"})

    # 未知顶层键静默丢弃，不进入信封也不回写到线格式
    assert not hasattr(parsed, "unknown_field")
    assert "unknown_field" not in parsed.to_dict()
