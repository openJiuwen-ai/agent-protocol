# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

"""ACP / A2A 与 E2A 的互转：请求转入、prompt 补全与响应投影。"""

import pytest

from e2a import (
    E2A_RESPONSE_KIND_A2A_STREAM_EVENT,
    E2A_RESPONSE_KIND_ACP_PROMPT_RESULT,
    E2A_RESPONSE_KIND_E2A_CHUNK,
    E2A_RESPONSE_KIND_E2A_ERROR,
    E2A_RESPONSE_STATUS_FAILED,
    E2AEnvelope,
    E2AResponse,
    e2a_response_to_a2a_stream_payload,
    e2a_response_to_acp_jsonrpc_response,
    envelope_from_a2a_send_message,
    envelope_from_acp_jsonrpc,
    envelope_to_acp_jsonrpc_call,
    merge_params_to_acp_prompt,
)


def test_acp_jsonrpc_call_converts_to_e2a_envelope():
    envelope = envelope_from_acp_jsonrpc(
        "session/prompt",
        {"session_id": "s1", "prompt": [{"type": "text", "text": "hi"}]},
        jsonrpc_id=42,
        channel="acp",
    )

    assert envelope.provenance.source_protocol == "acp"
    assert envelope.provenance.details["jsonrpc_method"] == "session/prompt"
    assert envelope.method == "session/prompt"
    assert envelope.jsonrpc_id == 42
    assert envelope.session_id == "s1"
    assert envelope.channel == "acp"


def test_a2a_send_message_converts_to_e2a_envelope():
    envelope = envelope_from_a2a_send_message(
        task_id="task-1",
        context_id="ctx-1",
        message_body={"role": "user", "parts": [{"text": "hi"}]},
        metadata={"trace": "t-1"},
        configuration={"acceptedOutputModes": ["text/plain"]},
    )

    assert envelope.provenance.source_protocol == "a2a"
    assert envelope.provenance.details["abstract_operation"] == "SendMessage"
    assert envelope.method == "session/prompt"
    assert envelope.task_id == "task-1"
    assert envelope.context_id == "ctx-1"
    assert envelope.expected_output_modes == ["text/plain"]
    assert envelope.a2a_metadata == {"trace": "t-1"}
    assert envelope.params["message"] == {"role": "user", "parts": [{"text": "hi"}]}


@pytest.mark.parametrize(
    "params,expected_prompt",
    [
        ({"text": "hi"}, [{"type": "text", "text": "hi"}]),
        ({"content_blocks": [{"type": "text", "text": "hi"}]}, [{"type": "text", "text": "hi"}]),
        ({"prompt": [{"type": "text", "text": "keep"}]}, [{"type": "text", "text": "keep"}]),
    ],
)
def test_merge_params_to_acp_prompt_fills_prompt(params, expected_prompt):
    envelope = E2AEnvelope(method="session/prompt", params=params)

    assert merge_params_to_acp_prompt(envelope)["prompt"] == expected_prompt


def test_merge_params_to_acp_prompt_prefers_prompt_over_blocks_and_text():
    params = {
        "text": "from-text",
        "content_blocks": [{"type": "text", "text": "from-blocks"}],
        "prompt": [{"type": "text", "text": "from-prompt"}],
    }

    merged = merge_params_to_acp_prompt(E2AEnvelope(method="session/prompt", params=params))

    # 优先级：已有 prompt > content_blocks > text
    assert merged["prompt"] == [{"type": "text", "text": "from-prompt"}]


def test_merge_params_to_acp_prompt_prefers_content_blocks_over_text():
    params = {"text": "from-text", "content_blocks": [{"type": "text", "text": "from-blocks"}]}

    merged = merge_params_to_acp_prompt(E2AEnvelope(method="session/prompt", params=params))

    assert merged["prompt"] == [{"type": "text", "text": "from-blocks"}]


def test_merge_params_to_acp_prompt_keeps_empty_prompt_without_text_fallback():
    params = {"text": "from-text", "prompt": []}

    merged = merge_params_to_acp_prompt(E2AEnvelope(method="session/prompt", params=params))

    # prompt 键存在即视为调用方已给定：空列表不回退到 text
    assert merged["prompt"] == []


def test_merge_params_to_acp_prompt_carries_session_and_meta():
    envelope = E2AEnvelope(
        method="session/prompt",
        session_id="s1",
        params={"text": "hi"},
        acp_meta={"trace": "t-1"},
    )

    merged = merge_params_to_acp_prompt(envelope)

    assert merged["session_id"] == "s1"
    assert merged["_meta"] == {"trace": "t-1"}


def test_merge_params_to_acp_prompt_is_noop_for_gateway_rpc():
    envelope = E2AEnvelope(method="chat.send", params={"text": "hi"}, acp_meta={"x": 1})

    assert merge_params_to_acp_prompt(envelope) == {"text": "hi"}


def test_envelope_to_acp_jsonrpc_call_exports_prompt():
    envelope = E2AEnvelope(method="session/prompt", jsonrpc_id=7, params={"query": "hi"})

    call = envelope_to_acp_jsonrpc_call(envelope)

    assert call["jsonrpc"] == "2.0"
    assert call["id"] == 7
    assert call["params"]["prompt"] == [{"type": "text", "text": "hi"}]


def test_acp_prompt_result_projects_to_jsonrpc_result():
    response = E2AResponse(
        jsonrpc_id=7,
        response_kind=E2A_RESPONSE_KIND_ACP_PROMPT_RESULT,
        body={"stop_reason": "end_turn"},
    )

    assert e2a_response_to_acp_jsonrpc_response(response) == {
        "jsonrpc": "2.0",
        "id": 7,
        "result": {"stop_reason": "end_turn"},
    }


def test_e2a_error_projects_to_jsonrpc_error_with_string_code():
    response = E2AResponse(
        jsonrpc_id=7,
        response_kind=E2A_RESPONSE_KIND_E2A_ERROR,
        status=E2A_RESPONSE_STATUS_FAILED,
        body={"code": "E2A.AGENT_ERROR", "message": "boom", "details": {"k": 1}},
    )

    payload = e2a_response_to_acp_jsonrpc_response(response)

    # 非整数错误码降级为 -32603，原值放进 data.e2a_code
    assert payload["error"]["code"] == -32603
    assert payload["error"]["message"] == "boom"
    assert payload["error"]["data"] == {"details": {"k": 1}, "e2a_code": "E2A.AGENT_ERROR"}


def test_a2a_stream_event_projects_to_stream_response_branch():
    response = E2AResponse(
        response_kind=E2A_RESPONSE_KIND_A2A_STREAM_EVENT,
        body={"branch": "status_update", "payload": {"status": {"state": "working"}}},
    )

    assert e2a_response_to_a2a_stream_payload(response) == {
        "statusUpdate": {"status": {"state": "working"}}
    }
    # 非 A2A 流事件不产出 StreamResponse
    assert e2a_response_to_a2a_stream_payload(
        E2AResponse(response_kind=E2A_RESPONSE_KIND_E2A_CHUNK)
    ) is None
