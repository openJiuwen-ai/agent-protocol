# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""AgentServer 与 Gateway 之间的线上编解码：E2A 响应帧与旧结构互转。"""

from e2a import (
    AgentResponse,
    AgentResponseChunk,
    encode_agent_chunk_for_wire,
    encode_agent_response_for_wire,
    is_e2a_response_wire_dict,
    parse_agent_server_wire_chunk,
    parse_agent_server_wire_unary,
)


def test_unary_success_roundtrips_through_e2a_wire():
    response = AgentResponse(
        request_id="req-1", channel_id="web", ok=True, payload={"content": "hi"}
    )

    wire = encode_agent_response_for_wire(response, response_id="r1")

    assert is_e2a_response_wire_dict(wire)
    assert wire["response_kind"] == "e2a.complete"
    assert wire["is_final"] is True

    parsed = parse_agent_server_wire_unary(wire)

    assert (parsed.request_id, parsed.channel_id, parsed.ok) == ("req-1", "web", True)
    assert parsed.payload == {"content": "hi"}


def test_unary_error_roundtrips_through_e2a_wire():
    response = AgentResponse(
        request_id="req-2", channel_id="web", ok=False, payload={"error": "boom"}
    )

    wire = encode_agent_response_for_wire(response, response_id="r2")

    assert wire["response_kind"] == "e2a.error"

    parsed = parse_agent_server_wire_unary(wire)

    assert parsed.ok is False
    assert parsed.payload == {"error": "boom"}


def test_stream_complete_chunk_roundtrips_through_e2a_wire():
    chunk = AgentResponseChunk(
        request_id="req-3",
        channel_id="web",
        payload={"is_complete": True},
        is_complete=True,
    )

    wire = encode_agent_chunk_for_wire(chunk, response_id="r3", sequence=1)

    assert wire["response_kind"] == "e2a.complete"
    assert (wire["sequence"], wire["is_final"]) == (1, True)

    parsed = parse_agent_server_wire_chunk(wire)

    assert parsed.request_id == "req-3"
    assert parsed.is_complete is True


def test_is_e2a_response_wire_dict_rejects_missing_response_kind():
    frame = {"protocol_version": "1.0", "request_id": "req-1", "sequence": 0}

    assert is_e2a_response_wire_dict(frame) is False


def test_is_e2a_response_wire_dict_rejects_none_or_blank_response_kind():
    base = {"protocol_version": "1.0", "request_id": "req-1"}

    assert is_e2a_response_wire_dict({**base, "response_kind": None}) is False
    assert is_e2a_response_wire_dict({**base, "response_kind": "  "}) is False
