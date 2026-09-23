from __future__ import annotations

import json
from pathlib import Path

import pytest

from iiap import (
    AgentRouter,
    AssistanceService,
    DecisionService,
    contains_forbidden_key,
    validate_assistance_text,
    validate_decision,
    validate_intent_context_packet,
    validate_privacy,
)


FIXTURES = Path(__file__).parents[3] / "contracts" / "fixtures"


def packet() -> dict:
    return json.loads((FIXTURES / "packet.valid.json").read_text())


class FakeModel:
    async def generate_decision(self, request):
        assert request.operation == "decision"
        return {"decision": "offer_help", "reason": "comparison_need", "offerType": "text_assistance", "helpTopic": "compare_options", "uiStyle": "inline_card", "message": "help"}

    async def generate_assistance(self, request):
        return {"type": "iiap.assistance.response", "iiapVersion": "0.1", "requestId": request.payload["requestId"], "message": "details"}


@pytest.mark.asyncio
async def test_decision_and_assistance_services():
    model = FakeModel()
    decision = await DecisionService(model, decision_id_factory=lambda: "dec_test").decide(packet())
    assert decision["type"] == "iiap.decision"
    assert decision["decisionId"] == "dec_test"
    assert decision["packetId"] == packet()["packetId"]
    assert decision["surfaceInstanceId"] == packet()["surfaceInstanceId"]
    assert decision["payload"]["decision"] == "offer_help"
    request = json.loads((FIXTURES / "assistance-request.valid.json").read_text())
    assert (await AssistanceService(model).assist(request))["message"] == "details"
    with pytest.raises(ValueError):
        await AssistanceService(model).assist({key: value for key, value in request.items() if key != "decisionId"})


@pytest.mark.asyncio
async def test_decision_service_rejects_schema_invalid_packets_before_model_call():
    class RecordingModel(FakeModel):
        calls = 0

        async def generate_decision(self, request):
            self.calls += 1
            return await super().generate_decision(request)

    for invalid in (
        {**packet(), "unexpected": True},
        {key: value for key, value in packet().items() if key != "messageId"},
        {**packet(), "observations": {"events": ["wrong-shape"]}},
        {**packet(), "window": {**packet()["window"], "durationMs": float("nan")}},
    ):
        model = RecordingModel()
        with pytest.raises(ValueError, match="^INVALID_PACKET$"):
            await DecisionService(model).decide(invalid)
        assert model.calls == 0


@pytest.mark.asyncio
async def test_production_rejects_test_scenario_before_model_call():
    class RecordingModel(FakeModel):
        calls = 0

        async def generate_decision(self, request):
            self.calls += 1
            return await super().generate_decision(request)

    controlled = {**packet(), "testScenario": "update_suggestion"}
    model = RecordingModel()
    with pytest.raises(ValueError, match="^TEST_SCENARIO_NOT_ALLOWED$"):
        await DecisionService(model, profile="production").decide(controlled)
    assert model.calls == 0
    result = await DecisionService(model, profile="test").decide(controlled)
    assert result["type"] == "iiap.decision"
    assert model.calls == 1
    with pytest.raises(ValueError, match="^INVALID_PROFILE$"):
        DecisionService(model, profile="other")  # type: ignore[arg-type]


@pytest.mark.asyncio
async def test_decision_service_does_not_send_unsanitized_definition_context_to_the_model():
    class RecordingModel(FakeModel):
        calls = 0

        async def generate_decision(self, request):
            self.calls += 1
            return await super().generate_decision(request)

    unsanitized = packet()
    unsanitized["surfaceContext"]["redaction"]["unknownCustomPropertiesExcluded"] = False
    unsanitized["surfaceContext"]["definition"][0]["customMetadata"] = "not-reviewed"
    sensitive = packet()
    sensitive["surfaceContext"]["definition"][0]["AUTH_TOKEN"] = "do-not-send"
    for unsafe in (unsanitized, sensitive):
        model = RecordingModel()
        result = await DecisionService(model).decide(unsafe)
        assert result["payload"]["decision"] == "no_intervention"
        assert model.calls == 0


