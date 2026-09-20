import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import test from 'node:test';

import { A2UIV08Adapter } from '../../../adapters/a2ui-v0.8/src/index.js';
import { ValidatedSuggestionExecutor, coerceModelObject, createIIAPRuntime, validateAssistanceText, validateDataModelSuggestion, validatePrivacy, type AssistanceRequest, type ComponentEvent, type IntentContextPacket } from '../packages/core/src/index.js';
import { describeModelOutput } from '../packages/core/src/model-output.js';
import { HTTPDecisionTransport } from '../../../transports/http/src/index.js';
import { createManualClock, systemClock } from '../packages/core/src/clock.js';
import { FeedbackController } from '../packages/core/src/feedback.js';
import { resolvePolicy } from '../packages/core/src/policy.js';
import { parseDecision } from '../packages/decision-client/src/index.js';

const v08Messages = [
  { beginRendering: { surfaceId: 'booking' } },
  { surfaceUpdate: { surfaceId: 'booking', components: [
    { id: 'date', component: { DateTimeInput: { value: { path: '/date' } } } },
    { id: 'choice', component: { MultipleChoice: { selections: { path: '/choice' }, options: [{ value: 'A' }, { value: 'B' }] } } },
    { id: 'submit', component: { Button: { action: { name: 'submit', context: [{ key: 'secret', value: { literalString: 'private' } }] } } } },
  ] } },
  { dataModelUpdate: { surfaceId: 'booking', path: '/', contents: [{ key: 'date', valueString: 'private-date' }] } },
];

function firstPlan(adapter: A2UIV08Adapter, input: Parameters<A2UIV08Adapter['buildObservationPlans']>[0]) {
  const plan = adapter.buildObservationPlans(input)[0];
  assert.ok(plan);
  return plan;
}

function owner(plan: { messageId: string; surface: { surfaceInstanceId: string } }) {
  return { messageId: plan.messageId, surfaceInstanceId: plan.surface.surfaceInstanceId };
}

function noIntervention(packet: { packetId: string; surfaceInstanceId: string }) {
  return {
    type: 'iiap.decision' as const, iiapVersion: '0.1' as const,
    decisionId: `decision-${packet.packetId}`, packetId: packet.packetId,
    surfaceInstanceId: packet.surfaceInstanceId,
    payload: { decision: 'no_intervention' as const, reason: 'test', offerType: 'none' as const, uiStyle: 'none' as const, message: '' },
  };
}

test('assistance text validator matches the shared corpus', () => {
  const corpus = JSON.parse(readFileSync(resolve(process.cwd(), 'contracts/fixtures/assistance-text-validation.corpus.json'), 'utf8')) as Array<{
    name: string; message: string; valid: boolean; reason?: string;
  }>;
  for (const entry of corpus) {
    assert.deepEqual(validateAssistanceText(entry.message), {
      valid: entry.valid,
      ...(entry.reason ? { reason: entry.reason } : {}),
    }, entry.name);
  }
});

test('HTTP assistance rejects A2UI output and mismatched request correlation', async () => {
  const request = {
    type: 'iiap.assistance.request' as const, iiapVersion: '0.1' as const,
    requestId: 'req-1', packetId: 'packet-1', decisionId: 'decision-1',
    surfaceInstanceId: 'surface-1', topic: 'compare_options' as const, language: 'zh-CN',
  };
  const response = (body: object) => async () => new Response(JSON.stringify(body), {
    status: 200, headers: { 'content-type': 'application/json' },
  });
  await assert.rejects(new HTTPDecisionTransport({
    baseUrl: 'https://example.invalid',
    fetch: response({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: 'req-1', message: '{"createSurface":{}}' }),
  }).assist(request), { code: 'ASSISTANCE_UNSAFE_OUTPUT' });
  await assert.rejects(new HTTPDecisionTransport({
    baseUrl: 'https://example.invalid',
    fetch: response({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: 'other', message: 'help' }),
  }).assist(request), /INVALID_ASSISTANCE_RESPONSE/);
});

test('HTTP transport exposes stable errors for invalid decisions, status failures, and timeouts', async () => {
  const packet = {
    packetId: 'packet-http-errors', sessionId: 'session-http-errors', surfaceInstanceId: 'surface-http-errors',
  } as Parameters<HTTPDecisionTransport['decide']>[0];
  const response = (body: object, status = 200) => async () => new Response(JSON.stringify(body), {
    status, headers: { 'content-type': 'application/json' },
  });
  await assert.rejects(new HTTPDecisionTransport({
    baseUrl: 'https://example.invalid', fetch: response({ invalid: true }),
  }).decide(packet), { code: 'INVALID_DECISION', retryable: false });
  await assert.rejects(new HTTPDecisionTransport({
    baseUrl: 'https://example.invalid', fetch: response({}, 503),
  }).decide(packet), { code: 'TRANSPORT_ERROR', retryable: true });
  await assert.rejects(new HTTPDecisionTransport({
    baseUrl: 'https://example.invalid', timeoutMs: 1,
    fetch: async (_input, init) => await new Promise<Response>((_resolve, reject) => {
      init?.signal?.addEventListener('abort', () => reject(new DOMException('aborted', 'AbortError')), { once: true });
    }),
  }).decide(packet), { code: 'MODEL_TIMEOUT', retryable: true });
});

test('HTTP transport sends feedback when the runtime invokes it', async () => {
  const requests: Array<{ url: string; body: unknown }> = [];
  const transport = new HTTPDecisionTransport({
    baseUrl: 'https://example.invalid/',
    fetch: async (input, init) => {
      requests.push({ url: String(input), body: JSON.parse(String(init?.body)) });
      return new Response('{}', { status: 200, headers: { 'content-type': 'application/json' } });
    },
  });
  await transport.sendFeedback({
    type: 'iiap.feedback', iiapVersion: '0.1', packetId: 'packet-1', decisionId: 'decision-1',
    surfaceInstanceId: 'surface-1', offerType: 'text_assistance', interaction: 'dismissed',
    timestamp: new Date(0).toISOString(),
  });
  assert.deepEqual(requests, [{
    url: 'https://example.invalid/feedback',
    body: {
      type: 'iiap.feedback', iiapVersion: '0.1', packetId: 'packet-1', decisionId: 'decision-1',
      surfaceInstanceId: 'surface-1', offerType: 'text_assistance', interaction: 'dismissed',
      timestamp: new Date(0).toISOString(),
    },
  }]);
});

test('v0.8 adapter builds an equivalent observation plan', () => {
  const plan = firstPlan(new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  assert.equal(plan.protocolVersion, '0.8');
  assert.equal(plan.surface.surfaceInstanceId, 'm:booking');
  assert.equal(plan.components.find((item) => item.componentId === 'date')?.capability, 'temporal_edit');
  assert.equal(plan.components.find((item) => item.componentId === 'choice')?.selectionMode, 'single');
  assert.equal(plan.explicitActionBoundaries[0]?.actionName, 'submit');
  const definition = JSON.stringify(plan.surfaceContext.definition);
  assert.equal(definition.includes('dataModelUpdate'), false);
  assert.equal(definition.includes('private-date'), false);
  assert.equal(definition.includes('private'), false);
  assert.equal(definition.includes('"name":"submit"'), true);
  assert.equal(plan.surfaceContext.redaction.unknownCustomPropertiesExcluded, false);
});

test('adapter creates one isolated observation plan per surface', () => {
  const plans = new A2UIV08Adapter().buildObservationPlans({
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'left' } },
      { surfaceUpdate: { surfaceId: 'left', components: [
        { id: 'left-date', component: { DateTimeInput: { value: { path: '/left' } } } },
      ] } },
      { beginRendering: { surfaceId: 'right' } },
      { surfaceUpdate: { surfaceId: 'right', components: [
        { id: 'right-choice', component: { MultipleChoice: { selections: { path: '/right' }, options: [{ value: 'A' }] } } },
        { id: 'right-submit', component: { Button: { action: { name: 'submit-right' } } } },
      ] } },
    ],
  });
  assert.deepEqual(plans.map((plan) => plan.surface.originalSurfaceId), ['left', 'right']);
  assert.deepEqual(plans[0]?.components.map((item) => item.componentId), ['left-date']);
  assert.deepEqual(plans[1]?.components.map((item) => item.componentId), ['right-choice', 'right-submit']);
  assert.deepEqual(plans[0]?.explicitActionBoundaries, []);
  assert.equal(plans[1]?.explicitActionBoundaries[0]?.actionName, 'submit-right');
});

