# coding: utf-8
"""LLM client adapter for ACP SDK."""
from __future__ import annotations

from collections.abc import Iterator
import time
from typing import Any, Callable, Optional, Sequence


class ACPClient:
    """A thin adapter around an LLM client.

    Supports:
    1) openai-style client: client.chat.completions.create(...)
    2) custom callable: completion_fn(prompt=..., system=..., model=..., temperature=...)
    """

    def __init__(
        self,
        llm_client: Optional[Any] = None,
        completion_fn: Optional[Callable[..., str]] = None,
        model: Optional[str] = None,
        temperature: float = 0.0,
        system_prompt: Optional[str] = None,
    ) -> None:
        self._llm_client = llm_client
        self._completion_fn = completion_fn
        self._model = model
        self._temperature = temperature
        self._system_prompt = system_prompt

    def complete(
        self,
        prompt: str,
        *,
        system: Optional[str] = None,
        model: Optional[str] = None,
        temperature: Optional[float] = None,
    ) -> str:
        """Return a text completion for the given prompt.

        For OpenAI-compatible chat clients, this aggregates from the streaming API
        so ACP SDK can consistently use streamed LLM generation everywhere.
        """
        if self._completion_fn is not None:
            return str(
                self._completion_fn(
                    prompt=prompt,
                    system=system or self._system_prompt,
                    model=model or self._model,
                    temperature=self._temperature if temperature is None else temperature,
                )
            )

        if self._llm_client is None:
            raise RuntimeError("ACPClient requires llm_client or completion_fn")

        if hasattr(self._llm_client, "chat") and hasattr(self._llm_client.chat, "completions"):
            return "".join(
                self.stream_complete(
                    prompt,
                    system=system,
                    model=model,
                    temperature=temperature,
                )
            ).strip()

        messages = []
        if system or self._system_prompt:
            messages.append({"role": "system", "content": system or self._system_prompt})
        messages.append({"role": "user", "content": prompt})

        if hasattr(self._llm_client, "completions"):
            response = self._llm_client.completions.create(
                model=model or self._model,
                prompt=prompt,
                temperature=self._temperature if temperature is None else temperature,
            )
            return (response.choices[0].text or "").strip()

        raise RuntimeError("Unsupported llm_client interface for ACPClient")

    def stream_complete(
        self,
        prompt: str,
        *,
        system: Optional[str] = None,
        model: Optional[str] = None,
        temperature: Optional[float] = None,
    ) -> Iterator[str]:
        """Yield text chunks for the given prompt."""
        if self._completion_fn is not None:
            yield self.complete(prompt, system=system, model=model, temperature=temperature)
            return

        if self._llm_client is None:
            raise RuntimeError("ACPClient requires llm_client or completion_fn")

        messages = []
        if system or self._system_prompt:
            messages.append({"role": "system", "content": system or self._system_prompt})
        messages.append({"role": "user", "content": prompt})

        if hasattr(self._llm_client, "chat") and hasattr(self._llm_client.chat, "completions"):
            max_attempts = 3
            for attempt in range(max_attempts):
                yielded_any = False
                try:
                    stream = self._llm_client.chat.completions.create(
                        model=model or self._model,
                        messages=messages,
                        temperature=self._temperature if temperature is None else temperature,
                        stream=True,
                    )
                    for chunk in stream:
                        choices = getattr(chunk, "choices", None) or []
                        if not choices:
                            continue
                        delta = getattr(choices[0], "delta", None)
                        text = getattr(delta, "content", None) if delta else None
                        if text:
                            yielded_any = True
                            yield str(text)
                    return
                except Exception:
                    if yielded_any or attempt >= max_attempts - 1:
                        raise
                    time.sleep(0.8 * (attempt + 1))
            return

        raise RuntimeError("Unsupported llm_client interface for ACPClient streaming")

    @staticmethod
    def ensure_text(value: Any) -> str:
        if value is None:
            return ""
        return str(value)

    @staticmethod
    def ensure_messages(prompt: str, system: Optional[str] = None) -> Sequence[dict]:
        if system:
            return [{"role": "system", "content": system}, {"role": "user", "content": prompt}]
        return [{"role": "user", "content": prompt}]
