# Testing and release

Last updated: 2026-09-20

Run from `IIAP/`:

```bash
npm test
npm run contracts
npm run docs
npm run packages:smoke
npm run demo:build

cd implementations/python
PYTHONPATH=src uv run --extra test python -m pytest -q -c pyproject.toml tests
```

`packages:smoke` builds and installs the real npm tarball and Python wheel in
clean temporary consumers. It verifies supported npm subpaths, packaged Python
Schema resources, metadata, and installed imports.

Contract fixtures prove wire compatibility. Unit and integration tests prove
runtime and service behavior. They do not prove remote-model content quality,
screen-reader behavior, A2UI v0.9.1 conformance, or every custom UI
catalog.

Release artifacts must be built from a fixed `agent-protocol` commit. The
combined JiuwenSwarm test repository records that commit and the matching
`IIAP/` tree hash so test results cannot silently refer to another SDK version.