test('adapter snapshot and observation plan exclude unreachable and deleted surface state', () => {
  const adapter = new A2UIV08Adapter();
  const plans = adapter.buildObservationPlans({
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'active', root: 'root' } },
      { surfaceUpdate: { surfaceId: 'active', components: [
        { id: 'root', component: { Column: { children: { explicitList: ['current'] } } } },
        { id: 'current', component: { DateTimeInput: { value: { path: '/current' } } } },
        { id: 'stale', component: { DateTimeInput: { value: { path: '/stale' } } } },
      ] } },
      { beginRendering: { surfaceId: 'deleted' } },
      { surfaceUpdate: { surfaceId: 'deleted', components: [{ id: 'gone', component: { DateTimeInput: {} } }] } },
      { deleteSurface: { surfaceId: 'deleted' } },
    ],
  });
  assert.deepEqual(plans.map((plan) => plan.surface.originalSurfaceId), ['active']);
  assert.deepEqual(plans[0]?.components.map((component) => component.componentId), ['current']);
  assert.equal(JSON.stringify(plans[0]?.surfaceContext.definition).includes('stale'), false);
});

test('adapter delete then recreate with the same surface id clears prior state', () => {
  const plans = new A2UIV08Adapter().buildObservationPlans({
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'form', root: 'old-root' } },
      { surfaceUpdate: { surfaceId: 'form', components: [
        { id: 'old-root', component: { Column: { children: { explicitList: ['old-field'] } } } },
        { id: 'old-field', component: { DateTimeInput: { value: { path: '/old' } } } },
      ] } },
      { deleteSurface: { surfaceId: 'form' } },
      { beginRendering: { surfaceId: 'form', root: 'new-root' } },
      { surfaceUpdate: { surfaceId: 'form', components: [
        { id: 'new-root', component: { Column: { children: { explicitList: ['new-field'] } } } },
        { id: 'new-field', component: { DateTimeInput: { value: { path: '/new' } } } },
      ] } },
    ],
  });
  assert.deepEqual(plans[0]?.components.map((component) => component.componentId), ['new-field']);
  const definition = JSON.stringify(plans[0]?.surfaceContext.definition);
  assert.equal(definition.includes('old-field'), false);
  assert.equal(definition.includes('new-field'), true);
});

test('core rejects missing or mismatched event owners before focus and window mutation', async () => {
  const rejected: string[] = [];
  const runtime = createIIAPRuntime({
    onEventRejected: (reason) => rejected.push(reason),
    policy: {
      componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0,
      maxReportsPerComponent: 3, thresholds: { temporalChangeCount: 3 },
    },
    transport: {
      decide: async (packet) => noIntervention(packet),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
  });
  const plans = new A2UIV08Adapter().buildObservationPlans({
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { surfaceUpdate: { surfaceId: 'left', components: [{ id: 'shared', component: { DateTimeInput: {} } }] } },
      { surfaceUpdate: { surfaceId: 'right', components: [{ id: 'shared', component: { DateTimeInput: {} } }] } },
    ],
  });
  const session = runtime.createSession({ sessionId: 's' });
  const left = session.activate(plans[0]!);
  const right = session.activate(plans[1]!, { focused: false });
  left.observe({ ...owner(plans[0]!), componentId: 'shared', eventType: 'change', valueToken: 'A' });
  left.observe({ ...owner(plans[0]!), componentId: 'shared', eventType: 'change', valueToken: 'B' });

  right.observe({ ...owner(plans[0]!), componentId: 'shared', eventType: 'change', valueToken: 'wrong-handle' });
  left.observe({ messageId: 'other', surfaceInstanceId: plans[0]!.surface.surfaceInstanceId, componentId: 'shared', eventType: 'change', valueToken: 'wrong-message' });
  left.observe({ ...owner(plans[0]!), componentId: 'missing', eventType: 'change', valueToken: 'unknown-component' });
  left.observe({ componentId: 'shared', eventType: 'change', valueToken: 'missing-owner' } as ComponentEvent);

  left.observe({ ...owner(plans[0]!), componentId: 'shared', eventType: 'change', valueToken: 'A' });
  const packet = await left.flush();
  assert.equal(packet?.surfaceInstanceId, plans[0]!.surface.surfaceInstanceId);
  assert.equal(packet?.patterns[0]?.metrics.changeCount, 3, 'rejected events do not clear or mutate the valid window');
  assert.equal(packet?.observations.events.length, 3, 'the packet preserves the sanitized source events');
  assert.deepEqual(rejected, ['owner_mismatch', 'owner_mismatch', 'unknown_component', 'missing_owner']);
  runtime.dispose();
});

test('one session keeps multiple surface handles isolated while interaction transfers focus', async () => {
  const runtime = createIIAPRuntime({
    policy: {
      componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0,
      maxReportsPerComponent: 3, thresholds: { temporalChangeCount: 1, optionChangeCount: 1 },
    },
    transport: {
      decide: async (packet) => noIntervention(packet),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
  });
  const plans = new A2UIV08Adapter().buildObservationPlans({
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { surfaceUpdate: { surfaceId: 'left', components: [{ id: 'left', component: { DateTimeInput: {} } }] } },
      { surfaceUpdate: { surfaceId: 'right', components: [{ id: 'right', component: { MultipleChoice: {} } }] } },
    ],
  });
  const session = runtime.createSession({ sessionId: 's' });
  const left = session.activate(plans[0]!);
  const right = session.activate(plans[1]!);
  right.observe({ ...owner(plans[1]!), componentId: 'right', eventType: 'change', valueToken: 'R' });
  const rightPacket = await right.flush();
  assert.equal(rightPacket?.surfaceInstanceId, 'm:right');
  assert.equal(rightPacket?.patterns[0]?.componentId, 'right');

  left.observe({ ...owner(plans[0]!), componentId: 'left', eventType: 'change', valueToken: 'L' });
  const leftPacket = await left.flush();
  assert.equal(leftPacket?.surfaceInstanceId, 'm:left');
  assert.equal(leftPacket?.patterns[0]?.componentId, 'left');

  left.deactivate('host-request');
  right.observe({ ...owner(plans[1]!), componentId: 'right', eventType: 'change', valueToken: 'R2' });
  assert.equal((await right.flush())?.surfaceInstanceId, 'm:right');
  runtime.dispose();
});

test('only the focused surface has automatic timers and interaction transfers focus', async () => {
  const clock = createManualClock(0);
  const decided: string[] = [];
  const runtime = createIIAPRuntime({
    clock,
    policy: {
      componentQuietMs: 1000, surfaceIdleMs: 10_000, minReportIntervalMs: 0,
      maxReportsPerComponent: 3, thresholds: { temporalChangeCount: 3 },
    },
    transport: {
      decide: async (packet) => {
        decided.push(packet.surfaceInstanceId);
        return noIntervention(packet);
      },
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
  });
  const plans = new A2UIV08Adapter().buildObservationPlans({
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { surfaceUpdate: { surfaceId: 'old', components: [{ id: 'old-date', component: { DateTimeInput: {} } }] } },
      { surfaceUpdate: { surfaceId: 'new', components: [{ id: 'new-date', component: { DateTimeInput: {} } }] } },
    ],
  });
  const session = runtime.createSession({ sessionId: 's' });
  const oldSurface = session.activate(plans[0]!);
  oldSurface.observe({ ...owner(plans[0]!), componentId: 'old-date', eventType: 'change', valueToken: 'A' });
  oldSurface.observe({ ...owner(plans[0]!), componentId: 'old-date', eventType: 'change', valueToken: 'B' });
  oldSurface.observe({ ...owner(plans[0]!), componentId: 'old-date', eventType: 'change', valueToken: 'A' });
  session.activate(plans[1]!);
  clock.advanceBy(1000);
  await Promise.resolve();
  assert.deepEqual(decided, [], 'demoted surface timer and pending window are discarded');

  oldSurface.observe({ ...owner(plans[0]!), componentId: 'old-date', eventType: 'change', valueToken: 'A' });
  oldSurface.observe({ ...owner(plans[0]!), componentId: 'old-date', eventType: 'change', valueToken: 'B' });
  oldSurface.observe({ ...owner(plans[0]!), componentId: 'old-date', eventType: 'change', valueToken: 'A' });
  clock.advanceBy(1000);
  await Promise.resolve();
  assert.deepEqual(decided, ['m:old'], 'direct interaction transfers the automatic timer lease');
  runtime.dispose();
});

