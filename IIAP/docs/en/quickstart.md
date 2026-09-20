# Quickstart

Last updated: 2026-09-20

This page validates the core IIAP (Implicit Intent Aware Protocol) flow with a
deterministic local example: A2UI surface → semantic event → IntentContextPacket
→ decision → user feedback. It does not call an external model or service.

## 1. Prepare the environment

Requirements: Node.js 20+ and Python 3.11+. From `IIAP/`:

```bash
npm ci
npm run build
uv sync --project implementations/python --extra test
```

This release candidate is not published to public registries. Pin a source
commit and use the npm tarball and Python wheel verified by
`npm run packages:smoke` for production integration.

## 2. Run the complete local example

```bash
node implementations/typescript/dist/examples/typescript/complete-v08.js
```

The example converts an A2UI v0.8 surface into an observation plan, observes a
privacy-safe local event, emits a packet, receives an in-memory
`text_assistance` decision, and simulates accepted assistance. The terminal
should print `offer:` followed by `assistance:`.

This proves that the SDK flow is runnable; it does not prove integration with a
real Renderer, network Transport, or model.

## 3. Convert A2UI into an ObservationPlan

```ts
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';

const adapter = new A2UIV08Adapter({
  resolveAllowedValues: ({ declaredValues }) => declaredValues,
});
const plan = adapter.buildObservationPlans({
  sessionId: 'session-1',
  messageId: 'message-1',
  namespace: 'message-1',
  messages: finalA2UIV08Messages,
})[0];
```

Pass only final A2UI messages that have been validated, repaired, and accepted
by the Renderer. Without a Host `SuggestionPolicy`, the Adapter does not
authorize UI updates.

## 4. Choose one packet-delivery mode

Use a Host-managed channel when the application already owns WebSocket, RPC,
or message-bus delivery:

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';

const runtime = createIIAPRuntime({
  onPacket: async (packet) => sendThroughExistingChannel(packet),
  presenter: hostPresenter,
  onAccept: applyAcceptedHelp,
});
```

Or use the SDK-managed HTTP Transport:

```ts
import { createIIAPRuntime } from '@openjiuwen/iiap';
import { HTTPDecisionTransport } from '@openjiuwen/iiap/http';

const runtime = createIIAPRuntime({
  transport: new HTTPDecisionTransport({ baseUrl: '/api/iiap' }),
  presenter,
  feedbackUpload: true,
  onAccept: applyAcceptedHelp,
});
```

`transport` and `onPacket` are mutually exclusive. Feedback upload is
controlled only by the Runtime `feedbackUpload` option.

## 5. Activate and observe

```ts
if (!plan) throw new Error('No observable surface');
const session = runtime.createSession({ sessionId: plan.sessionId });
const handle = session.activate(plan, { focused: true });

handle.observe({
  messageId: plan.messageId,
  surfaceInstanceId: plan.surface.surfaceInstanceId,
  componentId: 'choice',
  eventType: 'change',
  valueToken: 'temporary-token',
});
```

Every event must carry its complete owner. Tokens express equality only within
one component and surface instance; they must not contain raw values or be
reused across components.

Suspend observation before a real business action, deactivate replaced or
removed surfaces, and dispose the Runtime when the application unmounts.

## 6. Apply accepted updates safely

```ts
import { ValidatedSuggestionExecutor } from '@openjiuwen/iiap';

const executor = new ValidatedSuggestionExecutor((updates) => {
  applyDataModelBatchAtomically(updates);
});
```

After the user accepts an `update_suggestion`, construct `SuggestionContext`
from the original packet's `allowedOperations.updateTargets`. Reject the entire
batch when any target, value, owner, or selection cardinality is invalid.

## 7. Connect the Python decision service

```python
from iiap import DecisionService

service = DecisionService(model_adapter)
envelope = await service.decide(packet)
```

`DecisionService` validates the complete packet against the packaged canonical
Schema before constructing a model request. Malformed packets raise
`ValueError("INVALID_PACKET")` and never reach the model.

The HTTP or RPC handler remains responsible for authentication, request-size
limits, infrastructure timeouts, tracing, and transport-specific error mapping.

## 8. Next steps

- [Examples](../../examples/README.md)
- [Public API](api-overview.md)
- [Compatibility and security](compatibility-and-security.md)
- [Testing and release](testing-and-release.md)
