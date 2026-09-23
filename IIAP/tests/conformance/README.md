# Conformance tests

The executable conformance assets live in `contracts/fixtures/`; this directory
is an index, not a second copy of the suite.

From the `IIAP/` directory, run:

```bash
uv run --with jsonschema python scripts/check_contracts.py
uv run --with jsonschema python scripts/check_contracts.py --packet <captured-packet.json>
npm test
cd implementations/python
PYTHONPATH=src uv run --with jsonschema --with pytest --with pytest-asyncio \
  python -m pytest -q -c pyproject.toml tests
```

`check_contracts.py` validates the canonical valid/invalid wire fixtures. The
TypeScript and Python tests additionally consume shared privacy, assistance,
and safe-suggestion corpora. A captured packet may be either the packet object
itself or an `iiap.intent_context_packet` envelope.

Passing these commands proves schema and cross-language behavior only. It does
not prove A2UI v0.9.1 conformance, custom catalog privacy coverage, or browser
rendering behavior; see `docs/zh/testing-and-compatibility.md`.
