import type { BehaviorPattern, CandidateSignal, ComponentEvent, ComponentObservation, ObservedEvent } from './types.js';
import type { RuntimePolicy } from './policy.js';

export interface ComponentState {
  observation: ComponentObservation;
  events: Required<ComponentEvent>[];
  reportCount: number;
}

function pattern(tokens: string[]): string {
  const labels = new Map<string, string>();
  return tokens.map((token) => {
    if (!labels.has(token)) labels.set(token, String.fromCharCode(65 + labels.size));
    return labels.get(token)!;
  }).join('-');
}

function signal(state: ComponentState, signalType: string, metrics: CandidateSignal['metrics']): CandidateSignal {
  const { observation } = state;
  return {
    signalId: `${observation.componentId}.${signalType}`,
    componentId: observation.componentId,
    componentType: observation.componentType,
    componentRole: observation.componentRole,
    signalType,
    metrics,
    evidence: [`${observation.componentId} produced ${signalType}`],
  };
}

function multipleSelectionReversals(events: Required<ComponentEvent>[]): number {
  const previousStates = new Map<string, ComponentEvent['selectionState']>();
  let reversals = 0;
  for (const event of events) {
    if (event.eventType !== 'change' || !event.optionToken) continue;
    const previous = previousStates.get(event.optionToken);
    if (previous && previous !== event.selectionState) reversals += 1;
    previousStates.set(event.optionToken, event.selectionState);
  }
  return reversals;
}

export function buildComponentSignal(
  state: ComponentState,
  thresholds: RuntimePolicy['thresholds'],
): CandidateSignal | null {
  const changes = state.events.filter((event) => event.eventType === 'change' && event.valueToken);
  const tokens = changes.map((event) => event.valueToken);
  switch (state.observation.capability) {
    case 'temporal_edit':
      return changes.length >= thresholds.temporalChangeCount
        ? signal(state, 'datetime_churn', { changeCount: changes.length, switchPattern: pattern(tokens), finalEqualsInitial: tokens[0] === tokens.at(-1) }) : null;
    case 'option_select':
      if (state.observation.selectionMode === 'multiple') {
        const reversalCount = multipleSelectionReversals(state.events);
        const optionTokens = state.events
          .filter((event) => event.eventType === 'change' && event.optionToken)
          .map((event) => event.optionToken);
        return reversalCount >= thresholds.optionChangeCount
          ? signal(state, 'choice_churn', {
            changeCount: changes.length,
            distinctOptionCount: new Set(optionTokens).size,
            reversalCount,
          }) : null;
      }
      return changes.length >= thresholds.optionChangeCount
        ? signal(state, 'choice_churn', { changeCount: changes.length, distinctOptionCount: new Set(tokens).size, switchPattern: pattern(tokens) }) : null;
    case 'boolean_toggle':
      return changes.length >= thresholds.booleanToggleCount ? signal(state, 'toggle_churn', { toggleCount: changes.length }) : null;
    case 'scalar_adjust':
      return changes.length >= thresholds.scalarChangeCount ? signal(state, 'scalar_churn', { changeCount: changes.length, switchPattern: pattern(tokens) }) : null;
    case 'text_edit': {
      const invalid = state.events.filter((event) => event.eventType === 'validation_error' || event.validityState === 'invalid');
      return invalid.length >= thresholds.textValidationFailureCount ? signal(state, 'validation_block', { validationFailCount: invalid.length }) : null;
    }
    case 'overlay_reveal': {
      const count = state.events.filter((event) => event.eventType === 'open').length;
      return count >= thresholds.overlayOpenCount ? signal(state, 'rule_inspection', { openCount: count }) : null;
    }
    case 'content_navigate': {
      const count = state.events.filter((event) => event.eventType === 'tab_change').length;
      return count >= thresholds.navigationChangeCount ? signal(state, 'detail_revisit', { tabSwitchCount: count }) : null;
    }
    case 'media_control': {
      const count = state.events.filter((event) => ['play', 'pause', 'seek'].includes(event.eventType)).length;
      return count >= thresholds.mediaControlCount ? signal(state, 'media_revisit', { controlCount: count }) : null;
    }
    case 'viewport_navigate': {
      const scrolls = state.events.filter((event) => event.eventType === 'scroll' && event.valueToken);
      return scrolls.length >= thresholds.viewportChangeCount
        ? signal(state, 'navigation_revisit', { scrollCount: scrolls.length, switchPattern: pattern(scrolls.map((event) => event.valueToken)) }) : null;
    }
    default:
      return null;
  }
}

export function buildBehaviorPatterns(
  signals: CandidateSignal[],
  events: ObservedEvent[],
): BehaviorPattern[] {
  const typeMap: Record<string, BehaviorPattern['type']> = {
    datetime_churn: 'state_alternation',
    choice_churn: 'selection_reversal',
    toggle_churn: 'state_alternation',
    scalar_churn: 'state_alternation',
    validation_block: 'validation_failure_sequence',
    rule_inspection: 'repeated_open',
    detail_revisit: 'repeated_navigation',
    media_revisit: 'repeated_media_control',
    navigation_revisit: 'repeated_viewport_navigation',
  };
  return signals.map((item) => ({
    patternId: `pattern.${item.signalId}`,
    type: typeMap[item.signalType] ?? 'repeated_change',
    componentId: item.componentId,
    basedOnEvents: events.filter((event) => event.componentId === item.componentId).map((event) => event.eventId),
    metrics: item.metrics,
  }));
}
