# Integration tests

Integration checks intentionally run from their owning source trees rather than
being duplicated here.

From the `IIAP/` directory, run:

```bash
npm test
npm run contracts
npm run packages:smoke
npm run demo:build
uv run --project implementations/python pytest implementations/python/tests -q
```

These commands cover the SDK Runtime/Adapter/Transport/Presenter boundary,
Python model services, wire contracts, demos, and clean-install npm/wheel artifacts.

They do not drive a Host application's browser DOM, remote model, persisted
WebSocket session, keyboard/screen reader, or A2UI v0.9.1 conformance. Each Host
must maintain those acceptance procedures in its own integration repository.
