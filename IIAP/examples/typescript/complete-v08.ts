import {
  ValidatedSuggestionExecutor,
  createIIAPRuntime,
  type AssistanceRequest,
  type AssistanceResponse,
  type DecisionTransport,
  type FeedbackInteraction,
  type HelpPresenter,
  type IIAPDecision,
  type IIAPDecisionEnvelope,
  type IntentContextPacket,
  type SafeDataModelUpdate,
} from '@openjiuwen/iiap';
import { A2UIV08Adapter } from '@openjiuwen/iiap/a2ui-v08';

class DemoTransport implements DecisionTransport {
  async decide(packet: IntentContextPacket): Promise<IIAPDecisionEnvelope> {
    return {
      type: 'iiap.decision',
      iiapVersion: '0.1',
      decisionId: 'decision-demo-1',
      packetId: packet.packetId,
      surfaceInstanceId: packet.surfaceInstanceId,
      payload: {
        decision: 'offer_help',
        reason: 'repeated_option_change',
        offerType: 'text_assistance',
        helpTopic: 'compare_options',
        uiStyle: 'inline_card',
        message: '需要我帮你比较这些选项吗？',
      },
    };
  }

  async assist(request: AssistanceRequest): Promise<AssistanceResponse> {
    return {
      type: 'iiap.assistance.response',
      iiapVersion: '0.1',
      requestId: request.requestId,
      message: '可以先按预算和使用频率比较，再选择最合适的选项。',
    };
  }
}

class AutoAcceptPresenter implements HelpPresenter {
  async present(
    decision: IIAPDecision,
    context: { packet: IntentContextPacket; decisionId: string },
  ): Promise<FeedbackInteraction> {
    console.log('offer:', context.decisionId, decision.message);
    return 'accepted';
  }
}

const transport = new DemoTransport();
const presenter = new AutoAcceptPresenter();
const adapter = new A2UIV08Adapter({
  resolveAllowedValues: ({ declaredValues }) => declaredValues,
});
const executor = new ValidatedSuggestionExecutor(async (updates: SafeDataModelUpdate[]) => {
  console.log('apply safe data-model update batch:', updates);
});

const runtime = createIIAPRuntime({
  transport,
  presenter,
  policy: {
    minReportIntervalMs: 0,
    thresholds: { optionChangeCount: 1 },
  },
  onAccept: async (decision, context) => {
    if (decision.offerType === 'text_assistance' && decision.helpTopic) {
      const response = await transport.assist({
        type: 'iiap.assistance.request',
        iiapVersion: '0.1',
        requestId: 'assistance-demo-1',
        packetId: context.packet.packetId,
        decisionId: context.decisionId,
        surfaceInstanceId: context.packet.surfaceInstanceId,
        topic: decision.helpTopic,
        language: 'zh-CN',
      });
      console.log('assistance:', response.message);
      return;
    }

    if (decision.offerType === 'update_suggestion' && decision.updateSuggestion) {
      await executor.execute(decision.updateSuggestion, {
        surfaceInstanceId: context.packet.surfaceInstanceId,
        allowedTargets: context.packet.allowedOperations.updateTargets,
        accepted: true,
      });
    }
  },
  onError: (error) => console.error('IIAP error:', error),
});

const session = runtime.createSession({ sessionId: 'session-demo' });
const plan = adapter.buildObservationPlans({
  sessionId: 'session-demo',
  messageId: 'message-demo',
  namespace: 'message-demo',
  messages: [
    {
      surfaceUpdate: {
        surfaceId: 'preferences',
        components: [{
          id: 'meal-type',
          component: {
            MultipleChoice: {
              maxAllowedSelections: 1,
              selections: { path: '/mealType' },
              options: [
                { value: 'vegetarian' },
                { value: 'meat' },
              ],
            },
          },
        }],
      },
    },
    { beginRendering: { surfaceId: 'preferences', root: 'meal-type' } },
  ],
})[0];

if (!plan) throw new Error('No observable A2UI surface');
const handle = session.activate(plan, { focused: true });
handle.observe({
  messageId: 'message-demo',
  surfaceInstanceId: 'message-demo:preferences',
  componentId: 'meal-type',
  eventType: 'change',
  valueToken: 'option-1',
  selectionState: 'selected',
});

await handle.flush();
await new Promise((resolve) => setTimeout(resolve, 0));
session.deactivate('session-ended');
runtime.dispose();
