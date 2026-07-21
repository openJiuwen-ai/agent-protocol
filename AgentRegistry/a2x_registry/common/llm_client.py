"""
LLM API client with retry logic and batch support.

Provides a unified interface for calling LLM APIs (DeepSeek, OpenAI compatible).
Supports multiple providers with priority-based failover.
"""

import json
import logging
import time
import threading
from pathlib import Path
from typing import List, Dict, Any, Optional
from dataclasses import dataclass, field
from concurrent.futures import ThreadPoolExecutor, as_completed

import requests
from requests.adapters import HTTPAdapter

from a2x_registry.common.errors import LLMNotConfiguredError

logger = logging.getLogger(__name__)


@dataclass
class ProviderConfig:
    """Configuration for a single LLM provider."""
    name: str
    base_url: str
    model: str
    api_keys: List[str]
    current_key_index: int = 0


@dataclass
class LLMResponse:
    """Response from LLM API."""
    content: str
    tokens: int
    model: str
    success: bool = True
    error: Optional[str] = None
    provider: Optional[str] = None


class LLMClient:
    """Client for calling LLM APIs with retry logic.

    Supports multiple OpenAI-compatible providers with priority-based failover.
    Providers are tried in config order; within each provider, API keys rotate on failure.
    """

    def __init__(
        self,
        config_path: str | None = None,
        max_retries: int = 3,
        timeout: int = 120,
        pool_maxsize: int = 20,
    ):
        """Initialize LLM client.

        Args:
            config_path: Path to config file with providers list or legacy single-provider format.
                Default ``None`` resolves to ``<A2X_REGISTRY_HOME>/llm_apikey.json``
                via :mod:`a2x_registry.common.paths`.
            max_retries: Maximum retry attempts per API key
            timeout: Request timeout in seconds
            pool_maxsize: Max connections in the HTTP connection pool
        """
        if config_path is None:
            from a2x_registry.common.paths import llm_apikey_path
            config_path = str(llm_apikey_path())
        self.max_retries = max_retries
        self.timeout = timeout

        # Load config and parse providers, converting any failure into a
        # readable LLMNotConfiguredError so the CLI/HTTP layer can show it raw.
        config = self._load_config_or_raise(config_path)
        self.providers = self._parse_providers(config)

        if not self.providers:
            from a2x_registry.common.paths import LLM_APIKEY_EXAMPLE_PATH
            raise LLMNotConfiguredError(
                f"A2X build / search is unavailable: no valid LLM provider in "
                f"{config_path!r}.\n\n"
                f"Each entry under \"providers\" needs a non-empty \"api_keys\" list. "
                f"Fill in a real API key and retry. A bundled template is at\n"
                f"  {LLM_APIKEY_EXAMPLE_PATH}"
            )

        # Expose primary provider's model for backward compatibility
        self.model = self.providers[0].model

        self._lock = threading.Lock()

        # Statistics
        self.total_calls = 0
        self.total_tokens = 0

        # Reuse TCP connections across calls (avoids repeated DNS/TLS handshakes).
        # urllib3's pool is thread-safe; we only pass headers per-call (never
        # mutate session-level headers), so concurrent session.post() is safe.
        self._session = requests.Session()
        adapter = HTTPAdapter(
            pool_connections=len(self.providers),
            pool_maxsize=pool_maxsize,
        )
        self._session.mount("https://", adapter)
        self._session.mount("http://", adapter)

        provider_names = [p.name for p in self.providers]
        logger.info(f"LLMClient initialized with providers: {provider_names} (priority order)")

    def _load_config(self, config_path: str) -> Dict[str, Any]:
        """Load config from file. Caller supplies an absolute path (the default
        resolved via ``a2x_registry.common.paths``) or a relative one (tried as-is
        from CWD)."""
        with open(config_path, 'r', encoding='utf-8') as f:
            content = f.read()
            # Handle trailing commas in JSON
            content = content.replace(',\n}', '\n}').replace(',}', '}')
            return json.loads(content)

    def _load_config_or_raise(self, config_path: str) -> Dict[str, Any]:
        """Load config and translate failures into :class:`LLMNotConfiguredError`.

        Wraps :meth:`_load_config` so callers see an actionable message instead
        of a bare ``FileNotFoundError`` / ``JSONDecodeError`` traceback.
        """
        try:
            return self._load_config(config_path)
        except FileNotFoundError as exc:
            from a2x_registry.common.paths import LLM_APIKEY_EXAMPLE_PATH
            raise LLMNotConfiguredError(
                f"A2X build / search is unavailable: LLM API key file not found "
                f"at {config_path!r}.\n\n"
                f"To configure:\n"
                f"  1. Copy the bundled template into the default location:\n"
                f"         mkdir -p ~/.a2x_registry\n"
                f"         cp {LLM_APIKEY_EXAMPLE_PATH} ~/.a2x_registry/llm_apikey.json\n"
                f"     (or set the A2X_REGISTRY_HOME env var to point at a dir\n"
                f"      containing your own llm_apikey.json)\n"
                f"  2. Edit the file and fill in your provider's api_keys.\n"
                f"  3. Minimal inline example (OpenAI-compatible):\n"
                f'         {{\n'
                f'           "providers": [\n'
                f'             {{\n'
                f'               "name": "deepseek",\n'
                f'               "base_url": "https://api.deepseek.com/chat/completions",\n'
                f'               "model": "deepseek-chat",\n'
                f'               "api_keys": ["sk-your-key"]\n'
                f'             }}\n'
                f'           ]\n'
                f'         }}'
            ) from exc
        except json.JSONDecodeError as exc:
            raise LLMNotConfiguredError(
                f"A2X build / search is unavailable: the LLM config at "
                f"{config_path!r} is not valid JSON.\n"
                f"Parse error at line {exc.lineno}, column {exc.colno}: {exc.msg}"
            ) from exc
        except OSError as exc:
            raise LLMNotConfiguredError(
                f"A2X build / search is unavailable: cannot read the LLM config "
                f"at {config_path!r}: {type(exc).__name__}: {exc}"
            ) from exc

    @staticmethod
    def _parse_providers(config: Dict[str, Any]) -> List[ProviderConfig]:
        """Parse provider list from config, supporting both new and legacy formats.

        New format: {"providers": [{"name", "base_url", "model", "api_keys"}, ...]}
        Legacy format: {"base_url", "model", "api_key"/"api_keys"}
        """
        providers = []

        if "providers" in config and config["providers"]:
            for i, p in enumerate(config["providers"]):
                api_keys = p.get("api_keys") or []
                if not api_keys and p.get("api_key"):
                    api_keys = [p["api_key"]]
                if not api_keys:
                    logger.warning(f"Provider {p.get('name', i)} has no API keys, skipping")
                    continue
                providers.append(ProviderConfig(
                    name=p.get("name", f"provider_{i}"),
                    base_url=p["base_url"],
                    model=p["model"],
                    api_keys=api_keys,
                ))
        else:
            # Legacy single-provider format
            api_keys = config.get("api_keys") or []
            if not api_keys and config.get("api_key"):
                api_keys = [config["api_key"]]
            if api_keys:
                providers.append(ProviderConfig(
                    name="default",
                    base_url=config.get("base_url", "https://api.deepseek.com/chat/completions"),
                    model=config.get("model", "deepseek-chat"),
                    api_keys=api_keys,
                ))

        return providers

    def call(
        self,
        messages: List[Dict[str, str]],
        temperature: float = 0.0,
        max_tokens: Optional[int] = None
    ) -> LLMResponse:
        """Call LLM API with provider failover and key rotation.

        Tries providers in priority order. Within each provider, rotates API keys on failure.

        Args:
            messages: List of message dicts with 'role' and 'content'
            temperature: Sampling temperature (0.0 for deterministic)
            max_tokens: Maximum tokens in response

        Returns:
            LLMResponse with content and metadata
        """
        last_error = None

        for provider in self.providers:
            payload = {
                "model": provider.model,
                "messages": messages,
                "temperature": temperature,
            }
            if max_tokens:
                payload["max_tokens"] = max_tokens

            num_keys = len(provider.api_keys)

            for key_offset in range(num_keys):
                with self._lock:
                    key_idx = (provider.current_key_index + key_offset) % num_keys
                api_key = provider.api_keys[key_idx]

                headers = {
                    "Authorization": f"Bearer {api_key}",
                    "Content-Type": "application/json",
                }

                for attempt in range(self.max_retries):
                    try:
                        response = self._session.post(
                            provider.base_url,
                            headers=headers,
                            json=payload,
                            timeout=self.timeout,
                        )
                        response.raise_for_status()
                        result = response.json()

                        content = result["choices"][0]["message"]["content"]
                        tokens = result.get("usage", {}).get("total_tokens", 0)

                        with self._lock:
                            self.total_calls += 1
                            self.total_tokens += tokens
                            if key_offset > 0:
                                provider.current_key_index = key_idx

                        return LLMResponse(
                            content=content,
                            tokens=tokens,
                            model=provider.model,
                            provider=provider.name,
                        )

                    except (requests.exceptions.RequestException, json.JSONDecodeError) as e:
                        last_error = str(e)
                        if attempt < self.max_retries - 1:
                            wait_time = 2 ** attempt
                            logger.warning(
                                f"  [{provider.name}] Request failed, retrying in {wait_time}s... "
                                f"({attempt + 1}/{self.max_retries})"
                            )
                            time.sleep(wait_time)

                # Current key exhausted all retries, try next key
                if key_offset < num_keys - 1:
                    next_idx = (provider.current_key_index + key_offset + 1) % num_keys
                    logger.warning(
                        f"  [{provider.name}] Switching to API key {next_idx + 1}/{num_keys}..."
                    )

            # All keys for this provider exhausted, try next provider
            logger.warning(f"  [{provider.name}] All keys exhausted, falling back to next provider...")

        return LLMResponse(
            content="",
            tokens=0,
            model=self.providers[0].model,
            success=False,
            error=last_error,
        )

    def call_batch(
        self,
        prompts: List[str],
        system_prompt: str = "",
        temperature: float = 0.0,
        max_workers: int = 5
    ) -> List[LLMResponse]:
        """Call LLM for multiple prompts in parallel.

        Args:
            prompts: List of user prompts
            system_prompt: System prompt to prepend to all calls
            temperature: Sampling temperature
            max_workers: Maximum parallel workers

        Returns:
            List of LLMResponses in same order as prompts
        """
        results = [None] * len(prompts)

        def process_one(idx: int, prompt: str) -> tuple:
            messages = []
            if system_prompt:
                messages.append({"role": "system", "content": system_prompt})
            messages.append({"role": "user", "content": prompt})
            return idx, self.call(messages, temperature)

        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            futures = {
                executor.submit(process_one, i, p): i
                for i, p in enumerate(prompts)
            }

            for future in as_completed(futures):
                idx, response = future.result()
                results[idx] = response

        return results

    def get_stats(self) -> Dict[str, int]:
        """Get usage statistics."""
        return {
            "total_calls": self.total_calls,
            "total_tokens": self.total_tokens
        }

    def reset_stats(self) -> None:
        """Reset usage statistics."""
        self.total_calls = 0
        self.total_tokens = 0


def parse_json_response(text: str) -> Optional[Dict[str, Any]]:
    """Parse JSON from LLM response, handling markdown code blocks.

    Args:
        text: Raw LLM response text

    Returns:
        Parsed JSON dict or None if parsing fails
    """
    # Try direct parsing first
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass

    # Try extracting from markdown code block
    import re
    json_match = re.search(r'```(?:json)?\s*([\s\S]*?)\s*```', text)
    if json_match:
        try:
            return json.loads(json_match.group(1))
        except json.JSONDecodeError:
            pass

    # Try finding JSON object in text
    start = text.find('{')
    end = text.rfind('}')
    if start != -1 and end != -1 and end > start:
        try:
            return json.loads(text[start:end + 1])
        except json.JSONDecodeError:
            pass

    return None
