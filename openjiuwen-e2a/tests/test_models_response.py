# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""E2AResponse：流式多帧、终态与投影字段。"""

from e2a import (
    E2A_RESPONSE_KIND_E2A_CHUNK,
    E2A_RESPONSE_KIND_E2A_COMPLETE,
    E2A_RESPONSE_STATUS_IN_PROGRESS,
    E2A_RESPONSE_STATUS_SUCCEEDED,
    E2AResponse,
)


def test_streaming_frames_roundtrip_keep_sequence_and_final_flag():
    chunk = E2AResponse(
        response_id="r0",
        request_id="req-1",
        sequence=0,
        response_kind=E2A_RESPONSE_KIND_E2A_CHUNK,
        body={"delta_kind": "text", "delta": "你"},
    )
    final = E2AResponse(
        response_id="r1",
        request_id="req-1",
        sequence=1,
        is_final=True,
        status=E2A_RESPONSE_STATUS_SUCCEEDED,
        response_kind=E2A_RESPONSE_KIND_E2A_COMPLETE,
        body={"result": {"content": "你好"}},
    )

    parsed_chunk = E2AResponse.from_dict(chunk.to_dict())
    parsed_final = E2AResponse.from_dict(final.to_dict())

    assert (parsed_chunk.sequence, parsed_chunk.is_final) == (0, False)
    assert parsed_chunk.status == E2A_RESPONSE_STATUS_IN_PROGRESS
    assert parsed_chunk.body == {"delta_kind": "text", "delta": "你"}
    assert (parsed_final.sequence, parsed_final.is_final) == (1, True)
    assert parsed_final.body == {"result": {"content": "你好"}}


def test_response_fills_defaults_and_timestamp():
    response = E2AResponse(request_id="req-1")
    response.ensure_timestamp()

    assert response.protocol_version == "1.0"
    assert response.status == E2A_RESPONSE_STATUS_IN_PROGRESS
    assert response.identity_origin.value == "agent"
    assert response.is_final is False
    assert response.timestamp is not None


def test_response_roundtrip_keeps_projection_and_metadata():
    response = E2AResponse(
        response_id="r9",
        request_id="req-9",
        sequence=2,
        is_final=True,
        status=E2A_RESPONSE_STATUS_SUCCEEDED,
        response_kind=E2A_RESPONSE_KIND_E2A_COMPLETE,
        timestamp="2026-03-29T12:00:02+00:00",
        body={"result": {"ok": True}},
        metadata={"channel": "web"},
        projections={"acp": {"jsonrpc": "2.0", "id": 1, "result": {}}},
        channel="web",
    )

    parsed = E2AResponse.from_dict(response.to_dict())

    assert parsed.metadata == {"channel": "web"}
    assert parsed.projections == {"acp": {"jsonrpc": "2.0", "id": 1, "result": {}}}
    assert parsed.timestamp == "2026-03-29T12:00:02+00:00"
    assert parsed.channel == "web"