@pytest.mark.asyncio
async def test_assistance_service_validates_the_complete_request_schema_before_model_call():
    class RecordingModel(FakeModel):
        calls = 0

        async def generate_assistance(self, request):
            self.calls += 1
            return await super().generate_assistance(request)

    valid = json.loads((FIXTURES / "assistance-request.valid.json").read_text())
    for invalid in (
        {**valid, "unexpected": True},
        {**valid, "requestId": "x" * 129},
        {**valid, "language": ""},
        {**valid, "topic": 1},
    ):
        model = RecordingModel()
        with pytest.raises(ValueError, match="^INVALID_ASSISTANCE_REQUEST$"):
            await AssistanceService(model).assist(invalid)
        assert model.calls == 0


def test_public_packet_validator_uses_the_packaged_schema():
    validate_intent_context_packet(packet())
    with pytest.raises(ValueError, match="^INVALID_PACKET$"):
        validate_intent_context_packet({**packet(), "unexpected": True})


@pytest.mark.asyncio
async def test_agent_router_can_reuse_business_agent():
    calls = []

    async def business(request):
        calls.append(request.operation)
        return {"decision": "no_intervention"}

    router = AgentRouter(FakeModel(), business_decision=business, route="business_agent")
    await router.generate_decision(type("Request", (), {"operation": "decision"})())
    assert calls == ["decision"]


def test_privacy_and_update_validation():
    assert contains_forbidden_key({"nested": [{"rawText": "secret"}]})
    assert contains_forbidden_key({"nested": [{"AUTH_TOKEN": "secret"}]})
    assert contains_forbidden_key({"nested": [{"pass_word": "secret"}]})
    assert not validate_privacy({"value": float("nan")})
    assert not validate_privacy({"value": float("inf")})
    value = {"decision": "offer_help", "reason": "x", "offerType": "update_suggestion", "uiStyle": "inline_card", "message": "help", "updateSuggestion": {"kind": "data_model_update", "updates": [{"surfaceId": "booking", "path": "/date", "value": "evening"}, {"surfaceId": "other", "path": "/date", "value": "unsafe"}]}}
    result = validate_decision(value, packet=packet())
    assert result["offerType"] == "text_assistance"
    assert result["helpTopic"] == "explain_rules"
    assert "updateSuggestion" not in result
    wrong_value = {**value, "updateSuggestion": {"kind": "data_model_update", "updates": [{"surfaceId": "booking", "path": "/date", "value": "morning"}]}}
    assert validate_decision(wrong_value, packet=packet())["offerType"] == "text_assistance"

    numeric_packet = packet()
    numeric_packet["allowedOperations"]["updateTargets"][0]["allowedValues"] = [1]
    boolean_value = {**value, "updateSuggestion": {"kind": "data_model_update", "updates": [{"surfaceId": "booking", "path": "/date", "value": True}]}}
    assert validate_decision(boolean_value, packet=numeric_packet)["offerType"] == "text_assistance"

    boundary_reason = {
        "decision": "offer_help", "reason": "x" * 1024,
        "offerType": "text_assistance", "helpTopic": "explain_rules", "message": "help",
    }
    assert validate_decision(boundary_reason, packet=packet())["decision"] == "offer_help"
    for invalid_text in (
        {"decision": "offer_help", "reason": "x" * 1025, "offerType": "text_assistance", "message": "help"},
        {"decision": "offer_help", "reason": "x", "offerType": "text_assistance", "message": "x" * 2049},
        {"decision": "offer_help", "reason": 1, "offerType": "text_assistance", "message": "help"},
    ):
        assert validate_decision(invalid_text, packet=packet())["decision"] == "no_intervention"


def test_decision_state_combinations_are_normalized_to_the_wire_contract():
    source_packet = packet()
    source_packet["patterns"] = [{
        "patternId": "pattern.choice",
        "type": "selection_reversal",
        "componentId": "choice",
        "basedOnEvents": [],
        "metrics": {},
    }]
    inferred = validate_decision({
        "decision": "offer_help", "reason": "comparison_need",
        "offerType": "text_assistance", "uiStyle": "inline_card", "message": "help",
    }, packet=source_packet)
    assert inferred["helpTopic"] == "compare_options"

    contradictory = validate_decision({
        "decision": "defer", "reason": "wait",
        "offerType": "text_assistance", "helpTopic": "fix_block",
        "uiStyle": "inline_card", "message": "later",
    }, packet=source_packet)
    assert contradictory == {
        "decision": "defer", "reason": "wait",
        "offerType": "none", "uiStyle": "none", "message": "later",
    }

    invalid_offer = validate_decision({
        "decision": "offer_help", "reason": "invalid",
        "offerType": "none", "uiStyle": "none", "message": "",
    }, packet=source_packet)
    assert invalid_offer == {
        "decision": "no_intervention",
        "reason": "invalid_or_insufficient_iiap_decision",
        "offerType": "none", "uiStyle": "none", "message": "",
    }


