"""User-facing errors for missing or unavailable subsystems.

Each error subclass carries an actionable message — the caller (CLI or
HTTP handler) can print ``str(exc)`` directly to surface remediation steps.
"""

from __future__ import annotations


class A2XRegistryError(RuntimeError):
    """Base class for library-level user-facing errors."""


class VectorSearchUnavailableError(A2XRegistryError):
    """Embedding model failed to load — vector search is unavailable.

    Raised when :class:`~a2x_registry.vector.utils.embedding.EmbeddingModel`
    cannot instantiate the underlying SentenceTransformer (typically because
    the HuggingFace model download is blocked by regional restrictions, or
    the model has never been cached and this host is offline).
    """


class LLMNotConfiguredError(A2XRegistryError):
    """LLM API key config is missing or invalid — A2X build/search unavailable.

    Raised when :class:`~a2x_registry.common.llm_client.LLMClient` cannot
    locate, parse, or find any usable provider in ``llm_apikey.json``.
    """
