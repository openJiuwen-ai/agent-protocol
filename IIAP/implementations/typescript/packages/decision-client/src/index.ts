import type {
  IIAPDecision, IIAPDecisionEnvelope, IntentContextPacket,
} from '@openjiuwen/iiap';
import { coerceModelObject, validateDataModelSuggestion } from '@openjiuwen/iiap';

const NO_INTERVENTION: IIAPDecision = {
  decision: 'no_intervention', reason: 'invalid_or_insufficient_iiap_decision',
  offerType: 'none', uiStyle: 'none', message: '',
};
const DECISIONS = new Set(['offer_help', 'no_intervention', 'defer']);
const OFFERS = new Set(['update_suggestion', 'text_assistance']);
const TOPICS = new Set(['compare_options', 'explain_rules', 'fix_block', 'save_progress']);
const offerStyle = (offerType: IIAPDecision['offerType']): IIAPDecision['uiStyle'] => (
  offerType === 'none' ? 'none' : 'inline_card'
);

function record(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function boundedText(value: unknown, maxLength: number): string | null {
  return typeof value === 'string' && value.length <= maxLength ? value : null;
}

function inferHelpTopic(packet?: IntentContextPacket): NonNullable<IIAPDecision['helpTopic']> {
  if (packet?.patterns.some((pattern) => pattern.type === 'validation_failure_sequence')) return 'fix_block';
  return packet?.patterns.some((pattern) => pattern.type === 'selection_reversal')
    ? 'compare_options'
    : 'explain_rules';
}

export function parseDecision(value: string | unknown, packet?: IntentContextPacket): IIAPDecision {
  const parsed: unknown = coerceModelObject(value);
  if (!record(parsed) || !DECISIONS.has(String(parsed.decision))) return { ...NO_INTERVENTION };
  const reason = boundedText(parsed.reason, 1024);
  const message = boundedText(parsed.message, 2048);
  if (reason === null || message === null) return { ...NO_INTERVENTION };
  const decision = parsed.decision as IIAPDecision['decision'];
  if (decision !== 'offer_help') {
    return {
      decision, reason,
      offerType: 'none', uiStyle: 'none',
      message,
    };
  }
  if (!OFFERS.has(String(parsed.offerType))) {
    return { ...NO_INTERVENTION };
  }
  const offerType = parsed.offerType as Extract<IIAPDecision['offerType'], 'text_assistance' | 'update_suggestion'>;
  const uiStyle = offerStyle(offerType) as Exclude<IIAPDecision['uiStyle'], 'none'>;
  const result: IIAPDecision = {
    decision, reason, offerType, uiStyle, message,
  };
  const declaredTopic = TOPICS.has(String(parsed.helpTopic))
    ? parsed.helpTopic as NonNullable<IIAPDecision['helpTopic']>
    : undefined;
  if (offerType === 'update_suggestion') {
    const safe = packet && record(parsed.updateSuggestion) ? validateDataModelSuggestion(parsed.updateSuggestion, {
      accepted: true,
      surfaceInstanceId: packet.surfaceInstanceId,
      allowedTargets: packet.allowedOperations.updateTargets,
    }) : null;
    if (safe) result.updateSuggestion = safe;
    else result.offerType = 'text_assistance';
  }
  if (result.offerType === 'text_assistance') {
    result.helpTopic = declaredTopic ?? inferHelpTopic(packet);
  }
  result.uiStyle = offerStyle(result.offerType) as IIAPDecision['uiStyle'];
  return result;
}

export function parseDecisionEnvelope(
  value: string | unknown,
  packet?: IntentContextPacket,
): IIAPDecisionEnvelope | null {
  const parsed: unknown = coerceModelObject(value);
  if (!record(parsed) || parsed.type !== 'iiap.decision' || parsed.iiapVersion !== '0.1'
    || typeof parsed.decisionId !== 'string' || !parsed.decisionId
    || typeof parsed.packetId !== 'string' || !parsed.packetId
    || typeof parsed.surfaceInstanceId !== 'string' || !parsed.surfaceInstanceId
    || !record(parsed.payload)) return null;
  if (packet && (parsed.packetId !== packet.packetId
    || parsed.surfaceInstanceId !== packet.surfaceInstanceId)) return null;
  return {
    type: 'iiap.decision', iiapVersion: '0.1',
    decisionId: parsed.decisionId, packetId: parsed.packetId,
    surfaceInstanceId: parsed.surfaceInstanceId,
    payload: parseDecision(parsed.payload, packet),
  };
}
