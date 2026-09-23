# Compatibility and security

Last updated: 2026-09-23

## Supported baseline

| Area | v0.1 support |
|---|---|
| IIAP wire version | `0.1` |
| A2UI | v0.8 built-in adapter |
| Python | 3.11 or newer |
| Node.js | 20 or newer |
| React presenter | React `^18.2.0`, optional peer |

A2UI v0.9.1 has no built-in adapter or source prototype. Implement a tested
custom adapter or wait for a future conformance-complete release.

## Security invariants

- The SDK observes semantic metadata, never raw form values or raw input text.
- Events require the complete message, surface-instance, and component owner.
- UI updates require explicit Host authorization, explicit user acceptance,
  current-surface ownership, and exact path/value allowlists.
- A batch of updates is all-or-nothing.
- Assistance output is plain text and rejects A2UI or internal protocol data.
- Malformed packets and decisions fail closed before unsafe work is performed.
- Feedback upload is disabled by default.

## Definition sanitization

The v0.8 definition sanitizer retains only fields from the standard A2UI v0.8
component catalog. It excludes data-model contents, action-context values,
unknown component properties, and properties of unknown component types;
generated packets therefore report `unknownCustomPropertiesExcluded: true`.
The service also recursively rejects normalized sensitive field names and does
not send definitions marked as retaining unknown properties to a model. Hosts
must still never place secrets in legitimate visible static-text fields.
