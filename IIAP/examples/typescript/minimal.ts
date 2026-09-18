import { createIIAPRuntime } from '@openjiuwen/iiap';
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';

const adapter = new A2UIV08Adapter();
const runtime = createIIAPRuntime();
const session = runtime.createSession({ sessionId: 'example-session' });
const plan = adapter.buildObservationPlans({
  sessionId: 'example-session', messageId: 'message-1', namespace: 'message-1',
  messages: [
    { surfaceUpdate: { surfaceId: 'main', components: [
      { id: 'preference', component: { CheckBox: { value: { path: '/preference' } } } },
    ] } },
    { beginRendering: { surfaceId: 'main', root: 'preference' } },
  ],
})[0];
if (!plan) throw new Error('No observable surface');
const handle = session.activate(plan);

handle.deactivate('host-request');
runtime.dispose();
