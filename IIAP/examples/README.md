# IIAP examples

English | [简体中文](README_zh.md)

These examples show where IIAP (Implicit Intent Aware Protocol) fits in an
A2UI application. They are local integration references, not production
services and not protocol-conformance tests.

## What each example demonstrates

| Example | Purpose | External model/service |
|---|---|---|
| `typescript/complete-v08.ts` | Complete client flow: A2UI v0.8 Adapter, Runtime observation, packet delivery, decision presentation, accepted assistance, and validated update execution | No; uses an in-memory deterministic Transport |
| `typescript/minimal.ts` | Smallest Adapter/Runtime lifecycle: build a plan, activate it, then release resources | No |
| `browser/` | React HelpPresenter rendering plus accepted/dismissed feedback in a browser build | No |
| `python/complete_service.py` | Backend flow through `DecisionService` and `AssistanceService`, including canonical packet validation and response envelopes | No; uses a deterministic ModelAdapter |
| `python/minimal.py` | Minimal function showing how a Host passes its ModelAdapter and packet to `DecisionService` | Host-provided |

The complete TypeScript and Python examples deliberately use deterministic
in-memory adapters. This keeps the examples runnable without credentials while
still exercising the real SDK validation and orchestration code.

## Prerequisites

From the `IIAP/` directory:

```bash
npm ci
npm run build
uv sync --project implementations/python --extra test
```

## Run the complete TypeScript flow

```bash
node implementations/typescript/dist/examples/typescript/complete-v08.js
```

Expected result: the terminal prints an `offer:` line and then an `assistance:`
line. The Presenter auto-accepts the offer so the example can finish without a
UI. Replace `DemoTransport` and `AutoAcceptPresenter` with Host implementations
when integrating a real application.

## Run the complete Python service flow

```bash
PYTHONPATH=implementations/python/src \
  uv run --project implementations/python python examples/python/complete_service.py
```

Expected result: the terminal prints a correlated `iiap.decision` envelope and
an `iiap.assistance.response` envelope. The example reads the canonical packet
fixture and sends no network request.

## Build the browser presenter demo

```bash
npm run demo:build
```

The command builds `examples/browser/` into `build/browser-demo/`. This demo is
for inspecting the default React Presenter and feedback interactions; it does
not include a real A2UI Renderer or model backend.

## Minimal snippets

`typescript/minimal.ts` and `python/minimal.py` are intentionally incomplete
Host-side snippets. They document lifecycle and service-call boundaries and are
not standalone end-to-end demonstrations.

For a production integration, continue with the
[Quickstart](../docs/en/quickstart.md) and review
[compatibility and security](../docs/en/compatibility-and-security.md).
