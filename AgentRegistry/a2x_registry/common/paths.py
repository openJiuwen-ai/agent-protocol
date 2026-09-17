"""Runtime path resolution.

The library ships **without** bundled runtime data. Two external resources
must be resolvable at runtime:

- ``llm_apikey.json`` — credentials for external LLM providers.
  **Default**: ``~/.a2x_registry/llm_apikey.json``.
  Override with the ``A2X_REGISTRY_HOME`` env var → the file is then looked
  for at ``<A2X_REGISTRY_HOME>/llm_apikey.json``. The CWD is intentionally
  *not* probed — credentials stay out of project trees.

- ``database/`` — per-dataset service / taxonomy / query files.
  Lookup order:
    1. ``<A2X_REGISTRY_HOME>/database`` (when the env var is set)
    2. ``./database`` under CWD — convenient for devs running from a
       cloned source tree where the database is a git submodule.
    3. ``~/.a2x_registry/database``

Neither target is created by this module; callers do that on write.

A template for ``llm_apikey.json`` ships inside the package as
:data:`LLM_APIKEY_EXAMPLE_PATH` (``a2x_registry/llm_apikey.example.json``).
"""

from __future__ import annotations

import os
from functools import lru_cache
from pathlib import Path

ENV_VAR = "A2X_REGISTRY_HOME"
DEFAULT_USER_HOME = Path.home() / ".a2x_registry"

LLM_APIKEY_EXAMPLE_PATH = Path(__file__).resolve().parent.parent / "llm_apikey.example.json"
"""Absolute path to the bundled ``llm_apikey.example.json`` template."""


def _env_home() -> Path | None:
    """Return the explicit ``A2X_REGISTRY_HOME`` if set, else ``None``."""
    env = os.environ.get(ENV_VAR)
    return Path(env).expanduser().resolve() if env else None


@lru_cache(maxsize=1)
def get_home() -> Path:
    """Home directory used by :func:`database_dir` — **not** the LLM key lookup.

    Lookup:
      1. ``A2X_REGISTRY_HOME`` env var
      2. CWD if it contains ``./database/`` (source-tree dev mode)
      3. ``~/.a2x_registry/``
    """
    env = _env_home()
    if env:
        return env
    if (Path.cwd() / "database").is_dir():
        return Path.cwd().resolve()
    return DEFAULT_USER_HOME


def database_dir() -> Path:
    return get_home() / "database"


def dataset_dir(dataset: str) -> Path:
    return database_dir() / dataset


def llm_apikey_path() -> Path:
    """Return the resolved ``llm_apikey.json`` path.

    Priority:
      1. ``<A2X_REGISTRY_HOME>/llm_apikey.json``  (if env var set)
      2. ``~/.a2x_registry/llm_apikey.json``       (default)

    Deliberately does *not* probe CWD — keeps credentials out of source trees.
    """
    env = _env_home()
    base = env if env else DEFAULT_USER_HOME
    return base / "llm_apikey.json"


def reset_cache() -> None:
    """Clear the cached home lookup. Tests call this after mutating ``A2X_REGISTRY_HOME``."""
    get_home.cache_clear()
