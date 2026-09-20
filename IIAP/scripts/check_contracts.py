"""Validate contract fixtures and forbidden-key guarantees."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource

ROOT = Path(__file__).parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--packet", type=Path, help="validate a captured IntentContextPacket JSON file")
args = parser.parse_args()
CASES = {
    "packet.valid.json": "intent-context-packet.schema.json",
    "packet-envelope.valid.json": "packet-envelope.schema.json",
    "decision.valid.json": "decision.schema.json",
    "assistance-request.valid.json": "assistance.schema.json",
    "assistance-response.valid.json": "assistance.schema.json",
    "feedback.valid.json": "feedback.schema.json",
}

packet_schema = json.loads((ROOT / "contracts/schemas/intent-context-packet.schema.json").read_text())
packaged_packet_schema = json.loads((
    ROOT / "implementations/python/src/iiap/schemas/intent-context-packet.schema.json"
).read_text())
assert packaged_packet_schema == packet_schema, (
    "the Python wheel packet schema must match contracts/schemas/intent-context-packet.schema.json"
)
schema_registry = Registry().with_resource(
    packet_schema["$id"],
    Resource.from_contents(packet_schema),
)

for fixture_name, schema_name in CASES.items():
    fixture = json.loads((ROOT / "contracts/fixtures" / fixture_name).read_text())
    schema = json.loads((ROOT / "contracts/schemas" / schema_name).read_text())
    Draft202012Validator(
        schema,
        format_checker=FormatChecker(),
        registry=schema_registry,
    ).validate(fixture)

packet_envelope = json.loads((ROOT / "contracts/fixtures/packet-envelope.valid.json").read_text())
packet_validator = Draft202012Validator(packet_schema, format_checker=FormatChecker())
packet_validator.validate(packet_envelope["packet"])
packet_envelope_schema = json.loads((ROOT / "contracts/schemas/packet-envelope.schema.json").read_text())
packet_envelope_validator = Draft202012Validator(
    packet_envelope_schema,
    format_checker=FormatChecker(),
    registry=schema_registry,
)
assert not packet_envelope_validator.is_valid({
    **packet_envelope,
    "packet": {**packet_envelope["packet"], "unexpected": True},
})
for legacy_field in ("signals", "hypotheses", "uiContext"):
    invalid_packet = {**packet_envelope["packet"], legacy_field: [] if legacy_field != "uiContext" else {}}
    assert not packet_validator.is_valid(invalid_packet)

packet_with_target = json.loads((ROOT / "contracts/fixtures/packet.valid.json").read_text())
target = packet_with_target["allowedOperations"]["updateTargets"][0]
assert not packet_validator.is_valid({
    **packet_with_target,
    "allowedOperations": {"updateTargets": [{**target, "allowedValues": []}]},
})
assert not packet_validator.is_valid({
    **packet_with_target,
    "allowedOperations": {"updateTargets": [{
        key: value for key, value in target.items() if key != "allowedValues"
    } | {"suggestionValues": target["allowedValues"]}]},
})

# selectionMode 独立定义标量/集合形状；maxAllowedSelections 只是可选上限。
unbounded_multi_target = {
    **target,
    "componentType": "MultipleChoice",
    "allowedValues": ["stocks", "bonds"],
    "selectionMode": "multiple",
}
assert packet_validator.is_valid({
    **packet_with_target,
    "allowedOperations": {"updateTargets": [unbounded_multi_target]},
})
assert not packet_validator.is_valid({
    **packet_with_target,
    "allowedOperations": {"updateTargets": [{**target, "maxAllowedSelections": 2}]},
})
assert not packet_validator.is_valid({
    **packet_with_target,
    "allowedOperations": {"updateTargets": [{
        **unbounded_multi_target, "maxAllowedSelections": 1,
    }]},
})

# testScenario 只接受 test profile 的确定性场景枚举，且仍然是可选字段。
TEST_SCENARIOS = {
    "model", "compare_options", "explain_rules", "fix_block",
    "save_progress", "update_suggestion", "no_intervention", "defer", "invalid_json",
}
assert packet_validator.is_valid(packet_with_target)
for scenario in TEST_SCENARIOS:
    assert packet_validator.is_valid({**packet_with_target, "testScenario": scenario})
assert not packet_validator.is_valid({**packet_with_target, "testScenario": "bogus"})
assert not packet_validator.is_valid({**packet_with_target, "testScenario": True})

# surfaceContext.definition 是脱敏增量 message 序列，不限制条数；
# 组件总数上限（128）与 16 KiB 裁剪由 Adapter 实现并由 redaction.truncated 标记。
definition_probe = {**packet_envelope["packet"]}
assert packet_validator.is_valid({
    **definition_probe,
    "surfaceContext": {
        **definition_probe["surfaceContext"],
        "definition": [{"surfaceUpdate": {"surfaceId": "booking", "components": []}}] * 6,
    },
})
assert not packet_validator.is_valid({
    **definition_probe,
    "surfaceContext": {**definition_probe["surfaceContext"], "definition": []},
})

# 事件数组上限 128，且裁剪必须如实反映在 completeness 中。
event_probe = packet_envelope["packet"]
single_event = event_probe["observations"]["events"][0]


def _packet_with_events(count: int, *, complete: bool, dropped: int) -> dict:
    return {
        **event_probe,
        "observations": {
            "tokenScope": "component_within_surface_instance",
            "events": [{**single_event, "eventId": f"event_{index}", "sequence": index}
                       for index in range(1, count + 1)],
            "completeness": {"complete": complete, "droppedEventCount": dropped},
        },
    }


assert packet_validator.is_valid(_packet_with_events(128, complete=True, dropped=0))
assert packet_validator.is_valid(_packet_with_events(128, complete=False, dropped=72))
assert not packet_validator.is_valid(_packet_with_events(129, complete=False, dropped=72))
assert not packet_validator.is_valid(_packet_with_events(128, complete=False, dropped=-1))

if args.packet:
    captured = json.loads(args.packet.read_text())
    if isinstance(captured, dict) and captured.get("type") == "iiap.intent_context_packet":
        captured = captured.get("packet")
    packet_validator.validate(captured)
    print(f"validated captured packet: {args.packet}")

decision_schema = json.loads((ROOT / "contracts/schemas/decision.schema.json").read_text())
decision_fixture = json.loads((ROOT / "contracts/fixtures/decision.valid.json").read_text())
decision_validator = Draft202012Validator(decision_schema, format_checker=FormatChecker())
for invalid in (
    {key: value for key, value in decision_fixture.items() if key != "packetId"},
    {**decision_fixture, "iiapVersion": "0.2"},
    {**decision_fixture, "unexpected": True},
):
    assert not decision_validator.is_valid(invalid)

# Decision 的三类终态组合必须互斥：无介入/延后不携带 offer 数据，
# 文字帮助必须有 topic，更新帮助必须有 suggestion。
text_payload = decision_fixture["payload"]
update_payload = {
    "decision": "offer_help",
    "reason": "safe_update_available",
    "offerType": "update_suggestion",
    "uiStyle": "inline_card",
    "message": "I can apply this change.",
    "updateSuggestion": {
        "kind": "data_model_update",
        "updates": [{"surfaceId": "booking", "path": "/date", "value": "evening"}],
    },
}
no_intervention_payload = {
    "decision": "no_intervention", "reason": "ordinary_progress",
    "offerType": "none", "uiStyle": "none", "message": "",
}
for payload in (text_payload, update_payload, no_intervention_payload):
    assert decision_validator.is_valid({**decision_fixture, "payload": payload})
for payload in (
    {key: value for key, value in text_payload.items() if key != "helpTopic"},
    {**text_payload, "updateSuggestion": update_payload["updateSuggestion"]},
    {key: value for key, value in update_payload.items() if key != "updateSuggestion"},
    {**update_payload, "helpTopic": "explain_rules"},
    {**no_intervention_payload, "offerType": "text_assistance", "helpTopic": "explain_rules"},
    {**no_intervention_payload, "helpTopic": "explain_rules"},
    {**text_payload, "offerType": "none", "uiStyle": "none"},
    {**text_payload, "uiStyle": "none"},
):
    assert not decision_validator.is_valid({**decision_fixture, "payload": payload})

# uiStyle 是 Host 依据 offerType 推导的展示提示，取值域只有 inline_card | none。
# 历史缺陷：schema 曾声明 chip/side_panel/toast，而运行时只接受 inline_card/none，
# 契约与实现长期不一致。这里同时锁死取值域与 TS 类型的单一声明，防止再次漂移。
OFFER_STYLES = ("inline_card", "none")
for payload in (
    {**text_payload, "uiStyle": "chip"},
    {**text_payload, "uiStyle": "side_panel"},
    {**text_payload, "uiStyle": "toast"},
    {**update_payload, "uiStyle": "chip"},
    {**no_intervention_payload, "uiStyle": "inline_card"},
):
    assert not decision_validator.is_valid({**decision_fixture, "payload": payload})
assert decision_validator.is_valid({**decision_fixture, "payload": {**text_payload, "uiStyle": "inline_card"}})
assert decision_validator.is_valid({**decision_fixture, "payload": no_intervention_payload})

# schema 的取值域必须与 TS 唯一类型声明一致，避免"改了实现忘了改契约"。
types_source = (
    ROOT / "implementations/typescript/packages/core/src/types.ts"
).read_text()
style_declaration = re.search(r"^\s*uiStyle: (.+);\s*$", types_source, re.MULTILINE)
assert style_declaration, "types.ts must declare the uiStyle union"
declared_styles = tuple(re.findall(r"'([a-z_]+)'", style_declaration.group(1)))
assert declared_styles == OFFER_STYLES, (
    f"types.ts declares uiStyle {declared_styles} but this script expects {OFFER_STYLES}; "
    "update the schema enum and the Python offer_style map in the same change"
)
payload_schema = decision_schema["properties"]["payload"]
positive_schema = payload_schema["allOf"][0]["then"]["properties"]["uiStyle"]
assert payload_schema["properties"]["uiStyle"]["enum"] == list(OFFER_STYLES), (
    "decision.schema.json uiStyle enum must match the TS union"
)
# offer_help 分支只能是 inline_card —— 负向结果的 none 由 else 分支的 const 约束。
assert positive_schema == {"const": "inline_card"}, (
    "offer_help branch must pin uiStyle to the single positive value"
)
assert payload_schema["allOf"][0]["else"]["properties"]["uiStyle"] == {"const": "none"}

feedback_schema = json.loads((ROOT / "contracts/schemas/feedback.schema.json").read_text())
feedback_fixture = json.loads((ROOT / "contracts/fixtures/feedback.valid.json").read_text())
feedback_validator = Draft202012Validator(feedback_schema, format_checker=FormatChecker())
for invalid in (
    {key: value for key, value in feedback_fixture.items() if key != "decisionId"},
    {**feedback_fixture, "interaction": "accepted"},
    {**feedback_fixture, "outcome": "succeeded"},
):
    assert not feedback_validator.is_valid(invalid)

print(f"validated {len(CASES)} IIAP contract fixtures")
