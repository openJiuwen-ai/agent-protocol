# Public API overview

Last updated: 2026-09-20

## TypeScript package

| Import | Purpose |
|---|---|
| `@openjiuwen/iiap` | Runtime, session, handles, DTOs, policy ports, validators, executor, and errors |
| `@openjiuwen/iiap/decision` | Decision and correlated envelope parsing |
| `@openjiuwen/iiap/http` | Default HTTP decision transport |
| `@openjiuwen/iiap/a2ui-v08` | Supported A2UI v0.8 adapter |
| `@openjiuwen/iiap/react` | Optional React help presenter |
| `@openjiuwen/iiap/testing` | Deterministic manual clock |

No `a2ui-v091` subpath or source prototype is provided in v0.1. The wire version
remains representable for Hosts that supply a conformance-tested custom adapter.

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
- the public `decision_prompt` used by model integrations.

Application frameworks are not dependencies. Connect the async services to an
existing HTTP, RPC, WebSocket, agent, or model-gateway boundary.

For signatures and ownership details, see the
[Chinese API reference](../zh/api-reference.md).
