"""A2X Registry — hierarchical taxonomy service registry.

Public sub-packages:

- :mod:`a2x_registry.a2x` — taxonomy builder + recursive LLM search
- :mod:`a2x_registry.backend` — FastAPI backend exposing the above
- :mod:`a2x_registry.register` — service registration business logic
- :mod:`a2x_registry.vector` — ChromaDB-based vector baseline
- :mod:`a2x_registry.traditional` — full-context MCP-style baseline
- :mod:`a2x_registry.common` — shared utilities (paths, LLM client, etc.)

Runtime data (``database/``, ``llm_apikey.json``) is resolved via
:func:`a2x_registry.common.paths.get_home` — set the ``A2X_REGISTRY_HOME``
environment variable to pick a location, otherwise the library looks for
these resources in the current working directory, falling back to
``~/.a2x_registry/``.
"""

__version__ = "0.1.6"
