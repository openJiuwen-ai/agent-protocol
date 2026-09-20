# IIAP SDK

English | [简体中文](README_zh.md)

## Overview

IIAP (Implicit Intent Aware Protocol) derives privacy-safe semantic events from
local A2UI component interactions, aggregates them into intent context, lets a
backend decide whether to offer optional non-blocking help, and correlates user
feedback with the original context.

This self-contained SDK can be moved directly to `agent-protocol/IIAP`. The
current version is `0.1.0-rc.1` with built-in A2UI v0.8 support. The npm and
PyPI artifacts are not yet published to public registries.

## Features

- **Local observation**: converts A2UI v0.8 definitions into observation plans
  and captures privacy-safe interaction metadata.
- **Privacy by construction**: excludes raw input values, stable value hashes,
  and the complete data model from packets.
- **Non-blocking assistance**: supports text help and consent-gated update
  suggestions without submitting forms or invoking business actions.
- **Safe updates**: revalidates accepted updates against the original packet's
  `surface/path/value` allowlist and requires atomic batch application.
- **Host or SDK transport**: uses either a Host-managed channel or an SDK
  Transport per Runtime instance.
- **Cross-language integration**: provides TypeScript Runtime/Adapter/Presenter
  modules and Python Decision/Assistance services.

## Quickstart

Requirements: Node.js 20+ and Python 3.11+. From `IIAP/`:

```bash
npm ci
npm run build
uv sync --project implementations/python --extra test
node implementations/typescript/dist/examples/typescript/complete-v08.js
```

The example builds an A2UI v0.8 surface, observes a local interaction, emits a
packet, receives an in-memory decision, and simulates accepted assistance. See
the [Quickstart](docs/en/quickstart.md) for integration steps.

## Modules

| Module | Purpose |
|---|---|
| `@openjiuwen/iiap` | Runtime, public DTOs, privacy and suggestion validation |
| `@openjiuwen/iiap/a2ui-v08` | A2UI v0.8 Adapter |
| `@openjiuwen/iiap/decision` | Model decision and server-envelope parsing |
| `@openjiuwen/iiap/http` | Optional HTTP Transport |
| `@openjiuwen/iiap/react` | Optional React Presenter |
| `openjiuwen-iiap` | Python prompts, validators, and reference services |

## Examples

- `examples/typescript/complete-v08.ts`: complete observation, decision,
  presentation, assistance, and safe-update flow.
- `examples/browser/`: browser rendering and accepted/dismissed feedback with
  the React HelpPresenter.
- `examples/python/complete_service.py`: backend DecisionService and
  AssistanceService flow.

See [examples/README.md](examples/README.md) for purpose, prerequisites,
commands, and expected results.

## Tests

```bash
npm test
uv run --project implementations/python pytest implementations/python/tests -q
uv run --with jsonschema python scripts/check_contracts.py
uv run python scripts/check_docs.py
npm run packages:smoke
```

## Documentation

- [Documentation portal](docs/en/README.md)
- [Quickstart](docs/en/quickstart.md)
- [Public API](docs/en/api-overview.md)
- [Compatibility and security](docs/en/compatibility-and-security.md)
- [Testing and release](docs/en/testing-and-release.md)
- [Third-party dependency inventory](THIRD_PARTY_DEPENDENCIES.md)

## License

Licensed under Apache-2.0.
