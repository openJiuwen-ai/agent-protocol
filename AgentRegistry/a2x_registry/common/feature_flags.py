"""Runtime probe for optional backend features.

Maps a logical feature name (e.g. ``"vector"``) to the pip extras that
provides it and the actual Python modules that need to be importable.
``find_spec`` is intentionally **not** cached — that way ``pip install
a2x-registry[vector]`` followed by a backend restart is reflected
immediately, with no in-process state to invalidate.

Used by:
  - FastAPI handlers (search/build routers) — call :func:`require` at the
    entry point so a missing feature raises
    :class:`~a2x_registry.common.errors.FeatureNotInstalledError`, which
    the app-level exception handler renders as a 503 with a structured
    install hint.
  - Heavy CLI entry points (``a2x-build``, ``a2x-evaluate-*``) — same
    ``require()`` pattern; the CLI prints ``str(exc)`` and exits 2.
  - Backend startup / search-service early-returns — :func:`has` lets
    callbacks and warmup stages skip themselves silently in lite installs
    instead of spamming ``ImportError`` logs on every register.
"""

from __future__ import annotations

import importlib.util

from .errors import FeatureNotInstalledError

# feature name → (extras key, modules whose presence indicates the feature)
_FEATURES: dict[str, tuple[str, tuple[str, ...]]] = {
    "vector":     ("vector",     ("numpy", "sentence_transformers", "chromadb")),
    "evaluation": ("evaluation", ("tqdm",)),
}


def has(feature: str) -> bool:
    """Return True if every module backing ``feature`` is importable.

    No caching: re-probes on every call so install + restart picks up
    immediately. The cost is a few microseconds of ``find_spec`` per call.
    """
    if feature not in _FEATURES:
        raise KeyError(f"Unknown feature {feature!r}; known: {sorted(_FEATURES)}")
    _, modules = _FEATURES[feature]
    return all(importlib.util.find_spec(m) is not None for m in modules)


def require(feature: str) -> None:
    """Raise :class:`FeatureNotInstalledError` if ``feature`` is unavailable.

    The error message is a copy-pasteable ``pip install`` command. In a
    FastAPI handler the app-level exception handler turns it into 503 +
    JSON body. In a CLI ``main()`` the entry script catches it, prints to
    stderr, and exits with code 2 (conventional "usage error").
    """
    if not has(feature):
        extras, _ = _FEATURES[feature]
        raise FeatureNotInstalledError(feature=feature, extras=extras)
