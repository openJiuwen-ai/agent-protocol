import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import {
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const npmPackage = resolve(root, 'implementations/typescript/packages/core');
const pythonPackage = resolve(root, 'implementations/python');
const temporaryRoot = mkdtempSync(join(tmpdir(), 'iiap-package-smoke-'));
const pythonCommand = process.env.PYTHON || 'python3';

function run(command, args, options = {}) {
  return execFileSync(command, args, {
    cwd: options.cwd ?? root,
    encoding: 'utf8',
    stdio: options.capture ? 'pipe' : 'inherit',
    env: {
      ...process.env,
      PIP_DISABLE_PIP_VERSION_CHECK: '1',
    },
  });
}

try {
  const npmArtifacts = resolve(temporaryRoot, 'npm-artifacts');
  const npmConsumer = resolve(temporaryRoot, 'npm-consumer');
  mkdirSync(npmArtifacts);
  mkdirSync(npmConsumer);

  const packOutput = run(
    'npm',
    ['pack', npmPackage, '--pack-destination', npmArtifacts, '--json'],
    { capture: true },
  );
  const [packed] = JSON.parse(packOutput);
  assert.ok(packed?.filename, 'npm pack did not produce a tarball');
  const packedFiles = new Set(packed.files.map((file) => file.path));
  for (const required of [
    'README.md',
    'LICENSE',
    'dist/index.js',
    'dist/index.d.ts',
    'dist/decision/index.js',
    'dist/http/index.js',
    'dist/a2ui-v08/index.js',
    'dist/react/index.js',
    'dist/testing.js',
  ]) {
    assert.ok(packedFiles.has(required), `npm tarball is missing ${required}`);
  }

  writeFileSync(
    resolve(npmConsumer, 'package.json'),
    JSON.stringify({ private: true, type: 'module' }, null, 2),
  );
  const tarball = resolve(npmArtifacts, packed.filename);
  run('npm', [
    'install', '--ignore-scripts', '--no-audit', '--no-fund', '--package-lock=false',
    'react@18.3.1', tarball,
  ], { cwd: npmConsumer });
  run('node', ['--input-type=module', '-e', `
    import assert from 'node:assert/strict';
    const core = await import('@openjiuwen/iiap');
    const decision = await import('@openjiuwen/iiap/decision');
    const http = await import('@openjiuwen/iiap/http');
    const adapter = await import('@openjiuwen/iiap/a2ui-v08');
    const react = await import('@openjiuwen/iiap/react');
    const testing = await import('@openjiuwen/iiap/testing');
    assert.equal(typeof core.createIIAPRuntime, 'function');
    assert.equal(typeof decision.parseDecision, 'function');
    assert.equal(typeof http.HTTPDecisionTransport, 'function');
    assert.equal(typeof adapter.A2UIV08Adapter, 'function');
    assert.equal(typeof react.ReactHelpPresenter, 'function');
    assert.equal(typeof testing.createManualClock, 'function');
  `], { cwd: npmConsumer });

  const pythonArtifacts = resolve(temporaryRoot, 'python-artifacts');
  const pythonConsumer = resolve(temporaryRoot, 'python-consumer');
  const virtualEnvironment = resolve(temporaryRoot, 'python-venv');
  mkdirSync(pythonArtifacts);
  mkdirSync(pythonConsumer);
  run('uv', ['build', '--out-dir', pythonArtifacts], { cwd: pythonPackage });
  const wheelName = JSON.parse(run(pythonCommand, ['-c', `
import json
from pathlib import Path
wheels = sorted(Path(${JSON.stringify(pythonArtifacts)}).glob('*.whl'))
assert len(wheels) == 1, wheels
print(json.dumps(wheels[0].name))
  `], { capture: true }).trim());
  const wheel = resolve(pythonArtifacts, wheelName);
  run(pythonCommand, ['-m', 'venv', virtualEnvironment]);
  const venvPython = process.platform === 'win32'
    ? resolve(virtualEnvironment, 'Scripts', 'python.exe')
    : resolve(virtualEnvironment, 'bin', 'python');
  run(venvPython, ['-m', 'pip', 'install', wheel], { cwd: pythonConsumer });
  run(venvPython, ['-c', `
from importlib.metadata import metadata, version
from importlib.resources import files
from iiap import AssistanceService, DecisionService, validate_intent_context_packet, validate_privacy

assert version('openjiuwen-iiap') == '0.1.0rc1'
assert metadata('openjiuwen-iiap')['Description-Content-Type'] == 'text/markdown'
assert any(item.startswith('jsonschema') for item in metadata('openjiuwen-iiap').get_all('Requires-Dist'))
assert AssistanceService.__name__ == 'AssistanceService'
assert DecisionService.__name__ == 'DecisionService'
assert files('iiap.schemas').joinpath('intent-context-packet.schema.json').is_file()
assert validate_privacy({'safe': True})
  `], { cwd: pythonConsumer });

  const pythonReadme = readFileSync(resolve(pythonPackage, 'README.md'), 'utf8');
  assert.ok(pythonReadme.includes('# openjiuwen-iiap'));
  console.log('package smoke checks passed: npm tarball and Python wheel clean-install imports');
} finally {
  rmSync(temporaryRoot, { recursive: true, force: true });
}