def test_multi_field_and_multi_select_update_validation():
    source_packet = packet()
    source_packet["allowedOperations"]["updateTargets"] = [
        {
            "componentId": "risk", "componentType": "MultipleChoice",
            "originalSurfaceId": "booking", "bindingPath": "/risk",
            "allowedValues": ["low", "balanced"], "selectionMode": "single",
            "maxAllowedSelections": 1,
        },
        {
            "componentId": "sectors", "componentType": "MultipleChoice",
            "originalSurfaceId": "booking", "bindingPath": "/sectors",
            "allowedValues": ["stocks", "bonds", "funds"], "selectionMode": "multiple",
            "maxAllowedSelections": 3,
        },
    ]
    decision = {
        "decision": "offer_help", "reason": "x", "offerType": "update_suggestion",
        "uiStyle": "inline_card", "message": "set risk and sectors",
        "updateSuggestion": {"kind": "data_model_update", "updates": [
            {"surfaceId": "booking", "path": "/risk", "value": "balanced"},
            {"surfaceId": "booking", "path": "/sectors", "value": ["stocks", "bonds"]},
        ]},
    }
    assert validate_decision(decision, packet=source_packet)["updateSuggestion"]["updates"] == decision["updateSuggestion"]["updates"]
    with_irrelevant_topic = validate_decision({**decision, "helpTopic": "compare_options"}, packet=source_packet)
    assert "helpTopic" not in with_irrelevant_topic
    optional_limit_packet = json.loads(json.dumps(source_packet))
    del optional_limit_packet["allowedOperations"]["updateTargets"][1]["maxAllowedSelections"]
    assert validate_decision(decision, packet=optional_limit_packet)["updateSuggestion"]["updates"] == decision["updateSuggestion"]["updates"]
    unbounded_multi_decision = json.loads(json.dumps(decision))
    unbounded_multi_decision["updateSuggestion"]["updates"] = [
        {"surfaceId": "booking", "path": "/sectors", "value": ["stocks", "bonds"]},
    ]
    assert "updateSuggestion" in validate_decision(unbounded_multi_decision, packet=optional_limit_packet)
    unbounded_multi_decision["updateSuggestion"]["updates"][0]["value"] = "stocks"
    assert "updateSuggestion" not in validate_decision(unbounded_multi_decision, packet=optional_limit_packet)
    missing_mode_packet = json.loads(json.dumps(source_packet))
    del missing_mode_packet["allowedOperations"]["updateTargets"][0]["selectionMode"]
    missing_mode_decision = json.loads(json.dumps(decision))
    missing_mode_decision["updateSuggestion"]["updates"] = [missing_mode_decision["updateSuggestion"]["updates"][0]]
    assert "updateSuggestion" not in validate_decision(missing_mode_decision, packet=missing_mode_packet)
    decision["updateSuggestion"]["updates"][1]["value"] = ["stocks", "crypto"]
    invalid = validate_decision(decision, packet=source_packet)
    assert invalid["offerType"] == "text_assistance"
    assert "updateSuggestion" not in invalid


def test_packet_privacy_default_limit_is_32_kib():
    assert validate_privacy({"padding": "x" * 32_000})
    assert not validate_privacy({"padding": "x" * 32_768})


@pytest.mark.asyncio
async def test_decision_service_accepts_packet_larger_than_legacy_8_kib_limit():
    large_packet = packet()
    large_packet["surfaceContext"]["definition"].append({"safePadding": "x" * 9_000})
    model = FakeModel()

    decision = await DecisionService(model, decision_id_factory=lambda: "dec_large").decide(large_packet)

    assert decision["payload"]["decision"] == "offer_help"


