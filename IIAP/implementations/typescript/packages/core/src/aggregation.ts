import type { BehaviorPattern, ComponentEvent, ComponentObservation, ObservedEvent, Scalar } from './types.js';
import type { RuntimePolicy } from './policy.js';

export interface ComponentState {
  observation: ComponentObservation;
  events: Required<ComponentEvent>[];
  reportCount: number;
}

interface PatternCandidate {
  signalType: string;
  type: BehaviorPattern['type'];
  metrics: Record<string, Scalar>;
}

function tokenPattern(tokens: string[]): string {
  const labels = new Map<string, string>();
  return tokens.map((token) => {
    if (!labels.has(token)) labels.set(token, String.fromCharCode(65 + labels.size));
    return labels.get(token)!;
  }).join('-');
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

function candidateFor(
  state: ComponentState,
  thresholds: RuntimePolicy['thresholds'],
): PatternCandidate | null {
  const changes = state.events.filter((event) => event.eventType === 'change' && event.valueToken);
  const tokens = changes.map((event) => event.valueToken);
  switch (state.observation.capability) {
    case 'temporal_edit':
      return changes.length >= thresholds.temporalChangeCount
        ? { signalType: 'datetime_churn', type: 'state_alternation', metrics: { changeCount: changes.length, switchPattern: tokenPattern(tokens), finalEqualsInitial: tokens[0] === tokens.at(-1) } }
        : null;
    case 'option_select': {
      if (state.observation.selectionMode === 'multiple') {
        const reversalCount = multipleSelectionReversals(state.events);
        const optionTokens = state.events
          .filter((event) => event.eventType === 'change' && event.optionToken)
          .map((event) => event.optionToken);
        return reversalCount >= thresholds.optionChangeCount
          ? { signalType: 'choice_churn', type: 'selection_reversal', metrics: { changeCount: changes.length, distinctOptionCount: new Set(optionTokens).size, reversalCount } }
          : null;
      }
      return changes.length >= thresholds.optionChangeCount
        ? { signalType: 'choice_churn', type: 'selection_reversal', metrics: { changeCount: changes.length, distinctOptionCount: new Set(tokens).size, switchPattern: tokenPattern(tokens) } }
        : null;
    }
    case 'boolean_toggle':
      return changes.length >= thresholds.booleanToggleCount
        ? { signalType: 'toggle_churn', type: 'state_alternation', metrics: { toggleCount: changes.length } }
        : null;
    case 'scalar_adjust':
      return changes.length >= thresholds.scalarChangeCount
        ? { signalType: 'scalar_churn', type: 'state_alternation', metrics: { changeCount: changes.length, switchPattern: tokenPattern(tokens) } }
        : null;
    case 'text_edit': {
      const invalid = state.events.filter((event) => event.eventType === 'validation_error' || event.validityState === 'invalid');
      return invalid.length >= thresholds.textValidationFailureCount
        ? { signalType: 'validation_block', type: 'validation_failure_sequence', metrics: { validationFailCount: invalid.length } }
        : null;
    }
    case 'overlay_reveal': {
      const count = state.events.filter((event) => event.eventType === 'open').length;
      return count >= thresholds.overlayOpenCount ? { signalType: 'rule_inspection', type: 'repeated_open', metrics: { openCount: count } } : null;
    }
    case 'content_navigate': {
      const count = state.events.filter((event) => event.eventType === 'tab_change').length;
      return count >= thresholds.navigationChangeCount ? { signalType: 'detail_revisit', type: 'repeated_navigation', metrics: { tabSwitchCount: count } } : null;
    }
    case 'media_control': {
      const count = state.events.filter((event) => ['play', 'pause', 'seek'].includes(event.eventType)).length;
      return count >= thresholds.mediaControlCount ? { signalType: 'media_revisit', type: 'repeated_media_control', metrics: { controlCount: count } } : null;
    }
    case 'viewport_navigate': {
      const scrolls = state.events.filter((event) => event.eventType === 'scroll' && event.valueToken);
      return scrolls.length >= thresholds.viewportChangeCount
        ? { signalType: 'navigation_revisit', type: 'repeated_viewport_navigation', metrics: { scrollCount: scrolls.length, switchPattern: tokenPattern(scrolls.map((event) => event.valueToken)) } }
        : null;
    }
    default:
      return null;
  }
}

export function buildBehaviorPatterns(
  states: ComponentState[],
  events: ObservedEvent[],
  thresholds: RuntimePolicy['thresholds'],
): BehaviorPattern[] {
  return states.flatMap((state) => {
    const candidate = candidateFor(state, thresholds);
    if (!candidate) return [];
    const componentId = state.observation.componentId;
    return [{
      patternId: `pattern.${componentId}.${candidate.signalType}`,
      type: candidate.type,
      componentId,
      basedOnEvents: events.filter((event) => event.componentId === componentId).map((event) => event.eventId),
      metrics: candidate.metrics,
    }];
  });
}