test('decision envelopes reject stale, mismatched, unknown, and duplicate responses', async () => {
  let packetNumber = 0;
  const rejected: string[] = [];
  const runtime = createIIAPRuntime({
    idFactory: () => `packet-${++packetNumber}`,
    onDecisionRejected: (reason) => rejected.push(reason),
    policy: {
      componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0,
      maxReportsPerSurface: 5, maxReportsPerComponent: 5, thresholds: { temporalChangeCount: 1 },
    },
  });
  const plans = new A2UIV08Adapter().buildObservationPlans({
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { surfaceUpdate: { surfaceId: 'left', components: [{ id: 'left', component: { DateTimeInput: {} } }] } },
      { surfaceUpdate: { surfaceId: 'right', components: [{ id: 'right', component: { DateTimeInput: {} } }] } },
    ],
  });
  const session = runtime.createSession({ sessionId: 's' });
  const left = session.activate(plans[0]!);
  const right = session.activate(plans[1]!, { focused: false });

  left.observe({ ...owner(plans[0]!), componentId: 'left', eventType: 'change', valueToken: 'A' });
  const packetA = await left.flush();
  assert.ok(packetA);
  right.observe({ ...owner(plans[1]!), componentId: 'right', eventType: 'change', valueToken: 'B' });
  const packetB = await right.flush();
  assert.ok(packetB);

  assert.equal(session.handleDecision({ type: 'invalid' } as never), false);
  const decisionB = noIntervention(packetB);
  assert.equal(session.handleDecision(decisionB), true);
  assert.equal(session.handleDecision(decisionB), false, 'the same decisionId is idempotent');
  assert.equal(session.handleDecision(noIntervention(packetA)), false, 'focus transfer makes the prior packet stale');
  assert.equal(session.handleDecision({ ...noIntervention(packetB), decisionId: 'unknown', packetId: 'missing' }), false);

  left.observe({ ...owner(plans[0]!), componentId: 'left', eventType: 'change', valueToken: 'C' });
  const packetC = await left.flush();
  assert.ok(packetC);
  const decisionC = noIntervention(packetC);
  assert.equal(session.handleDecision({ ...decisionC, surfaceInstanceId: 'wrong:surface' }), false);
  assert.equal(session.handleDecision({
    ...decisionC,
    payload: { ...decisionC.payload, helpTopic: 'explain_rules' },
  } as never), false, 'no-intervention cannot carry assistance state');
  assert.equal(session.handleDecision({
    ...decisionC,
    payload: { decision: 'offer_help', reason: 'test', offerType: 'text_assistance', uiStyle: 'inline_card', message: 'help' },
  } as never), false, 'text assistance requires a topic');
  assert.equal(session.handleDecision({
    ...decisionC,
    payload: { decision: 'offer_help', reason: 'test', offerType: 'update_suggestion', uiStyle: 'inline_card', message: 'help' },
  } as never), false, 'update assistance requires a suggestion');
  assert.equal(session.handleDecision(decisionC), true, 'owner mismatch does not consume the valid pending request');
  assert.deepEqual(rejected, [
    'invalid_envelope', 'duplicate_decision', 'stale', 'unknown_packet', 'owner_mismatch',
    'invalid_envelope', 'invalid_envelope', 'invalid_envelope',
  ]);
  runtime.dispose();
});

test('a newer terminal decision invalidates an older offer on the same surface', async () => {
  let packetNumber = 0;
  let resolveOffer: ((interaction: 'accepted') => void) | undefined;
  let accepted = 0;
  let dismissed = 0;
  const runtime = createIIAPRuntime({
    idFactory: () => `packet-${++packetNumber}`,
    policy: {
      componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0,
      maxReportsPerSurface: 3, maxReportsPerComponent: 3, thresholds: { temporalChangeCount: 1 },
    },
    presenter: {
      present: () => new Promise((resolve) => { resolveOffer = resolve as (interaction: 'accepted') => void; }),
      dismiss: () => { dismissed += 1; },
    },
    onAccept: () => { accepted += 1; },
  });
  const plan = firstPlan(new A2UIV08Adapter(), {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { surfaceUpdate: { surfaceId: 'booking', components: [{ id: 'date', component: { DateTimeInput: {} } }] } },
    ],
  });
  const session = runtime.createSession({ sessionId: 's' });
  const handle = session.activate(plan);
  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'A' });
  const positivePacket = await handle.flush();
  assert.ok(positivePacket);
  assert.equal(session.handleDecision({
    type: 'iiap.decision', iiapVersion: '0.1', decisionId: 'positive',
    packetId: positivePacket.packetId, surfaceInstanceId: positivePacket.surfaceInstanceId,
    payload: { decision: 'offer_help', reason: 'help', offerType: 'text_assistance', helpTopic: 'explain_rules', uiStyle: 'inline_card', message: 'help' },
  }), true);

  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'B' });
  const negativePacket = await handle.flush();
  assert.ok(negativePacket);
  assert.equal(session.handleDecision(noIntervention(negativePacket)), true);
  assert.equal(dismissed, 1, 'the terminal decision dismisses the older offer');
  resolveOffer?.('accepted');
  await new Promise<void>((resolve) => setImmediate(resolve));
  assert.equal(accepted, 0, 'a dismissed stale offer cannot execute after a terminal decision');
  runtime.dispose();
});

test('host-managed async transport uses onPacket and cancelDecision without a second state machine', async () => {
  const emitted: string[] = [];
  const runtime = createIIAPRuntime({
    onPacket: (packet) => { emitted.push(packet.packetId); },
    idFactory: () => `packet-${emitted.length + 1}`,
    policy: {
      componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0,
      maxReportsPerSurface: 3, maxReportsPerComponent: 3,
      thresholds: { temporalChangeCount: 1 },
    },
  });
  const plan = firstPlan(new A2UIV08Adapter(), {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { surfaceUpdate: { surfaceId: 'form', components: [{ id: 'date', component: { DateTimeInput: {} } }] } },
    ],
  });
  const session = runtime.createSession({ sessionId: 's' });
  const handle = session.activate(plan);
  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'A' });
  const first = await handle.flush();
  assert.ok(first);
  assert.deepEqual(emitted, [first.packetId]);
  assert.equal(session.cancelDecision(first.packetId), true);
  assert.equal(session.cancelDecision(first.packetId), false);

  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'B' });
  const second = await handle.flush();
  assert.ok(second, 'cancelled host request releases the surface for another packet');
  assert.equal(session.handleDecision(noIntervention(second)), true);
  runtime.dispose();
});

test('runtime rejects simultaneous host-managed and SDK-managed delivery', () => {
  assert.throws(() => createIIAPRuntime({
    onPacket: () => undefined,
    transport: {
      decide: async (packet: IntentContextPacket) => noIntervention(packet),
      assist: async (request: AssistanceRequest) => ({
        type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help',
      }),
    },
  } as never), /mutually exclusive/);
});