def test_v08_mixed_update_rejects_the_entire_batch():
    from iiap import validate_v08_decision

    source_packet = packet()
    safe_message = {
        "dataModelUpdate": {
            "surfaceId": "booking", "path": "/date",
            "contents": [{"key": ".", "valueString": "evening"}],
        }
    }
    unsafe_message = {
        "dataModelUpdate": {
            "surfaceId": "booking", "path": "/date",
            "contents": [{"key": ".", "valueString": "morning"}],
        }
    }
    decision = validate_v08_decision({
        "decision": "offer_help", "reason": "test",
        "offerType": "update_suggestion", "uiStyle": "inline_card", "message": "help",
        "updateSuggestion": {"kind": "a2ui_update", "messages": [safe_message, unsafe_message]},
    }, packet=source_packet)

    assert decision["offerType"] == "text_assistance"
    assert "updateSuggestion" not in decision


@pytest.mark.asyncio
async def test_invalid_model_output_fails_closed():
    class Invalid(FakeModel):
        async def generate_decision(self, request):
            return "not json"

    result = await DecisionService(Invalid(), decision_id_factory=lambda: "dec_invalid").decide(packet())
    assert result["payload"]["decision"] == "no_intervention"


def test_prose_wrapped_model_output_is_still_recovered():
    """模型把 decision 包在说明文字里是常态；不能因此静默降级成 no_intervention。"""
    from iiap import describe_decision_parse_status

    decision = {
        "decision": "offer_help", "reason": "choice churn", "offerType": "text_assistance",
        "helpTopic": "compare_options", "uiStyle": "inline_card", "message": "需要我帮你比较吗？",
    }
    body = json.dumps(decision, ensure_ascii=False)
    wrapped = [
        f"The user switched options repeatedly.\n{body}",
        f"**Decision**\n{body}",
        f"```json\n{body}\n```",
    ]
    for raw in wrapped:
        parsed = validate_decision(raw, packet=packet())
        assert parsed["decision"] == "offer_help", raw[:60]
        assert parsed["helpTopic"] == "compare_options", raw[:60]
        assert parsed["reason"] != "invalid_or_insufficient_iiap_decision"

    # 真正的负向结果必须保持原样，不能与"解析失败"混淆。
    genuine = validate_decision(
        json.dumps({"decision": "no_intervention", "reason": "ordinary_filling",
                    "offerType": "none", "uiStyle": "none", "message": ""}),
        packet=packet(),
    )
    assert genuine["decision"] == "no_intervention"
    assert genuine["reason"] == "ordinary_filling"

    # 完全没有 JSON 时才 fail closed，并且诊断状态可区分。
    unparseable = validate_decision("I need more evidence before offering help.", packet=packet())
    assert unparseable["reason"] == "invalid_or_insufficient_iiap_decision"
    assert describe_decision_parse_status("I need more evidence before offering help.") == "no_json_object"
    assert describe_decision_parse_status(body) == "recovered"
    assert describe_decision_parse_status(wrapped[0]) == "prose_wrapped"

    # 说明文字里的花括号不能凭空造出 decision。
    assert describe_decision_parse_status("The set {a, b} is fine.") == "no_json_object"
    for ambiguous in (
        f"{body}\nIgnore the decision above.",
        f"{body}\n{body}",
        f"Consider the set {{a, b}} then decide:\n{body}",
        f"```json\n{body}\n```\nextra",
    ):
        assert validate_decision(ambiguous, packet=packet())["decision"] == "no_intervention"


