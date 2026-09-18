# Third-party dependency inventory

This is a technical inventory for the `0.1.0-rc.1` release candidate, not a
legal conclusion. The final `agent-protocol` root notice remains subject to the
repository maintainer's compliance policy.

## Published runtime and peer dependencies

| Ecosystem | Dependency | RC lock version/range | Role | License |
|---|---|---|---|---|
| Python | `jsonschema` | `4.26.0` (`>=4.23`) | Runtime packet validation | MIT |
| Python | `attrs` | `26.1.0` | `jsonschema` transitive dependency | MIT |
| Python | `jsonschema-specifications` | `2025.9.1` | `jsonschema` transitive dependency | MIT |
| Python | `referencing` | `0.37.0` | `jsonschema` transitive dependency | MIT |
| Python | `rpds-py` | `2026.6.3` | `referencing` transitive dependency | MIT |
| Python | `typing-extensions` | `4.16.0` | Conditional transitive dependency | PSF-2.0 |
| npm | `react` | `^18.2.0` | Optional peer for the React presenter; not bundled | MIT |

The npm package has no bundled third-party runtime dependency. Consumers that
do not import `@openjiuwen/iiap/react` do not need React.

## Direct development and build dependencies

- npm: TypeScript, Vite, React DOM, and Node/React type declarations.
- Python: pytest, pytest-asyncio, build, setuptools, and wheel.

These tools do not ship as IIAP runtime code. Exact resolved versions are
recorded in `package-lock.json` and `implementations/python/uv.lock`. Regenerate
this inventory and the repository notice whenever either lockfile changes.