test('replacing the same surface preserves completed history and ignores stale handle deactivation', async () => {
  let packetNumber = 0;
  const runtime = createIIAPRuntime({
    idFactory: () => `packet-${++packetNumber}`,
    policy: {
      componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0,
      maxReportsPerSurface: 5, maxReportsPerComponent: 5, thresholds: { temporalChangeCount: 1 },
    },
    transport: {
      decide: async (packet) => noIntervention(packet),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
  });
  const plan = firstPlan(new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  const session = runtime.createSession({ sessionId: 's' });
  const oldHandle = session.activate(plan);
  oldHandle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'first' });
  assert.deepEqual((await oldHandle.flush())?.reportHistory, []);

  const newHandle = session.activate(plan);
  oldHandle.deactivate('surface-replaced');
  newHandle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'second' });
  const packet = await newHandle.flush();
  assert.equal(packet?.packetId, 'packet-2');
  assert.deepEqual(packet?.reportHistory.map((item) => item.packetId), ['packet-1']);
  runtime.dispose();
});

test('each packet contains the current report plus at most three previous reports', async () => {
  let packetNumber = 0;
  const runtime = createIIAPRuntime({
    idFactory: () => `packet-${++packetNumber}`,
    policy: {
      componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0,
      maxReportsPerSurface: 5, maxReportsPerComponent: 5, reportHistoryLimit: 999,
      thresholds: { temporalChangeCount: 1 },
    },
    transport: {
      decide: async (packet) => noIntervention(packet),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
  });
  const historyPlan = firstPlan(
    new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages },
  );
  const handle = runtime.createSession({ sessionId: 's' }).activate(historyPlan);
  const packets = [];
  for (let index = 1; index <= 5; index += 1) {
    handle.observe({ ...owner(historyPlan), componentId: 'date', eventType: 'change', valueToken: `value-${index}` });
    packets.push(await handle.flush());
  }
  assert.deepEqual(packets.map((packet) => packet?.reportHistory.length), [0, 1, 2, 3, 3]);
  assert.deepEqual(packets[4]?.reportHistory.map((item) => item.packetId), ['packet-2', 'packet-3', 'packet-4']);
  assert.equal(packets[4]?.packetId, 'packet-5', 'the current report remains the packet body');
  runtime.dispose();
});

test('multiple selection only reports repeated reversals, not normal additions', async () => {
  const messages = [
    { beginRendering: { surfaceId: 'preferences' } },
    { surfaceUpdate: { surfaceId: 'preferences', components: [{
      id: 'cuisines',
      component: { MultipleChoice: {
        variant: 'checkbox',
        maxAllowedSelections: 4,
        selections: { path: '/cuisines' },
        options: [{ value: 'A' }, { value: 'B' }, { value: 'C' }],
      } },
    }] } },
  ];
  const plan = firstPlan(new A2UIV08Adapter(), {
    sessionId: 's', messageId: 'm', namespace: 'm', messages,
  });
  assert.equal(plan.components[0]?.selectionMode, 'multiple');
  const runtime = createIIAPRuntime({
    policy: { componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0, thresholds: { optionChangeCount: 2 } },
  });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  handle.observe({ ...owner(plan), componentId: 'cuisines', eventType: 'change', valueToken: 'A+', optionToken: 'A', selectionState: 'selected' });
  handle.observe({ ...owner(plan), componentId: 'cuisines', eventType: 'change', valueToken: 'B+', optionToken: 'B', selectionState: 'selected' });
  handle.observe({ ...owner(plan), componentId: 'cuisines', eventType: 'change', valueToken: 'C+', optionToken: 'C', selectionState: 'selected' });
  assert.equal(await handle.flush(), null, 'monotonic multi-select additions are not churn');
  handle.observe({ ...owner(plan), componentId: 'cuisines', eventType: 'change', valueToken: 'A-', optionToken: 'A', selectionState: 'cleared' });
  assert.equal(await handle.flush(), null, 'one reversal is below the configured threshold');
  handle.observe({ ...owner(plan), componentId: 'cuisines', eventType: 'change', valueToken: 'A+', optionToken: 'A', selectionState: 'selected' });
  const packet = await handle.flush();
  assert.equal(packet?.patterns[0]?.type, 'selection_reversal');
  assert.equal(packet?.patterns[0]?.metrics.reversalCount, 2);
  runtime.dispose();
});

test('runtime supports independent sessions and privacy-safe aggregation', async () => {
  const clock = createManualClock(0);
  const runtime = createIIAPRuntime({
    clock, idFactory: () => 'pkt_test',
    policy: { componentQuietMs: 1000, surfaceIdleMs: 10000, minReportIntervalMs: 0, maxReportsPerComponent: 2 },
  });
  const plan = firstPlan(new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'A' });
  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'B' });
  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'A' });
  const packet = await handle.flush();
  assert.equal(packet?.packetId, 'pkt_test');
  assert.equal(packet?.patterns[0]?.type, 'state_alternation');
  assert.equal(packet?.patterns[0]?.metrics.switchPattern, 'A-B-A');
  assert.deepEqual(packet?.patterns[0]?.basedOnEvents, ['event_1', 'event_2', 'event_3']);
  assert.deepEqual(packet?.observations.events.map((event) => event.valueToken), ['A', 'B', 'A']);
  assert.equal(validatePrivacy(packet), true);
  runtime.dispose();
});

test('quiet and idle deadlines are independently scheduled', () => {
  const clock = createManualClock(0);
  const runtime = createIIAPRuntime({ clock, policy: { componentQuietMs: 2000, surfaceIdleMs: 10000 } });
  const plan = firstPlan(new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'A' });
  clock.advanceBy(1999);
  handle.observe({ ...owner(plan), componentId: 'date', eventType: 'change', valueToken: 'B' });
  clock.advanceBy(1);
  handle.deactivate('host-request');
  runtime.dispose();
});

test('system clock preserves the browser timer receiver', () => {
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  let setReceiver: unknown;
  let clearReceiver: unknown;
  try {
    globalThis.setTimeout = function (this: unknown, ...args: Parameters<typeof setTimeout>) {
      setReceiver = this;
      return originalSetTimeout(...args);
    } as typeof setTimeout;
    globalThis.clearTimeout = function (this: unknown, handle) {
      clearReceiver = this;
      return originalClearTimeout(handle);
    } as typeof clearTimeout;
    const handle = systemClock.setTimeout(() => undefined, 60_000);
    systemClock.clearTimeout(handle);
    assert.equal(setReceiver, globalThis);
    assert.equal(clearReceiver, globalThis);
  } finally {
    globalThis.setTimeout = originalSetTimeout;
    globalThis.clearTimeout = originalClearTimeout;
  }
});

test('privacy and safe suggestion validation fail closed', () => {
  assert.equal(validatePrivacy({ nested: { rawValue: 'secret' } }), false);
  const circular: Record<string, unknown> = {};
  circular.self = circular;
  assert.doesNotThrow(() => validatePrivacy(circular));
  assert.equal(validatePrivacy(circular), false);
  assert.equal(validatePrivacy({ unsupported: 1n }), false);
  assert.equal(validatePrivacy({ unsupported: Number.NaN }), false);
  assert.equal(validatePrivacy({ unsupported: Number.POSITIVE_INFINITY }), false);
  assert.equal(validatePrivacy(undefined), false);
  const context = { accepted: true, surfaceInstanceId: 'm:booking', allowedTargets: [{ componentId: 'date', componentType: 'DateTimeInput', originalSurfaceId: 'booking', bindingPath: '/date', allowedValues: ['A'] }] };
  assert.ok(validateDataModelSuggestion({ kind: 'data_model_update', updates: [{ surfaceId: 'booking', surfaceInstanceId: 'm:booking', path: '/date', value: 'A' }] }, context));
  assert.equal(validateDataModelSuggestion({ kind: 'data_model_update', updates: [{ surfaceId: 'booking', path: '/unsafe', value: 'A' }] }, context), null);
  assert.equal(validateDataModelSuggestion({ kind: 'data_model_update', updates: [{ surfaceId: 'booking', path: '/date', value: 'B' }] }, context), null);
  assert.equal(validateDataModelSuggestion({ kind: 'data_model_update', updates: [{ surfaceId: 'booking', path: '/date', value: 'A' }] }, { ...context, accepted: false }), null);
});

