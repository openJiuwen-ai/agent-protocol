# Compatibility and security

Last updated: 2026-09-18

## Supported baseline

| Area | v0.1 support |
|---|---|
| IIAP wire version | `0.1` |
| A2UI | v0.8 built-in adapter |
| Python | 3.11 or newer |
| Node.js | 20 or newer |
| React presenter | React `^18.2.0`, optional peer |

A2UI v0.9.1 is not exported or packaged. Implement a tested custom adapter or
wait for a future conformance-complete release.

## Security invariants

- The SDK observes semantic metadata, never raw form values or raw input text.
- Events require the complete message, surface-instance, and component owner.
- UI updates require explicit Host authorization, explicit user acceptance,
  current-surface ownership, and exact path/value allowlists.
- A batch of updates is all-or-nothing.
- Assistance output is plain text and rejects A2UI or internal protocol data.
- Malformed packets and decisions fail closed before unsafe work is performed.
- Feedback upload is disabled by default.

## Accepted v0.1 limitation

The v0.8 definition sanitizer excludes data-model contents and action-context
values, but it does not maintain a per-component property allowlist. Generated
packets therefore report `unknownCustomPropertiesExcluded: false`. Hosts must
not place secrets in arbitrary A2UI definition properties.
