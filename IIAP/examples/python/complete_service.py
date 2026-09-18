import asyncio
import json
from pathlib import Path

from iiap import AssistanceService, DecisionService, ModelRequest


class DemoModelAdapter:
    async def generate_decision(self, request: ModelRequest) -> object:
        assert request.operation == "decision"
        return {
            "decision": "offer_help",
            "reason": "repeated_option_change",
            "offerType": "text_assistance",
            "helpTopic": "compare_options",
            "uiStyle": "inline_card",
            "message": "需要我帮你比较这些选项吗？",
        }

    async def generate_assistance(self, request: ModelRequest) -> object:
        assert request.operation == "assistance"
        return {
            "type": "iiap.assistance.response",
            "iiapVersion": "0.1",
            "requestId": request.payload["requestId"],
            "message": "可以先按预算和使用频率比较，再选择最合适的选项。",
        }


async def main() -> None:
    iiap_root = Path(__file__).resolve().parents[2]
    packet = json.loads(
        (iiap_root / "contracts/fixtures/packet.valid.json").read_text(encoding="utf-8")
    )
    model = DemoModelAdapter()
    decision = await DecisionService(
        model,
        decision_id_factory=lambda: "decision-demo-1",
    ).decide(packet)
    print("decision:", decision)

    request = {
        "type": "iiap.assistance.request",
        "iiapVersion": "0.1",
        "requestId": "assistance-demo-1",
        "packetId": decision["packetId"],
        "decisionId": decision["decisionId"],
        "surfaceInstanceId": decision["surfaceInstanceId"],
        "topic": decision["payload"].get("helpTopic", "compare_options"),
        "language": "zh-CN",
    }
    assistance = await AssistanceService(model).assist(request)
    print("assistance:", assistance)


if __name__ == "__main__":
    asyncio.run(main())