test('adapters do not authorize suggestions without an explicit host policy', () => {
  const defaultPlan = firstPlan(new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  assert.equal(defaultPlan.components.some((component) => component.allowedValues?.length), false);
  const authorizedPlan = firstPlan(new A2UIV08Adapter({
    resolveAllowedValues: ({ bindingPath }) => bindingPath === '/date' ? ['A'] : undefined,
  }), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  assert.deepEqual(authorizedPlan.components.find((component) => component.bindingPath === '/date')?.allowedValues, ['A']);
});

test('maxAllowedSelections=1 overrides chips presentation to a single-value update target', () => {
  const adapter = new A2UIV08Adapter({
    resolveAllowedValues: ({ declaredValues }) => declaredValues,
  });
  const plan = firstPlan(adapter, {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'preferences' } },
      { surfaceUpdate: { surfaceId: 'preferences', components: [{
        id: 'frequency', component: { MultipleChoice: {
          variant: 'chips', maxAllowedSelections: 1,
          selections: { path: '/frequency' },
          options: [{ value: 'daily' }, { value: 'weekly' }],
        } },
      }] } },
    ],
  });
  const frequency = plan.components[0];
  assert.equal(frequency?.selectionMode, 'single');
  assert.deepEqual(frequency?.allowedValues, ['daily', 'weekly']);
});

test('a selection field without explicit cardinality is authorized but still allowlist-bounded', async () => {
  const adapter = new A2UIV08Adapter({
    resolveAllowedValues: ({ declaredValues }) => declaredValues,
  });
  const plan = firstPlan(adapter, {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'preferences' } },
      { surfaceUpdate: { surfaceId: 'preferences', components: [{
        id: 'risk', component: { MultipleChoice: {
          selections: { path: '/risk' },
          options: [{ value: 'low' }, { value: 'high' }],
        } },
      }] } },
    ],
  });
  // 选择基数是值的形状约束，不是授权前置条件：声明了取值范围的绑定字段仍可被建议。
  assert.equal(plan.components[0]?.selectionMode, 'single');
  assert.deepEqual(plan.components[0]?.allowedValues, ['low', 'high']);

  const runtime = createIIAPRuntime({
    policy: { componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0, thresholds: { optionChangeCount: 1 } },
  });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  handle.observe({ ...owner(plan), componentId: 'risk', eventType: 'change', valueToken: 'A' });
  const packet = await handle.flush();
  const target = packet?.allowedOperations.updateTargets[0];
  assert.ok(target);
  assert.equal(target.bindingPath, '/risk');
  assert.equal(target.maxAllowedSelections, undefined, 'no declared bound means no bound is advertised');

  // 放宽授权不等于放宽取值：越界值仍然整批拒绝。
  const context = {
    accepted: true, surfaceInstanceId: plan.surface.surfaceInstanceId, allowedTargets: packet!.allowedOperations.updateTargets,
  };
  assert.ok(validateDataModelSuggestion(
    { kind: 'data_model_update', updates: [{ surfaceId: 'preferences', path: '/risk', value: 'high' }] }, context,
  ));
  assert.equal(validateDataModelSuggestion(
    { kind: 'data_model_update', updates: [{ surfaceId: 'preferences', path: '/risk', value: 'crypto' }] }, context,
  ), null, 'values outside allowedValues stay rejected');
  runtime.dispose();
});

test('an implicit multi-select without an explicit limit preserves set semantics', async () => {
  const adapter = new A2UIV08Adapter({
    resolveAllowedValues: ({ declaredValues }) => declaredValues,
  });
  const plan = firstPlan(adapter, {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'preferences' } },
      { surfaceUpdate: { surfaceId: 'preferences', components: [{
        id: 'sectors', component: { MultipleChoice: {
          variant: 'chips', selections: { path: '/sectors' },
          options: [{ value: 'stocks' }, { value: 'bonds' }, { value: 'funds' }],
        } },
      }] } },
    ],
  });
  const runtime = createIIAPRuntime({
    policy: { componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0, thresholds: { optionChangeCount: 1 } },
  });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  handle.observe({ ...owner(plan), componentId: 'sectors', eventType: 'change', optionToken: 'A', selectionState: 'selected' });
  handle.observe({ ...owner(plan), componentId: 'sectors', eventType: 'change', optionToken: 'A', selectionState: 'cleared' });
  const packet = await handle.flush();
  const target = packet?.allowedOperations.updateTargets[0];
  assert.equal(target?.selectionMode, 'multiple');
  assert.equal(target?.maxAllowedSelections, undefined);
  const context = {
    accepted: true, surfaceInstanceId: plan.surface.surfaceInstanceId, allowedTargets: packet!.allowedOperations.updateTargets,
  };
  assert.ok(validateDataModelSuggestion(
    { kind: 'data_model_update', updates: [{ surfaceId: 'preferences', path: '/sectors', value: ['stocks', 'bonds'] }] }, context,
  ));
  for (const bad of ['stocks', ['stocks', 'stocks'], ['stocks', 'crypto']]) {
    assert.equal(validateDataModelSuggestion(
      { kind: 'data_model_update', updates: [{ surfaceId: 'preferences', path: '/sectors', value: bad }] }, context,
    ), null, `unbounded multi-select must reject ${JSON.stringify(bad)}`);
  }
  runtime.dispose();
});

test('a boolean toggle is authorized against its two-value domain and stays scalar', async () => {
  // A2UI 0.8 的 CheckBox 是无参数布尔开关，值域恒为 {true,false}——与 options 等价地有限。
  // 而 Slider 只有 minValue/maxValue 且协议无 step，DateTimeInput/TextField 值域无界，均不授权。
  const adapter = new A2UIV08Adapter({
    resolveAllowedValues: ({ declaredValues }) => declaredValues,
  });
  const plan = firstPlan(adapter, {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'preferences' } },
      { surfaceUpdate: { surfaceId: 'preferences', components: [
        { id: 'takeout', component: { CheckBox: { label: { literalString: '外卖' }, value: { path: '/takeout' } } } },
        { id: 'spice', component: { Slider: { label: { literalString: '辣度' }, value: { path: '/spice' }, minValue: 0, maxValue: 5 } } },
        { id: 'due', component: { DateTimeInput: { value: { path: '/due' }, enableTime: true } } },
      ] } },
    ],
  });
  const byId = new Map(plan.components.map((component) => [component.componentId, component]));
  assert.deepEqual(byId.get('takeout')?.allowedValues, [true, false]);
  assert.equal(byId.get('takeout')?.selectionMode, undefined, 'a toggle is scalar, not a selection set');
  assert.equal(byId.get('spice')?.allowedValues, undefined, 'a continuous slider has no enumerable domain');
  assert.equal(byId.get('due')?.allowedValues, undefined, 'a datetime field has no enumerable domain');

  const runtime = createIIAPRuntime({
    policy: { componentQuietMs: 60_000, surfaceIdleMs: 60_000, minReportIntervalMs: 0, thresholds: { booleanToggleCount: 1 } },
  });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  handle.observe({ ...owner(plan), componentId: 'takeout', eventType: 'change', valueToken: 'A' });
  const packet = await handle.flush();
  const targets = packet?.allowedOperations.updateTargets ?? [];
  assert.deepEqual(targets.map((target) => target.componentId), ['takeout']);
  assert.deepEqual(targets[0]?.allowedValues, [true, false]);
  assert.equal(targets[0]?.selectionMode, undefined);

  const context = { accepted: true, surfaceInstanceId: plan.surface.surfaceInstanceId, allowedTargets: targets };
  assert.ok(validateDataModelSuggestion(
    { kind: 'data_model_update', updates: [{ surfaceId: 'preferences', path: '/takeout', value: true }] }, context,
  ));
  assert.ok(validateDataModelSuggestion(
    { kind: 'data_model_update', updates: [{ surfaceId: 'preferences', path: '/takeout', value: false }] }, context,
  ));
  // 非布尔值仍然整批拒绝：授权布尔字段不等于放开任意取值。
  for (const bad of ['true', 1, 0, null, ['true']]) {
    assert.equal(validateDataModelSuggestion(
      { kind: 'data_model_update', updates: [{ surfaceId: 'preferences', path: '/takeout', value: bad }] }, context,
    ), null, `value ${JSON.stringify(bad)} must stay rejected`);
  }
  runtime.dispose();
});

