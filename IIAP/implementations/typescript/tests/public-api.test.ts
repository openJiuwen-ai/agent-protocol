import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

interface PublicAPISnapshot {
  rootValues: string[];
  rootTypes: string[];
  subpaths: string[];
}

const namesFromBlocks = (source: string, expression: RegExp): string[] => [...source.matchAll(expression)]
  .flatMap((match) => match[1]!.split(','))
  .map((name) => name.trim())
  .filter(Boolean)
  .sort();

test('the single npm package exposes only approved root values and supported subpaths', async () => {
  const sourceRoot = new URL('../../../../', import.meta.url);
  const snapshot = JSON.parse(readFileSync(new URL('tests/fixtures/public-api.snapshot.json', sourceRoot), 'utf8')) as PublicAPISnapshot;
  const root = await import('@openjiuwen/iiap');
  assert.deepEqual(Object.keys(root).sort(), snapshot.rootValues.sort());

  const declarations = readFileSync(new URL('packages/core/dist/index.d.ts', sourceRoot), 'utf8');
  assert.deepEqual(namesFromBlocks(declarations, /export type \{([^}]+)\}/gs), snapshot.rootTypes.sort());
  const packageJSON = JSON.parse(readFileSync(new URL('packages/core/package.json', sourceRoot), 'utf8')) as { exports: Record<string, unknown> };
  assert.deepEqual(Object.keys(packageJSON.exports).sort(), snapshot.subpaths.sort());

  assert.equal(typeof (await import('@openjiuwen/iiap/decision')).parseDecision, 'function');
  assert.equal(typeof (await import('@openjiuwen/iiap/http')).HTTPDecisionTransport, 'function');
  assert.equal(typeof (await import('@openjiuwen/iiap/a2ui-v08')).A2UIV08Adapter, 'function');
  assert.equal(typeof (await import('@openjiuwen/iiap/react')).ReactHelpPresenter, 'function');
  assert.equal(typeof (await import('@openjiuwen/iiap/testing')).createManualClock, 'function');

  const forbiddenDeepImport = '@openjiuwen/iiap/' + 'feedback';
  await assert.rejects(import(forbiddenDeepImport), { code: 'ERR_PACKAGE_PATH_NOT_EXPORTED' });
});
