# Examples

- `browser/`: default React HelpPresenter with accepted/dismissed interaction feedback.
- `typescript/complete-v08.ts`: complete, runnable A2UI v0.8 client integration with Runtime, Adapter, Transport, Presenter, assistance, and safe update handling.
- `typescript/minimal.ts`: minimal Adapter and Runtime lifecycle example; not a conformance example.
- `python/complete_service.py`: runnable DecisionService, AssistanceService, and ModelAdapter example.
- `python/minimal.py`: DecisionService invocation.

From the `IIAP/` root:

```bash
npm run build
node implementations/typescript/dist/examples/typescript/complete-v08.js

PYTHONPATH=implementations/python/src \
  uv run python examples/python/complete_service.py

npm run demo:build
```
