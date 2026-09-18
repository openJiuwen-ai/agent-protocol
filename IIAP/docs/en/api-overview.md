# Public API overview

Last updated: 2026-09-18

## TypeScript package

| Import | Purpose |
|---|---|
| `@openjiuwen/iiap` | Runtime, session, handles, DTOs, policy ports, validators, executor, and errors |
| `@openjiuwen/iiap/decision` | Decision parsing and correlated envelope helpers |
| `@openjiuwen/iiap/http` | Default HTTP decision transport |
| `@openjiuwen/iiap/a2ui-v08` | Supported A2UI v0.8 adapter |
| `@openjiuwen/iiap/react` | Optional React help presenter |
| `@openjiuwen/iiap/testing` | Deterministic manual clock |

No `a2ui-v091` subpath is published in v0.1. Source prototypes are not public
API and must not be imported by path.

The UI protocol boundary is `UIProtocolAdapter`; a Host may implement this port
for another protocol without changing IIAP Core. Host-specific compatibility
code belongs to the Host repository, not the IIAP SDK.

## Python package

The `iiap` root package exports:

- `DecisionService` and `AssistanceService`;
- `ModelAdapter`, `ModelRequest`, and `AgentRouter`;
- `validate_intent_context_packet`;
- privacy, assistance, and decision validators;
- decision-envelope helpers and the v0.8 migration validator.

Application frameworks are not dependencies. Connect the async services to an
existing HTTP, RPC, WebSocket, agent, or model-gateway boundary.

For signatures and ownership details, see the full
[Chinese API reference](../zh/sdk-integration/02-sdk-modules-capabilities-and-interfaces.md).
