# IIAP SDK

IIAP (Intelligent Interaction Assistance Protocol) detects privacy-safe UI
interaction patterns and offers optional, non-blocking assistance. This source
tree is self-contained and is designed to be moved to `agent-protocol/IIAP`.

Version: `0.1.0-rc.1`

## Distribution and modules

- TypeScript npm package: `@openjiuwen/iiap`, built from `implementations/typescript/packages/core`
- TypeScript subpath modules: `/decision`, `/http`, `/a2ui-v08`, `/react`, and `/testing`
- Python services: `implementations/python`
- Source modules: the supported A2UI v0.8 adapter and unpublished v0.9.1 experiments in `adapters/`, HTTP transport in `transports/http`, and the optional React presenter in `presenters/react`

The SDK never submits a form or applies a suggestion without an explicit host
action. Feedback upload is optional and disabled by default.

Before distributing local artifacts, run `npm run packages:smoke`. The command
builds the TypeScript SDK, packs it, installs the tarball in a fresh temporary
project, builds the Python wheel, installs it in a fresh virtual environment,
and imports both installed packages.

Start with the [English Quickstart](docs/en/quickstart.md) or the
[English documentation portal](docs/en/README.md). Detailed architecture and
integration references are available from the [Chinese documentation portal](docs/zh/README.md).
The release dependency inventory is in [THIRD_PARTY_DEPENDENCIES.md](THIRD_PARTY_DEPENDENCIES.md).
