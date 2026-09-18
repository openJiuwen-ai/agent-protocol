# Integration tests

Integration checks intentionally run from their owning source trees rather than
being duplicated here.

From the repository root, run:

```bash
uv run pytest -q tests/unit_tests/iiap tests/unit_tests/a2ui \
  tests/system_tests/test_a2ui_system_flow.py

cd jiuwenswarm/channels/web/frontend
npm run test:iiap
npx tsc --noEmit
npm run build
npm run test:iiap:chain
node --experimental-strip-types tests/e2e/iiap-scenarios.mjs

cd ../../../../IIAP
npm run packages:smoke
```

These commands cover the JiuwenSwarm Host bridge, A2UI backend boundary,
frontend focused tests and production build, credential-free packet/decision/
assistance flow, scenario sequencing, and clean-install npm/wheel artifacts.

They do not drive a real browser DOM, remote model, persisted WebSocket
session, keyboard/screen reader, or official A2UI v0.9.1 conformance. The
manual SR/AR acceptance procedure and current gaps are recorded in
`../../../docs/zh/IIAP-feature-test-guide.md`.
