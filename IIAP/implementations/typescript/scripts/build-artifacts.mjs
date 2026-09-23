import { cpSync, mkdirSync, rmSync } from 'node:fs';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '../../..');
const emitted = resolve(root, 'implementations/typescript/dist');
const targets = [
  ['implementations/typescript/packages/core', 'implementations/typescript/packages/core/src'],
];

for (const [target, source] of targets) {
  const destination = resolve(root, target, 'dist');
  rmSync(destination, { recursive: true, force: true });
  mkdirSync(destination, { recursive: true });
  cpSync(resolve(emitted, source), destination, { recursive: true });
  cpSync(resolve(root, 'LICENSE'), resolve(root, target, 'LICENSE'));
}

const packageDist = resolve(root, 'implementations/typescript/packages/core/dist');
for (const [subpath, source] of [
  ['decision', 'implementations/typescript/packages/decision-client/src'],
  ['http', 'transports/http/src'],
  ['a2ui-v08', 'adapters/a2ui-v0.8/src'],
  ['react', 'presenters/react/src'],
]) {
  cpSync(resolve(emitted, source), resolve(packageDist, subpath), { recursive: true });
}
