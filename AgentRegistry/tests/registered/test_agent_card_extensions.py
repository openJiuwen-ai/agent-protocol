"""Agent-card extension fields must survive registry parsing and output shape."""

from a2x_registry.register.models import AgentCard


def test_capabilities_skill_and_provider_extensions_survive_model_dump():
    card = AgentCard.model_validate({
        "name": "extended-agent",
        "description": "An agent with protocol extensions",
        "version": "1.0.0",
        "capabilities": {
            "streaming": True,
            "extensions": [{"uri": "https://example.test/ext", "required": False}],
        },
        "skills": [{
            "id": "skill-1",
            "name": "skill",
            "description": "does things",
            "tags": ["test"],
            "extension": "https://example.test/skill-ext",
        }],
        "provider": {
            "organization": "Example",
            "url": "https://example.test",
            "extra": "preserved",
        },
    })

    dumped = card.model_dump(exclude_none=True)

    assert dumped["capabilities"]["extensions"] == [
        {"uri": "https://example.test/ext", "required": False}
    ]
    assert dumped["skills"][0]["extension"] == "https://example.test/skill-ext"
    assert dumped["provider"]["extra"] == "preserved"