test('explicit multi-select limit authorizes a bounded full-set update target', async () => {
  const adapter = new A2UIV08Adapter({
    resolveAllowedValues: ({ declaredValues }) => declaredValues,
  });
  const plan = firstPlan(adapter, {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: [
      { beginRendering: { surfaceId: 'preferences' } },
      { surfaceUpdate: { surfaceId: 'preferences', components: [{
        id: 'sectors', component: { MultipleChoice: {
          variant: 'chips', maxAllowedSelections: 3,
          selections: { path: '/sectors' },
          options: [{ value: 'stocks' }, { value: 'bonds' }, { value: 'funds' }],
        } },
      }] } },
    ],
  });
  assert.equal(plan.components[0]?.selectionMode, 'multiple');
  assert.equal(plan.components[0]?.maxAllowedSelections, 3);
  assert.deepEqual(plan.components[0]?.allowedValues, ['stocks', 'bonds', 'funds']);
  const runtime = createIIAPRuntime({ policy: { minReportIntervalMs: 0, thresholds: { optionChangeCount: 1 } } });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  handle.observe({ ...owner(plan), componentId: 'sectors', eventType: 'change', valueToken: 'A+', optionToken: 'A', selectionState: 'selected' });
  handle.observe({ ...owner(plan), componentId: 'sectors', eventType: 'change', valueToken: 'A-', optionToken: 'A', selectionState: 'cleared' });
  const packet = await handle.flush();
  assert.equal(packet?.allowedOperations.updateTargets[0]?.selectionMode, 'multiple');
  assert.equal(packet?.allowedOperations.updateTargets[0]?.maxAllowedSelections, 3);
  runtime.dispose();
});

test('decision parser recovers prose-wrapped model output without inventing a decision', () => {
  const body = JSON.stringify({
    decision: 'offer_help', reason: 'choice churn', offerType: 'text_assistance',
    helpTopic: 'compare_options', uiStyle: 'inline_card', message: '需要我帮你比较吗？',
  });
  const wrapped = [
    `The user switched options repeatedly.\n${body}`,
    `${body}\n\nI chose this because the evidence shows churn.`,
    `**Decision**\n${body}`,
    `\`\`\`json\n${body}\n\`\`\``,
    `Consider the set {a, b} then decide:\n${body}`,
  ];
  for (const raw of wrapped) {
    const parsed = parseDecision(raw);
    assert.equal(parsed.decision, 'offer_help', raw.slice(0, 50));
    assert.equal(parsed.reason, 'choice churn', raw.slice(0, 50));
  }
  // 真正的负向结果保持原样，不与"解析失败"混淆。
  const genuine = parseDecision(JSON.stringify({
    decision: 'no_intervention', reason: 'ordinary_filling', offerType: 'none', uiStyle: 'none', message: '',
  }));
  assert.equal(genuine.decision, 'no_intervention');
  assert.equal(genuine.reason, 'ordinary_filling');
  // 无 JSON 时 fail closed，且诊断状态可区分。
  assert.equal(parseDecision('I need more evidence.').reason, 'invalid_or_insufficient_iiap_decision');
  assert.equal(describeModelOutput('I need more evidence.'), 'no_json_object');
  assert.equal(describeModelOutput(body), 'recovered');
  assert.equal(describeModelOutput(wrapped[0]), 'prose_wrapped');
  // 说明文字里的花括号不能凭空造出 decision。
  assert.equal(describeModelOutput('The set {a, b} is fine.'), 'no_json_object');
  assert.equal(coerceModelObject('The set {a, b} is fine.'), null);
  assert.equal(parseDecision({
    decision: 'offer_help', reason: 'x'.repeat(1024), offerType: 'text_assistance',
    helpTopic: 'explain_rules', message: 'help',
  }).decision, 'offer_help', 'reason at the protocol hard limit remains valid');
  for (const invalidText of [
    { decision: 'offer_help', reason: 'x'.repeat(1025), offerType: 'text_assistance', message: 'help' },
    { decision: 'offer_help', reason: 'x', offerType: 'text_assistance', message: 'x'.repeat(2049) },
    { decision: 'offer_help', reason: 1, offerType: 'text_assistance', message: 'help' },
  ]) {
    assert.equal(parseDecision(invalidText).decision, 'no_intervention');
  }
});

test('uiStyle is derived from offerType and never invalidates a decision', () => {
  // 生产实测故障：模型把 uiStyle 写成不存在的 "inline_hint"，整份正确决策被作废。
  for (const invented of ['inline_hint', 'banner', 'card', 'INLINE_CARD', '']) {
    const parsed = parseDecision(JSON.stringify({
      decision: 'offer_help', reason: 'choice churn', offerType: 'text_assistance',
      helpTopic: 'compare_options', uiStyle: invented, message: '需要我帮你比较吗？',
    }));
    assert.equal(parsed.decision, 'offer_help', invented);
    assert.equal(parsed.offerType, 'text_assistance', invented);
    assert.equal(parsed.helpTopic, 'compare_options', invented);
    assert.equal(parsed.uiStyle, 'inline_card', invented);
  }
  // 模型不再输出 uiStyle 时同样成立。
  const withoutStyle = parseDecision(JSON.stringify({
    decision: 'offer_help', reason: 'choice churn', offerType: 'text_assistance',
    helpTopic: 'compare_options', message: '需要我帮你比较吗？',
  }));
  assert.equal(withoutStyle.decision, 'offer_help');
  assert.equal(withoutStyle.uiStyle, 'inline_card');
  // 负向事件必须渲染为 none，模型无法用 uiStyle 影响这一点。
  const negative = parseDecision(JSON.stringify({
    decision: 'no_intervention', reason: 'ordinary_filling', offerType: 'none', uiStyle: 'inline_card', message: '',
  }));
  assert.equal(negative.decision, 'no_intervention');
  assert.equal(negative.uiStyle, 'none');
});

