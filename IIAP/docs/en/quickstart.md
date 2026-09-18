# Quickstart

Last updated: 2026-09-18

## Requirements

- Node.js 20 or newer for the TypeScript package build and tests.
- Python 3.11 or newer for the model-side services.
- A2UI v0.8 messages for the built-in UI adapter.

From the `IIAP/` source directory:

```bash
npm ci
npm run build

cd implementations/python
uv sync --extra test
```

The release candidate is not published to a public registry yet. Use the npm
tarball and Python wheel produced by `npm run packages:smoke`, or build from a
fixed source commit.

## Observe an A2UI v0.8 surface

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';

const runtime = createIIAPRuntime();
const session = runtime.createSession({ sessionId: 'session-1' });
const plan = new A2UIV08Adapter().buildObservationPlans({
  sessionId: 'session-1',
  messageId: 'message-1',
  namespace: 'message-1',
  messages: [
    { surfaceUpdate: { surfaceId: 'main', components: [
      { id: 'choice', component: { CheckBox: { value: { path: '/choice' } } } },
    ] } },
    { beginRendering: { surfaceId: 'main', root: 'choice' } },
  ],
})[0];

if (!plan) throw new Error('No observable surface');
const handle = session.activate(plan);
// Renderer integrations pass privacy-safe ComponentEvent metadata to handle.observe(...).

handle.deactivate('host-request');
runtime.dispose();
```

Use the complete v0.8 example in `examples/typescript/complete-v08.ts` to wire
a transport, presenter, assistance flow, and consent-gated updates.

## Connect a Python decision service

```python
from iiap import DecisionService

service = DecisionService(model_adapter)
envelope = await service.decide(packet)
```

`DecisionService` validates the complete packet against the packaged canonical
Schema before it constructs a prompt. Malformed packets raise
`ValueError("INVALID_PACKET")` and never reach the model.

The API or RPC handler remains responsible for authentication, request-size
limits, infrastructure timeouts, tracing, and mapping the stable validation
error to its transport-specific response.