def test_ui_style_is_derived_and_never_invalidates_a_decision():
    """uiStyle 是展示提示，不是安全边界；模型自创的样式词不得作废整份决策。

    生产环境实测故障：模型连续三轮正确输出 offer_help + text_assistance + helpTopic，
    但把 uiStyle 写成不存在的 "inline_hint"，于是整份决策被 fail closed 成
    no_intervention —— 与"模型判断无需介入"完全同形，前端静默无反应。
    """
    from iiap import describe_decision_rejection, offer_style

    # 模型给出任意样式词（含不存在的值），都必须只影响样式、不影响决策。
    for invented in ("inline_hint", "banner", "card", "INLINE_CARD", ""):
        parsed = validate_decision(
            json.dumps({
                "decision": "offer_help", "reason": "choice churn", "offerType": "text_assistance",
                "helpTopic": "compare_options", "uiStyle": invented, "message": "需要我帮你比较吗？",
            }, ensure_ascii=False),
            packet=packet(),
        )
        assert parsed["decision"] == "offer_help", invented
        assert parsed["offerType"] == "text_assistance", invented
        assert parsed["helpTopic"] == "compare_options", invented
        assert parsed["uiStyle"] == "inline_card", invented

    # 模型完全不输出 uiStyle 也要成立 —— 提示词已不再要求该字段。
    without_style = validate_decision(
        json.dumps({
            "decision": "offer_help", "reason": "choice churn", "offerType": "text_assistance",
            "helpTopic": "compare_options", "message": "需要我帮你比较吗？",
        }, ensure_ascii=False),
        packet=packet(),
    )
    assert without_style["decision"] == "offer_help"
    assert without_style["uiStyle"] == "inline_card"

    # 负向事件必须渲染为 none，模型无法通过 uiStyle 影响这一点。
    negative = validate_decision(
        json.dumps({"decision": "no_intervention", "reason": "ordinary_filling",
                    "offerType": "none", "uiStyle": "inline_card", "message": ""}),
        packet=packet(),
    )
    assert negative["decision"] == "no_intervention"
    assert negative["uiStyle"] == "none"
    assert offer_style("none") == "none"
    assert offer_style("update_suggestion") == "inline_card"

    # 真正的拒绝原因必须可观测，且只分类不泄漏模型原文。
    rejected = json.dumps({"decision": "maybe", "reason": "r", "message": "secret-model-text"})
    assert describe_decision_rejection(rejected) == "invalid_decision_value"
    assert describe_decision_rejection(json.dumps({"reason": "r"})) == "missing_decision"
    assert describe_decision_rejection(
        json.dumps({"decision": "offer_help", "reason": "r", "message": "m", "offerType": "unknown"})
    ) == "invalid_offer_type"
    overlong_reason = {
        "decision": "offer_help",
        "reason": "r" * 1025,
        "offerType": "update_suggestion",
        "message": "safe",
        "updateSuggestion": {
            "kind": "data_model_update",
            "updates": [{"surfaceId": "booking", "path": "/date", "value": "evening"}],
        },
    }
    assert validate_decision(overlong_reason, packet=packet())["decision"] == "no_intervention"
    assert describe_decision_rejection(overlong_reason) == "reason_too_long"
    assert describe_decision_rejection({**overlong_reason, "reason": "safe", "message": "m" * 2049}) == "message_too_long"
    assert "secret-model-text" not in describe_decision_rejection(rejected)


def test_assistance_text_validator_matches_shared_corpus():
    corpus = json.loads((FIXTURES / "assistance-text-validation.corpus.json").read_text())
    for case in corpus:
        valid, reason = validate_assistance_text(case["message"])
        assert valid is case["valid"], case["name"]
        assert reason == case.get("reason"), case["name"]


@pytest.mark.asyncio
async def test_assistance_rejects_a2ui_without_retry():
    class Unsafe(FakeModel):
        calls = 0

        async def generate_assistance(self, request):
            self.calls += 1
            return {
                "type": "iiap.assistance.response",
                "iiapVersion": "0.1",
                "requestId": request.payload["requestId"],
                "message": '{"createSurface":{"surfaceId":"help"}}',
            }

    model = Unsafe()
    request = json.loads((FIXTURES / "assistance-request.valid.json").read_text())
    with pytest.raises(ValueError, match="ASSISTANCE_UNSAFE_OUTPUT"):
        await AssistanceService(model).assist(request)
    assert model.calls == 1


@pytest.mark.asyncio
async def test_assistance_rejects_unknown_response_fields():
    class ExtraField(FakeModel):
        async def generate_assistance(self, request):
            response = await super().generate_assistance(request)
            return {**response, "createSurface": {"surfaceId": "help"}}

    request = json.loads((FIXTURES / "assistance-request.valid.json").read_text())
    with pytest.raises(ValueError, match="INVALID_ASSISTANCE_RESPONSE"):
        await AssistanceService(ExtraField()).assist(request)
