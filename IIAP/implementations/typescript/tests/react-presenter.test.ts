import assert from 'node:assert/strict';
import test from 'node:test';

import type { IIAPDecision, IntentContextPacket } from '@openjiuwen/iiap';
import { ReactHelpPresenter } from '@openjiuwen/iiap/react';

const decision: IIAPDecision = {
  decision: 'offer_help',
  reason: 'test',
  offerType: 'text_assistance',
  uiStyle: 'inline_card',
  message: 'help',
};

function packet(packetId: string, surfaceInstanceId: string): IntentContextPacket {
  return {
    iiapVersion: '0.1', packetId, sessionId: 'session', messageId: 'message',
    originalSurfaceId: 'surface', surfaceInstanceId, protocolVersion: '0.9.1',
    timestamp: new Date(0).toISOString(),
    window: { startTime: new Date(0).toISOString(), endTime: new Date(0).toISOString(), durationMs: 0 },
    surfaceContext: {
      protocol: 'a2ui', protocolVersion: '0.9.1', snapshotType: 'sanitized_effective_definition',
      definition: [], redaction: {
        dataModelExcluded: true, actionContextValuesExcluded: true,
        unreachableComponentsExcluded: false, unknownCustomPropertiesExcluded: true, truncated: false,
      },
    },
    observations: { tokenScope: 'component_within_surface_instance', events: [], completeness: { complete: true, droppedEventCount: 0 } },
    patterns: [], reportHistory: [], allowedOperations: { updateTargets: [] },
  };
}

test('React presenters isolate offers between instances', async () => {
  const first = new ReactHelpPresenter();
  const second = new ReactHelpPresenter();
  const firstResult = first.present(decision, { packet: packet('p1', 'surface:1'), decisionId: 'd1' });
  const secondResult = second.present(decision, { packet: packet('p2', 'surface:2'), decisionId: 'd2' });
  assert.equal(first.getSnapshot()?.decisionId, 'd1');
  assert.equal(second.getSnapshot()?.decisionId, 'd2');
  assert.equal(second.respond('d2', 'accepted'), true);
  assert.equal(await secondResult, 'accepted');
  assert.equal(first.getSnapshot()?.decisionId, 'd1');
  assert.equal(first.respond('d1', 'dismissed'), true);
  assert.equal(await firstResult, 'dismissed');
  first.dispose();
  second.dispose();
});

test('a new offer ignores the old offer and rejects its stale response', async () => {
  const presenter = new ReactHelpPresenter();
  const oldResult = presenter.present(decision, { packet: packet('p1', 'surface:1'), decisionId: 'old' });
  const newResult = presenter.present(decision, { packet: packet('p2', 'surface:1'), decisionId: 'new' });
  assert.equal(await oldResult, 'ignored');
  assert.equal(presenter.respond('old', 'accepted'), false);
  assert.equal(presenter.getSnapshot()?.decisionId, 'new');
  assert.equal(presenter.respond('new', 'dismissed'), true);
  assert.equal(await newResult, 'dismissed');
  presenter.dispose();
});

test('surface dismissal is not reported as a user dismissal', async () => {
  const presenter = new ReactHelpPresenter();
  const result = presenter.present(decision, { packet: packet('p1', 'surface:1'), decisionId: 'd1' });
  presenter.dismiss('other:surface');
  assert.equal(presenter.getSnapshot()?.decisionId, 'd1');
  presenter.dismiss('surface:1');
  assert.equal(await result, 'ignored');
  assert.equal(presenter.getSnapshot(), null);
  presenter.dispose();
});

test('offers time out after the configured TTL', async (context) => {
  context.mock.timers.enable({ apis: ['setTimeout'] });
  const presenter = new ReactHelpPresenter({ ttlMs: 5_000 });
  const result = presenter.present(decision, { packet: packet('p1', 'surface:1'), decisionId: 'd1' });
  context.mock.timers.tick(4_999);
  assert.equal(presenter.getSnapshot()?.decisionId, 'd1');
  context.mock.timers.tick(1);
  assert.equal(await result, 'timed_out');
  assert.equal(presenter.getSnapshot(), null);
  presenter.dispose();
});

test('presenter TTL stays within the supported range', () => {
  assert.throws(() => new ReactHelpPresenter({ ttlMs: 4_999 }), RangeError);
  assert.throws(() => new ReactHelpPresenter({ ttlMs: 600_001 }), RangeError);
});
