# Changelog

## 0.1.0-rc.1 - 2026-09-18

- Extract the IIAP runtime, contracts, services, adapters, transport, presenter,
  and host integration boundary from the embedded JiuwenSwarm implementation.
- Add the verified A2UI v0.8 migration adapter. Retain a source-only v0.9.1
  prototype for research; it is not exported or included in the npm package.
- Validate every Python `DecisionService` packet against the packaged canonical
  Schema before model invocation and reject malformed input as `INVALID_PACKET`.
- Add TypeScript and Python conformance suites and standalone package builds.
- Replace JiuwenSwarm embedded runtime, observation, privacy, prompt, and
  decision algorithms with SDK-backed compatibility adapters.
- Verify npm tarballs, Python sdist/wheel, the React demo, production Web build,
  and a local browser smoke without a remote model.
- Register the Python SDK as a JiuwenSwarm runtime dependency and remove
  repository-layout imports from the backend startup path.
- Scope JiuwenSwarm A2UI observation cleanup to the activation that owns it,
  so an unmounted stale message cannot deactivate the latest surface.
- Fix the JiuwenSwarm test console disclosure layout so visible controls are
  interactive, and report manual-trigger outcomes and failures explicitly.
- Preserve the browser receiver when the TypeScript SDK schedules or clears
  timers, preventing `Illegal invocation` during observation aggregation.
- Treat normal multi-select additions as progress and only report repeated
  selected/cleared reversals for the same component-local option token.
- Bind renderer events to the active message, surface instance, and component,
  preventing A2UI returned by an IIAP assistance turn from taking observation ownership.
- Enforce a text-only assistance output contract without prescribing whether
  the host reuses its business agent or routes to a dedicated model.
- State the decision `reason`/`message` length limits in both model prompts,
  raise the `reason` hard limit from 512 to 1024 characters for realistic model
  variance, and report over-limit output with the correct fail-closed category.
