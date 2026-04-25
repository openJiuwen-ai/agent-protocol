"""LLM provider management API."""

import json
import logging

from fastapi import APIRouter

from a2x_registry.backend.services.search_service import search_service
from a2x_registry.common.paths import llm_apikey_path

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/providers", tags=["providers"])


def _load_llm_config() -> dict:
    config_path = llm_apikey_path()
    with open(config_path, encoding="utf-8") as f:
        content = f.read().replace(',\n}', '\n}').replace(',}', '}')
        return json.loads(content)


@router.get("/")
async def list_providers():
    """List available LLM providers and the currently active one."""
    config = _load_llm_config()
    providers = config.get("providers", [])
    current = search_service.get_current_provider() or (providers[0]["name"] if providers else "")
    return {
        "providers": [{"name": p["name"], "model": p["model"]} for p in providers],
        "current": current,
    }


@router.post("/{name}")
async def switch_provider(name: str):
    """Switch all LLM clients to use a different provider.

    Reorders providers in llm_apikey.json (so LLMClient picks up the new default)
    and resets all cached A2X engine instances.
    """
    config = _load_llm_config()
    providers = config.get("providers", [])
    valid_names = [p["name"] for p in providers]
    if name not in valid_names:
        return {"error": f"Unknown provider: {name}", "valid": valid_names}

    # Reorder providers: put selected first, keep rest in original order
    target_idx = valid_names.index(name)
    config["providers"] = [providers[target_idx]] + [
        p for i, p in enumerate(providers) if i != target_idx
    ]

    config_path = llm_apikey_path()
    with open(config_path, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2, ensure_ascii=False)

    search_service.switch_provider(name)
    logger.info("Switched LLM provider to: %s", name)

    return {"status": "ok", "current": name}