test('decision parser accepts safe output and rejects unsafe updates', () => {
  const packet = {
    iiapVersion: '0.1' as const, packetId: 'p', sessionId: 's', messageId: 'm',
    originalSurfaceId: 'booking', surfaceInstanceId: 'm:booking', protocolVersion: '0.9.1' as const,
    timestamp: new Date(0).toISOString(), window: { startTime: new Date(0).toISOString(), endTime: new Date(1).toISOString(), durationMs: 1 },
    surfaceContext: { protocol: 'a2ui' as const, protocolVersion: '0.9.1' as const, snapshotType: 'sanitized_effective_definition' as const, definition: [], redaction: { dataModelExcluded: true as const, actionContextValuesExcluded: true as const, unreachableComponentsExcluded: false, unknownCustomPropertiesExcluded: true, truncated: false } },
    observations: { tokenScope: 'component_within_surface_instance' as const, events: [], completeness: { complete: true, droppedEventCount: 0 } },
    patterns: [], reportHistory: [], allowedOperations: { updateTargets: [{ componentId: 'date', componentType: 'DateTimeInput', originalSurfaceId: 'booking', bindingPath: '/date', allowedValues: ['A'] }] },
  };
  const safe = parseDecision({ decision: 'offer_help', reason: 'x', offerType: 'update_suggestion', uiStyle: 'inline_card', message: 'help', updateSuggestion: { kind: 'data_model_update', updates: [{ surfaceId: 'booking', path: '/date', value: 'A' }] } }, packet);
  assert.equal(safe.updateSuggestion?.updates.length, 1);
  const unsafe = parseDecision({ decision: 'offer_help', reason: 'x', offerType: 'update_suggestion', uiStyle: 'inline_card', message: 'help', updateSuggestion: { kind: 'data_model_update', updates: [{ surfaceId: 'other', path: '/date', value: 'A' }] } }, packet);
  assert.equal(unsafe.offerType, 'text_assistance');
  assert.equal(unsafe.helpTopic, 'explain_rules');
  const wrongValue = parseDecision({ decision: 'offer_help', reason: 'x', offerType: 'update_suggestion', uiStyle: 'inline_card', message: 'help', updateSuggestion: { kind: 'data_model_update', updates: [{ surfaceId: 'booking', path: '/date', value: 'B' }] } }, packet);
  assert.equal(wrongValue.offerType, 'text_assistance');
  assert.equal(wrongValue.helpTopic, 'explain_rules');
  assert.deepEqual(parseDecision({
    decision: 'offer_help', reason: 'x', offerType: 'text_assistance', uiStyle: 'inline_card', message: 'help',
  }, packet), {
    decision: 'offer_help', reason: 'x', offerType: 'text_assistance', helpTopic: 'explain_rules',
    uiStyle: 'inline_card', message: 'help',
  });
  const comparison = parseDecision({
    decision: 'offer_help', reason: 'x', offerType: 'text_assistance', uiStyle: 'inline_card', message: 'help',
  }, {
    ...packet,
    patterns: [{
      patternId: 'pattern.choice', type: 'selection_reversal', componentId: 'choice',
      basedOnEvents: [], metrics: {},
    }],
  });
  assert.equal(comparison.helpTopic, 'compare_options');
  assert.equal(parseDecision({
    decision: 'offer_help', reason: 'x', offerType: 'none', uiStyle: 'none', message: '',
  }, packet).decision, 'no_intervention');
  assert.deepEqual(parseDecision({
    decision: 'defer', reason: 'wait', offerType: 'text_assistance', helpTopic: 'fix_block',
    uiStyle: 'inline_card', message: 'later',
  }, packet), {
    decision: 'defer', reason: 'wait', offerType: 'none', uiStyle: 'none', message: 'later',
  });
});

test('decision parser accepts multi-field and multi-select batches only as a whole', () => {
  const packet = {
    iiapVersion: '0.1' as const, packetId: 'p', sessionId: 's', messageId: 'm',
    originalSurfaceId: 'preferences', surfaceInstanceId: 'm:preferences', protocolVersion: '0.8' as const,
    timestamp: new Date(0).toISOString(), window: { startTime: new Date(0).toISOString(), endTime: new Date(1).toISOString(), durationMs: 1 },
    surfaceContext: { protocol: 'a2ui' as const, protocolVersion: '0.8' as const, snapshotType: 'sanitized_effective_definition' as const, definition: [], redaction: { dataModelExcluded: true as const, actionContextValuesExcluded: true as const, unreachableComponentsExcluded: false, unknownCustomPropertiesExcluded: true, truncated: false } },
    observations: { tokenScope: 'component_within_surface_instance' as const, events: [], completeness: { complete: true, droppedEventCount: 0 } },
    patterns: [], reportHistory: [], allowedOperations: { updateTargets: [
      { componentId: 'risk', componentType: 'MultipleChoice', originalSurfaceId: 'preferences', bindingPath: '/risk', allowedValues: ['low', 'balanced'], selectionMode: 'single' as const, maxAllowedSelections: 1 },
      { componentId: 'sectors', componentType: 'MultipleChoice', originalSurfaceId: 'preferences', bindingPath: '/sectors', allowedValues: ['stocks', 'bonds', 'funds'], selectionMode: 'multiple' as const, maxAllowedSelections: 3 },
    ] },
  };
  const base = { decision: 'offer_help', reason: 'x', offerType: 'update_suggestion', uiStyle: 'inline_card', message: 'set risk and sectors' };
  const safe = parseDecision({ ...base, updateSuggestion: { kind: 'data_model_update', updates: [
    { surfaceId: 'preferences', path: '/risk', value: 'balanced' },
    { surfaceId: 'preferences', path: '/sectors', value: ['stocks', 'bonds'] },
  ] } }, packet);
  assert.deepEqual(safe.updateSuggestion?.updates.map((update) => update.value), ['balanced', ['stocks', 'bonds']]);
  const partiallyUnsafe = parseDecision({ ...base, updateSuggestion: { kind: 'data_model_update', updates: [
    { surfaceId: 'preferences', path: '/risk', value: 'balanced' },
    { surfaceId: 'preferences', path: '/sectors', value: ['stocks', 'crypto'] },
  ] } }, packet);
  assert.equal(partiallyUnsafe.offerType, 'text_assistance');
  assert.equal(partiallyUnsafe.updateSuggestion, undefined);
  const duplicatePath = parseDecision({ ...base, updateSuggestion: { kind: 'data_model_update', updates: [
    { surfaceId: 'preferences', path: '/risk', value: 'low' },
    { surfaceId: 'preferences', path: '/risk', value: 'balanced' },
  ] } }, packet);
  assert.equal(duplicatePath.offerType, 'text_assistance');
});

test('feedback backoff and executor require explicit acceptance', async () => {
  const clock = createManualClock(0);
  const feedback = new FeedbackController(clock, resolvePolicy({ feedbackBackoffMs: { dismissed: 1000 } }));
  feedback.record('dismissed');
  assert.equal(feedback.canOffer(), false);
  clock.advanceBy(1000);
  assert.equal(feedback.canOffer(), true);
  const applied: unknown[] = [];
  const executor = new ValidatedSuggestionExecutor((updates) => { applied.push(updates); });
  const suggestion = { kind: 'data_model_update' as const, updates: [{ surfaceId: 'booking', surfaceInstanceId: 'm:booking', path: '/date', value: 'A' }] };
  const context = { accepted: true, surfaceInstanceId: 'm:booking', allowedTargets: [{ componentId: 'date', componentType: 'DateTimeInput', originalSurfaceId: 'booking', bindingPath: '/date', allowedValues: ['A'] }] };
  await executor.execute(suggestion, context);
  assert.equal(applied.length, 1);
  await assert.rejects(() => executor.execute(suggestion, { ...context, accepted: false }));
  await assert.rejects(
    () => executor.execute({
      ...suggestion,
      updates: [{ ...suggestion.updates[0]!, surfaceInstanceId: 'old:booking' }],
    }, context),
    (error: unknown) => Boolean(error && typeof error === 'object' && 'code' in error && error.code === 'STALE_SURFACE'),
  );
  assert.equal(applied.length, 1, 'stale suggestions never reach applyBatch');
});

test('flush diagnostics distinguish feedback backoff and allow a later report', async () => {
  const clock = createManualClock(0);
  const skipped: string[] = [];
  const runtime = createIIAPRuntime({
    clock,
    policy: {
      componentQuietMs: 60_000,
      surfaceIdleMs: 60_000,
      minReportIntervalMs: 0,
      maxReportsPerComponent: 3,
      feedbackBackoffMs: { accepted: 3000 },
      thresholds: { optionChangeCount: 2 },
    },
    onFlushSkipped: (reason) => skipped.push(reason),
    transport: {
      decide: async (packet) => ({
        type: 'iiap.decision', iiapVersion: '0.1', decisionId: `decision-${packet.packetId}`,
        packetId: packet.packetId, surfaceInstanceId: packet.surfaceInstanceId,
        payload: { decision: 'offer_help', reason: 'test', offerType: 'text_assistance', helpTopic: 'explain_rules', uiStyle: 'inline_card', message: 'help' },
      }),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
    presenter: { present: async () => 'accepted' },
    onAccept: async () => undefined,
  });
  const plan = firstPlan(new A2UIV08Adapter(), {
    sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages,
  });
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);

  assert.equal(await handle.flush(), null);
  assert.equal(skipped.at(-1), 'no_pattern');
  for (const valueToken of ['A', 'B']) {
    handle.observe({ ...owner(plan), componentId: 'choice', eventType: 'change', valueToken });
  }
  assert.ok(await handle.flush());
  await new Promise<void>((resolve) => setImmediate(resolve));

  for (const valueToken of ['A', 'B']) {
    handle.observe({ ...owner(plan), componentId: 'choice', eventType: 'change', valueToken });
  }
  assert.equal(await handle.flush(), null);
  assert.equal(skipped.at(-1), 'feedback_backoff');
  clock.advanceBy(3000);
  assert.ok(await handle.flush());
  runtime.dispose();
});

