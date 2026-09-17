"""Vector utilities — lite-safe init.

The two embedding-model literal constants are loaded eagerly (zero heavy
deps). The classes that *do* need numpy / sentence_transformers / chromadb
(``EmbeddingModel``, ``ChromaStore``, ``metrics``) are exposed through
PEP 562 ``__getattr__`` so they're only imported on first access.

This means ``import a2x_registry.vector.utils`` succeeds in lite installs;
heavy attributes only fail (with the underlying ImportError) when actually
touched. Callers on heavy paths still write ``from a2x_registry.vector.utils
import EmbeddingModel`` exactly as before — no behavior change.
"""

from .embedding_constants import DEFAULT_EMBEDDING_MODEL, EMBEDDING_MODELS

__all__ = [
    "DEFAULT_EMBEDDING_MODEL",
    "EMBEDDING_MODELS",
    "EmbeddingModel",
    "ChromaStore",
    "metrics",
]


def __getattr__(name):
    if name == "EmbeddingModel":
        from .embedding import EmbeddingModel
        return EmbeddingModel
    if name == "ChromaStore":
        from .chroma_store import ChromaStore
        return ChromaStore
    if name == "metrics":
        from . import metrics
        return metrics
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__():
    return sorted(__all__)
