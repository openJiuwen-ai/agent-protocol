"""Registered (lite-safe import) contract test.

The register module is on the SDK hot path. It must import in lite
mode without dragging in numpy / chromadb / sentence_transformers via
its dependencies on validation, agent_card fetch, etc.
"""

from __future__ import annotations

import importlib


def test_register_service_lite_safe(lite_env):
    """register.service must import: lite SDK uses _ensure_dataset_initialized."""
    importlib.import_module("a2x_registry.register.service")