test('feedback correlates the presented decision and records accepted execution outcome', async () => {
  const clock = createManualClock(0);
  const uploaded: unknown[] = [];
  let respond: ((interaction: 'accepted') => void) | undefined;
  let accepted = 0;
  const runtime = createIIAPRuntime({
    clock,
    policy: { minReportIntervalMs: 0, thresholds: { optionChangeCount: 2 } },
    feedbackUpload: true,
    transport: {
      decide: async (packet) => ({
        type: 'iiap.decision', iiapVersion: '0.1', decisionId: 'decision-feedback',
        packetId: packet.packetId, surfaceInstanceId: packet.surfaceInstanceId,
        payload: { decision: 'offer_help', reason: 'test', offerType: 'text_assistance', helpTopic: 'explain_rules', uiStyle: 'inline_card', message: 'help' },
      }),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
      sendFeedback: async (feedback) => { uploaded.push(feedback); },
    },
    presenter: {
      present: async (_decision, context) => {
        assert.equal(context.decisionId, 'decision-feedback');
        return await new Promise<'accepted'>((resolve) => { respond = resolve; });
      },
    },
    onAccept: async () => { accepted += 1; },
  });
  const plan = firstPlan(new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  const session = runtime.createSession({ sessionId: 's' });
  const handle = session.activate(plan);
  handle.observe({ ...owner(plan), componentId: 'choice', eventType: 'change', valueToken: 'A' });
  handle.observe({ ...owner(plan), componentId: 'choice', eventType: 'change', valueToken: 'B' });
  const packet = await handle.flush();
  assert.ok(packet);
  assert.equal(session.recordFeedback({
    type: 'iiap.feedback', iiapVersion: '0.1', packetId: packet.packetId,
    decisionId: 'decision-feedback', surfaceInstanceId: 'wrong:surface',
    offerType: 'text_assistance', interaction: 'dismissed', timestamp: new Date(0).toISOString(),
  }), false);
  assert.ok(respond);
  respond('accepted');
  await new Promise<void>((resolve) => setImmediate(resolve));
  assert.equal(accepted, 1);
  assert.equal(uploaded.length, 1);
  assert.deepEqual(uploaded[0], {
    type: 'iiap.feedback', iiapVersion: '0.1', packetId: packet.packetId,
    decisionId: 'decision-feedback', surfaceInstanceId: packet.surfaceInstanceId,
    offerType: 'text_assistance', interaction: 'accepted', outcome: 'succeeded',
    timestamp: new Date(0).toISOString(),
  });
  assert.equal(session.recordFeedback(uploaded[0] as Parameters<typeof session.recordFeedback>[0]), false);
  runtime.dispose();
});

test('accepted execution failure is recorded locally without feedback upload by default', async () => {
  const clock = createManualClock(0);
  let respond: ((interaction: 'accepted') => void) | undefined;
  let uploads = 0;
  const errors: Error[] = [];
  const runtime = createIIAPRuntime({
    clock,
    policy: { minReportIntervalMs: 0, thresholds: { optionChangeCount: 2 } },
    transport: {
      decide: async (packet) => ({
        type: 'iiap.decision', iiapVersion: '0.1', decisionId: 'decision-failed',
        packetId: packet.packetId, surfaceInstanceId: packet.surfaceInstanceId,
        payload: { decision: 'offer_help', reason: 'test', offerType: 'text_assistance', helpTopic: 'explain_rules', uiStyle: 'inline_card', message: 'help' },
      }),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
      sendFeedback: async () => { uploads += 1; },
    },
    presenter: {
      present: async () => await new Promise<'accepted'>((resolve) => { respond = resolve; }),
    },
    onAccept: async () => { throw new Error('host execution failed'); },
    onError: (error) => errors.push(error),
  });
  const plan = firstPlan(new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages });
  const session = runtime.createSession({ sessionId: 's' });
  const handle = session.activate(plan);
  handle.observe({ ...owner(plan), componentId: 'choice', eventType: 'change', valueToken: 'A' });
  handle.observe({ ...owner(plan), componentId: 'choice', eventType: 'change', valueToken: 'B' });
  assert.ok(await handle.flush());
  assert.ok(respond);
  respond('accepted');
  await new Promise<void>((resolve) => setImmediate(resolve));
  assert.equal(errors[0]?.message, 'host execution failed');
  assert.equal(uploads, 0);
  assert.equal(session.recordFeedback({
    type: 'iiap.feedback', iiapVersion: '0.1', packetId: 'unknown-packet',
    decisionId: 'decision-failed', surfaceInstanceId: plan.surface.surfaceInstanceId,
    offerType: 'text_assistance', interaction: 'accepted', outcome: 'failed',
    reasonCode: 'execution_failed', timestamp: new Date(0).toISOString(),
  }), false);
  runtime.dispose();
});

test('packet event arrays are bounded by policy and report dropped events honestly', async () => {
  const runtime = createIIAPRuntime({
    clock: createManualClock(0),
    policy: { minReportIntervalMs: 0, maxEventsPerPacket: 999, thresholds: { temporalChangeCount: 1 } },
    transport: {
      decide: async (packet) => noIntervention(packet),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
  });
  const plan = firstPlan(
    new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages },
  );
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  for (let index = 0; index < 200; index += 1) {
    handle.observe({
      ...owner(plan), componentId: 'date', eventType: 'change',
      valueToken: `value-${index % 3}`, timestamp: index,
    });
  }
  const packet = await handle.flush();
  assert.ok(packet);
  const events = packet.observations.events;
  assert.equal(events.length, 128, 'event array must respect the packet schema bound');
  assert.equal(packet.observations.completeness.complete, false, 'truncation must not claim completeness');
  assert.equal(packet.observations.completeness.droppedEventCount, 200 - events.length);
  assert.equal(events[0]!.sequence, 200 - events.length + 1, 'the most recent facts are retained');
  const retained = new Set(events.map((event) => event.eventId));
  for (const pattern of packet.patterns) {
    for (const eventId of pattern.basedOnEvents) {
      assert.ok(retained.has(eventId), `basedOnEvents must not dangle: ${eventId}`);
    }
  }
  runtime.dispose();
});

test('packets below the event bound keep reporting complete observations', async () => {
  const runtime = createIIAPRuntime({
    clock: createManualClock(0),
    policy: { minReportIntervalMs: 0, thresholds: { temporalChangeCount: 1 } },
    transport: {
      decide: async (packet) => noIntervention(packet),
      assist: async (request) => ({ type: 'iiap.assistance.response', iiapVersion: '0.1', requestId: request.requestId, message: 'help' }),
    },
  });
  const plan = firstPlan(
    new A2UIV08Adapter(), { sessionId: 's', messageId: 'm', namespace: 'm', messages: v08Messages },
  );
  const handle = runtime.createSession({ sessionId: 's' }).activate(plan);
  for (let index = 0; index < 6; index += 1) {
    handle.observe({
      ...owner(plan), componentId: 'date', eventType: 'change',
      valueToken: `value-${index % 2}`, timestamp: index,
    });
  }
  const packet = await handle.flush();
  assert.ok(packet);
  assert.equal(packet.observations.events.length, 6);
  assert.deepEqual(packet.observations.completeness, { complete: true, droppedEventCount: 0 });
  runtime.dispose();
});
