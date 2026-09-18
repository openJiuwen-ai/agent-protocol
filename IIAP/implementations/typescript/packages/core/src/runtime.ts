import { buildBehaviorPatterns, buildComponentSignal, type ComponentState } from './aggregation.js';
import { systemClock, type Clock } from './clock.js';
import { FeedbackController } from './feedback.js';
import { packetByteSize, validatePrivacy } from './privacy.js';
import { resolvePolicy, type PolicyOverrides, type RuntimePolicy } from './policy.js';
import {
  IIAP_VERSION, type ComponentEvent, type DecisionTransport, type DeactivationReason,
  type HelpPresenter, type IIAPDecisionEnvelope, type IIAPFeedback, type IIAPRuntime,
  type IIAPSession, type IntentContextPacket, type ObservationHandle, type FeedbackInteraction,
  type ObservationPlan, type ObservedEvent, type SessionContext,
} from './types.js';

export interface RuntimeOptions {
  clock?: Clock;
  policy?: PolicyOverrides;
  transport?: DecisionTransport;
  presenter?: HelpPresenter;
  feedbackUpload?: boolean;
  idFactory?: () => string;
  onError?: (error: Error) => void;
  onPacket?: (packet: IntentContextPacket) => Promise<void> | void;
  onAccept?: (decision: IIAPDecisionEnvelope['payload'], context: { packet: IntentContextPacket; decisionId: string }) => Promise<void> | void;
  onEventRejected?: (reason: 'missing_owner' | 'owner_mismatch' | 'unknown_component') => void;
  onDecisionRejected?: (reason: 'invalid_envelope' | 'unknown_packet' | 'owner_mismatch' | 'stale' | 'duplicate_decision') => void;
  onFeedbackRejected?: (reason: 'invalid_feedback' | 'unknown_decision' | 'owner_mismatch' | 'offer_mismatch' | 'duplicate_feedback') => void;
  onFlushSkipped?: (reason: FlushSkipReason, context: FlushSkipContext) => void;
}

export type FlushSkipReason =
  | 'not_focused'
  | 'inactive'
  | 'in_flight'
  | 'feedback_backoff'
  | 'surface_report_limit'
  | 'report_interval'
  | 'no_pattern'
  | 'privacy_rejected'
  | 'packet_too_large';

export interface FlushSkipContext {
  surfaceInstanceId: string;
  automatic: boolean;
}

interface ObservationState {
  plan: ObservationPlan;
  windowStart: number;
  lastEventAt: number;
  lastReportAt: number;
  reportCount: number;
  history: IntentContextPacket['reportHistory'];
  components: Map<string, ComponentState>;
  quietTimer?: ReturnType<typeof setTimeout>;
  idleTimer?: ReturnType<typeof setTimeout>;
  inFlight: boolean;
  active: boolean;
}

interface PendingDecision {
  packet: IntentContextPacket;
  state: ObservationState;
  focusGeneration: number;
}
interface PresentedDecision {
  packet: IntentContextPacket;
  decision: IIAPDecisionEnvelope['payload'];
}

function normalizedEvent(event: ComponentEvent, now: number): Required<ComponentEvent> {
  return {
    componentId: event.componentId, eventType: event.eventType,
    valueToken: event.valueToken ?? '', validityState: event.validityState ?? 'valid',
    optionToken: event.optionToken ?? '', selectionState: event.selectionState ?? 'selected',
    messageId: event.messageId, surfaceInstanceId: event.surfaceInstanceId,
    interactionKind: event.interactionKind ?? '', timestamp: event.timestamp ?? now,
  };
}

function observedEvents(state: ObservationState): ObservedEvent[] {
  return [...state.components.values()]
    .flatMap((component) => component.events)
    .sort((left, right) => left.timestamp - right.timestamp)
    .map((event, index) => ({
      eventId: `event_${index + 1}`,
      sequence: index + 1,
      offsetMs: Math.max(0, event.timestamp - state.windowStart),
      componentId: event.componentId,
      eventType: event.eventType,
      ...(event.valueToken ? { valueToken: event.valueToken } : {}),
      ...(event.optionToken ? { optionToken: event.optionToken } : {}),
      ...(event.optionToken ? { selectionState: event.selectionState } : {}),
      ...(event.validityState === 'invalid' ? { validityState: event.validityState } : {}),
      ...(event.interactionKind ? { interactionKind: event.interactionKind } : {}),
    }));
}

class Session implements IIAPSession {
  private observations = new Map<string, ObservationState>();
  private feedback: FeedbackController;
  private focusedSurfaceInstanceId: string | null = null;
  private focusGeneration = 0;
  private pendingDecisions = new Map<string, PendingDecision>();
  private stalePacketIds = new Set<string>();
  private processedDecisionIds = new Set<string>();
  private presentedDecisions = new Map<string, PresentedDecision>();
  private feedbackDecisionIds = new Set<string>();

  constructor(
    private readonly context: SessionContext,
    private readonly clock: Clock,
    private readonly policy: RuntimePolicy,
    private readonly options: RuntimeOptions,
  ) { this.feedback = new FeedbackController(clock, policy); }

  activate(plan: ObservationPlan, options: { focused?: boolean } = {}): ObservationHandle {
    if (plan.sessionId !== this.context.sessionId) throw new Error('ObservationPlan sessionId mismatch');
    const surface = plan.surface;
    if (!surface.originalSurfaceId || !surface.surfaceInstanceId) throw new Error('ObservationPlan requires a surface');
    if (plan.components.some((component) => component.surfaceId !== surface.originalSurfaceId
      || component.surfaceInstanceId !== surface.surfaceInstanceId)) {
      throw new Error('ObservationPlan components must belong to its surface');
    }
    const previous = this.observations.get(surface.surfaceInstanceId);
    const previousComponentReports = new Map(
      [...(previous?.components.values() ?? [])]
        .map((component) => [component.observation.componentId, component.reportCount]),
    );
    if (previous) {
      this.deactivateState(previous);
      if (this.focusedSurfaceInstanceId === surface.surfaceInstanceId) {
        this.focusedSurfaceInstanceId = null;
      }
    }
    const now = this.clock.now();
    const state: ObservationState = {
      plan, windowStart: now, lastEventAt: now,
      lastReportAt: previous?.lastReportAt ?? Number.NEGATIVE_INFINITY,
      reportCount: previous?.reportCount ?? 0,
      history: previous?.history.slice(-this.policy.reportHistoryLimit) ?? [],
      inFlight: false, active: true,
      components: new Map(plan.components.map((observation) => [observation.componentId, {
        observation, events: [], reportCount: previousComponentReports.get(observation.componentId) ?? 0,
      }])),
    };
    this.observations.set(surface.surfaceInstanceId, state);
    if (options.focused !== false) this.focusState(state);
    return {
      observe: (event) => this.observe(state, event),
      flush: () => this.flush(state),
      deactivate: () => {
        this.deactivateState(state);
        if (this.observations.get(surface.surfaceInstanceId) === state) {
          this.observations.delete(surface.surfaceInstanceId);
          if (this.focusedSurfaceInstanceId === surface.surfaceInstanceId) {
            this.focusedSurfaceInstanceId = null;
          }
        }
      },
    };
  }

  focus(surfaceInstanceId: string): void {
    const state = this.observations.get(surfaceInstanceId);
    if (state) this.focusState(state);
  }

  suspend(): void {
    const focused = this.focusedSurfaceInstanceId
      ? this.observations.get(this.focusedSurfaceInstanceId)
      : undefined;
    if (focused) {
      this.clearTimers(focused);
      for (const component of focused.components.values()) component.events = [];
      this.invalidatePendingForState(focused);
      this.invalidatePresentedForSurface(focused.plan.surface.surfaceInstanceId);
    }
    this.focusGeneration += 1;
    this.focusedSurfaceInstanceId = null;
  }

  handleDecision(decision: IIAPDecisionEnvelope): boolean {
    if (!this.isValidDecisionEnvelope(decision)) {
      this.options.onDecisionRejected?.('invalid_envelope');
      return false;
    }
    if (this.processedDecisionIds.has(decision.decisionId)) {
      this.options.onDecisionRejected?.('duplicate_decision');
      return false;
    }
    const pending = this.pendingDecisions.get(decision.packetId);
    if (!pending) {
      this.options.onDecisionRejected?.(this.stalePacketIds.has(decision.packetId) ? 'stale' : 'unknown_packet');
      return false;
    }
    const { packet, state } = pending;
    if (decision.surfaceInstanceId !== packet.surfaceInstanceId) {
      this.options.onDecisionRejected?.('owner_mismatch');
      return false;
    }
    if (!state.active || pending.focusGeneration !== this.focusGeneration
      || this.focusedSurfaceInstanceId !== packet.surfaceInstanceId) {
      this.invalidatePending(decision.packetId);
      this.options.onDecisionRejected?.('stale');
      return false;
    }
    this.pendingDecisions.delete(packet.packetId);
    this.rememberProcessedDecision(decision.decisionId);
    state.inFlight = false;
    const hasPresentedForSurface = [...this.presentedDecisions.values()]
      .some((presented) => presented.packet.surfaceInstanceId === packet.surfaceInstanceId);
    if (hasPresentedForSurface) this.invalidatePresentedForSurface(packet.surfaceInstanceId);
    if (decision.payload.decision !== 'offer_help' || !this.options.presenter
      || decision.payload.offerType === 'none') return true;
    this.presentedDecisions.set(decision.decisionId, { packet, decision: decision.payload });
    void this.options.presenter.present(decision.payload, { packet, decisionId: decision.decisionId })
      .then((interaction) => this.handlePresenterInteraction(decision.decisionId, interaction))
      .catch((error: unknown) => this.options.onError?.(error instanceof Error ? error : new Error(String(error))));
    return true;
  }

  cancelDecision(packetId: string): boolean {
    const pending = this.pendingDecisions.get(packetId);
    if (!pending) return false;
    this.invalidatePending(packetId);
    pending.state.inFlight = false;
    return true;
  }

  recordFeedback(value: IIAPFeedback): boolean {
    if (!this.isValidFeedback(value)) {
      this.options.onFeedbackRejected?.('invalid_feedback');
      return false;
    }
    if (this.feedbackDecisionIds.has(value.decisionId)) {
      this.options.onFeedbackRejected?.('duplicate_feedback');
      return false;
    }
    const presented = this.presentedDecisions.get(value.decisionId);
    if (!presented) {
      this.options.onFeedbackRejected?.('unknown_decision');
      return false;
    }
    if (presented.packet.packetId !== value.packetId
      || presented.packet.surfaceInstanceId !== value.surfaceInstanceId) {
      this.options.onFeedbackRejected?.('owner_mismatch');
      return false;
    }
    if (presented.decision.offerType !== value.offerType) {
      this.options.onFeedbackRejected?.('offer_mismatch');
      return false;
    }
    this.presentedDecisions.delete(value.decisionId);
    this.rememberBounded(this.feedbackDecisionIds, value.decisionId);
    this.feedback.record(value.interaction, value.outcome);
    const state = this.observations.get(value.surfaceInstanceId);
    if (state) state.inFlight = false;
    if (this.options.feedbackUpload && this.options.transport?.sendFeedback) {
      void this.options.transport.sendFeedback(value)
        .catch((error: unknown) => this.options.onError?.(error instanceof Error ? error : new Error(String(error))));
    }
    return true;
  }

  deactivate(_reason: DeactivationReason): void {
    for (const state of this.observations.values()) this.deactivateState(state);
    this.observations.clear();
    this.pendingDecisions.clear();
    this.presentedDecisions.clear();
    this.feedbackDecisionIds.clear();
    this.stalePacketIds.clear();
    this.focusedSurfaceInstanceId = null;
  }

  private observe(state: ObservationState, event: ComponentEvent): void {
    if (!state.active || !validatePrivacy(event)) return;
    if (typeof event.messageId !== 'string' || !event.messageId
      || typeof event.surfaceInstanceId !== 'string' || !event.surfaceInstanceId) {
      this.options.onEventRejected?.('missing_owner');
      return;
    }
    if (event.messageId !== state.plan.messageId
      || event.surfaceInstanceId !== state.plan.surface.surfaceInstanceId) {
      this.options.onEventRejected?.('owner_mismatch');
      return;
    }
    const component = state.components.get(event.componentId);
    if (!component) {
      this.options.onEventRejected?.('unknown_component');
      return;
    }
    if (component.reportCount >= this.policy.maxReportsPerComponent) return;
    this.focusState(state);
    component.events.push(normalizedEvent(event, this.clock.now()));
    state.lastEventAt = this.clock.now();
    if (state.quietTimer) this.clock.clearTimeout(state.quietTimer);
    state.quietTimer = this.clock.setTimeout(() => void this.flush(state, true), this.policy.componentQuietMs);
    this.scheduleIdle(state);
  }

  private scheduleIdle(state: ObservationState): void {
    if (state.idleTimer) this.clock.clearTimeout(state.idleTimer);
    state.idleTimer = this.clock.setTimeout(() => void this.flush(state, true), this.policy.surfaceIdleMs);
  }

  private async flush(state: ObservationState, automatic = false): Promise<IntentContextPacket | null> {
    if (automatic && this.focusedSurfaceInstanceId !== state.plan.surface.surfaceInstanceId) {
      return this.skipFlush(state, automatic, 'not_focused');
    }
    if (!state.active) return this.skipFlush(state, automatic, 'inactive');
    if (state.inFlight) return this.skipFlush(state, automatic, 'in_flight');
    if (!this.feedback.canOffer()) return this.skipFlush(state, automatic, 'feedback_backoff');
    const now = this.clock.now();
    if (state.reportCount >= this.policy.maxReportsPerSurface) {
      return this.skipFlush(state, automatic, 'surface_report_limit');
    }
    if (now - state.lastReportAt < this.policy.minReportIntervalMs) {
      return this.skipFlush(state, automatic, 'report_interval');
    }
    const signals = [...state.components.values()]
      .filter((component) => component.reportCount < this.policy.maxReportsPerComponent)
      .map((component) => buildComponentSignal(component, this.policy.thresholds))
      .filter((item): item is NonNullable<typeof item> => item !== null);
    // 事件数组必须与 packet schema 的 maxItems 对齐；超出时保留最近事实并如实上报丢弃数量。
    // 先裁剪再构建 pattern，保证 basedOnEvents 不会引用已被丢弃的 eventId。
    const allEvents = observedEvents(state);
    const droppedEventCount = Math.max(0, allEvents.length - this.policy.maxEventsPerPacket);
    const events = droppedEventCount > 0 ? allEvents.slice(droppedEventCount) : allEvents;
    const retainedEventIds = new Set(events.map((event) => event.eventId));
    const patterns = buildBehaviorPatterns(signals, events)
      .filter((item) => item.basedOnEvents.every((eventId) => retainedEventIds.has(eventId)));
    const surface = state.plan.surface;
    if (patterns.length === 0) return this.skipFlush(state, automatic, 'no_pattern');
    const timestamp = new Date(now).toISOString();
    const packet: IntentContextPacket = {
      iiapVersion: IIAP_VERSION,
      packetId: this.options.idFactory?.() ?? `pkt_${now}_${Math.random().toString(36).slice(2, 8)}`,
      sessionId: state.plan.sessionId, messageId: state.plan.messageId,
      originalSurfaceId: surface.originalSurfaceId, surfaceInstanceId: surface.surfaceInstanceId,
      protocolVersion: state.plan.protocolVersion, timestamp,
      window: { startTime: new Date(state.windowStart).toISOString(), endTime: timestamp, durationMs: Math.max(0, now - state.windowStart) },
      surfaceContext: state.plan.surfaceContext,
      observations: {
        tokenScope: 'component_within_surface_instance',
        events,
        completeness: { complete: droppedEventCount === 0, droppedEventCount },
      },
      patterns,
      reportHistory: state.history.slice(-this.policy.reportHistoryLimit),
      allowedOperations: {
        // 授权由 Adapter 的 SuggestionPolicy 决定（bindingPath + allowedValues 齐全即已授权）。
        // 选择基数只影响值的形状校验，不在这里二次收窄。
        updateTargets: state.plan.components.filter((component) => component.bindingPath && component.allowedValues?.length)
          .map((component) => ({
            componentId: component.componentId, componentType: component.componentType,
            originalSurfaceId: component.surfaceId, bindingPath: component.bindingPath!,
            allowedValues: component.allowedValues!,
            // selectionMode 决定标量/集合形状，必须独立保留；maxAllowedSelections 只是可选上限。
            ...(component.selectionMode ? { selectionMode: component.selectionMode } : {}),
            ...(component.maxAllowedSelections !== undefined
              ? { maxAllowedSelections: component.maxAllowedSelections }
              : {}),
          })).slice(0, 8),
      },
    };
    if (!validatePrivacy(packet)) return this.skipFlush(state, automatic, 'privacy_rejected');
    if (packetByteSize(packet) > this.policy.packetMaxBytes) {
      return this.skipFlush(state, automatic, 'packet_too_large');
    }
    state.inFlight = true;
    state.lastReportAt = now;
    state.reportCount += 1;
    for (const component of state.components.values()) {
      if (signals.some((item) => item.componentId === component.observation.componentId)) component.reportCount += 1;
      component.events = [];
    }
    state.history = [...state.history, {
      packetId: packet.packetId, timestamp, window: packet.window,
      observations: packet.observations, patterns,
    }]
      .slice(-this.policy.reportHistoryLimit);
    state.windowStart = now;
    this.pendingDecisions.set(packet.packetId, {
      packet, state, focusGeneration: this.focusGeneration,
    });
    if (this.options.onPacket) {
      try {
        await this.options.onPacket(packet);
      }
      catch (error) {
        if (this.pendingDecisions.delete(packet.packetId)) state.inFlight = false;
        this.options.onError?.(error instanceof Error ? error : new Error(String(error)));
        return packet;
      }
    }
    if (this.options.transport) {
      try {
        const accepted = this.handleDecision(await this.options.transport.decide(packet));
        if (!accepted && this.pendingDecisions.has(packet.packetId)) {
          this.pendingDecisions.delete(packet.packetId);
          state.inFlight = false;
        }
      }
      catch (error) {
        if (this.pendingDecisions.delete(packet.packetId)) state.inFlight = false;
        this.options.onError?.(error instanceof Error ? error : new Error(String(error)));
      }
    }
    return packet;
  }

  private skipFlush(
    state: ObservationState,
    automatic: boolean,
    reason: FlushSkipReason,
  ): null {
    this.options.onFlushSkipped?.(reason, {
      surfaceInstanceId: state.plan.surface.surfaceInstanceId,
      automatic,
    });
    return null;
  }

  private focusState(state: ObservationState): void {
    const surfaceInstanceId = state.plan.surface.surfaceInstanceId;
    if (this.focusedSurfaceInstanceId === surfaceInstanceId) return;
    const previous = this.focusedSurfaceInstanceId
      ? this.observations.get(this.focusedSurfaceInstanceId)
      : undefined;
    if (previous) {
      this.clearTimers(previous);
      for (const component of previous.components.values()) component.events = [];
      this.invalidatePendingForState(previous);
      this.invalidatePresentedForSurface(previous.plan.surface.surfaceInstanceId);
    }
    this.focusGeneration += 1;
    this.focusedSurfaceInstanceId = surfaceInstanceId;
    const now = this.clock.now();
    state.windowStart = now;
    state.lastEventAt = now;
    this.scheduleIdle(state);
  }

  private clearTimers(state: ObservationState): void {
    if (state.quietTimer) this.clock.clearTimeout(state.quietTimer);
    if (state.idleTimer) this.clock.clearTimeout(state.idleTimer);
    state.quietTimer = undefined;
    state.idleTimer = undefined;
  }

  private deactivateState(state: ObservationState): void {
    state.active = false;
    this.clearTimers(state);
    this.invalidatePendingForState(state);
    this.invalidatePresentedForSurface(state.plan.surface.surfaceInstanceId);
    state.components.clear();
    state.inFlight = false;
  }

  private invalidatePendingForState(state: ObservationState): void {
    for (const [packetId, pending] of this.pendingDecisions) {
      if (pending.state === state) this.invalidatePending(packetId);
    }
    state.inFlight = false;
  }

  private invalidatePending(packetId: string): void {
    if (!this.pendingDecisions.delete(packetId)) return;
    this.rememberBounded(this.stalePacketIds, packetId);
  }

  private invalidatePresentedForSurface(surfaceInstanceId: string): void {
    for (const [decisionId, presented] of this.presentedDecisions) {
      if (presented.packet.surfaceInstanceId === surfaceInstanceId) this.presentedDecisions.delete(decisionId);
    }
    this.options.presenter?.dismiss?.(surfaceInstanceId);
  }

  private rememberProcessedDecision(decisionId: string): void {
    this.rememberBounded(this.processedDecisionIds, decisionId);
  }

  private async handlePresenterInteraction(decisionId: string, interaction: FeedbackInteraction): Promise<void> {
    const presented = this.presentedDecisions.get(decisionId);
    if (!presented) return;
    let outcome: IIAPFeedback['outcome'];
    let reasonCode: string | undefined;
    if (interaction === 'accepted') {
      try {
        if (!this.options.onAccept) throw new Error('ACCEPT_HANDLER_MISSING');
        await this.options.onAccept(presented.decision, { packet: presented.packet, decisionId });
        outcome = 'succeeded';
      } catch (error) {
        outcome = 'failed';
        reasonCode = error instanceof Error && error.message === 'ACCEPT_HANDLER_MISSING'
          ? 'accept_handler_missing'
          : error instanceof Error && error.message === 'ASSISTANCE_UNSAFE_OUTPUT'
            ? 'assistance_unsafe_output'
            : 'execution_failed';
        this.options.onError?.(error instanceof Error ? error : new Error(String(error)));
      }
    }
    this.recordFeedback({
      type: 'iiap.feedback', iiapVersion: IIAP_VERSION,
      packetId: presented.packet.packetId, decisionId,
      surfaceInstanceId: presented.packet.surfaceInstanceId,
      offerType: presented.decision.offerType as IIAPFeedback['offerType'], interaction,
      ...(outcome ? { outcome } : {}), ...(reasonCode ? { reasonCode } : {}),
      timestamp: new Date(this.clock.now()).toISOString(),
    });
  }

  private rememberBounded(values: Set<string>, value: string): void {
    values.add(value);
    if (values.size > 128) {
      const oldest = values.values().next().value;
      if (oldest) values.delete(oldest);
    }
  }

  private isValidDecisionEnvelope(value: IIAPDecisionEnvelope): boolean {
    const structurallyValid = Boolean(value) && value.type === 'iiap.decision' && value.iiapVersion === IIAP_VERSION
      && typeof value.decisionId === 'string' && Boolean(value.decisionId)
      && typeof value.packetId === 'string' && Boolean(value.packetId)
      && typeof value.surfaceInstanceId === 'string' && Boolean(value.surfaceInstanceId)
      && Boolean(value.payload) && ['offer_help', 'no_intervention', 'defer'].includes(value.payload.decision)
      && typeof value.payload.reason === 'string' && value.payload.reason.length <= 512
      && typeof value.payload.message === 'string' && value.payload.message.length <= 2048
      && ['update_suggestion', 'text_assistance', 'none'].includes(value.payload.offerType)
      && ['inline_card', 'none'].includes(value.payload.uiStyle);
    if (!structurallyValid) return false;
    const payload = value.payload;
    if (payload.decision !== 'offer_help') {
      return payload.offerType === 'none' && payload.uiStyle === 'none'
        && payload.helpTopic === undefined && payload.updateSuggestion === undefined;
    }
    if (payload.uiStyle === 'none') return false;
    if (payload.offerType === 'text_assistance') {
      return ['compare_options', 'explain_rules', 'fix_block', 'save_progress'].includes(payload.helpTopic ?? '')
        && payload.updateSuggestion === undefined;
    }
    return payload.offerType === 'update_suggestion'
      && payload.helpTopic === undefined
      && Boolean(payload.updateSuggestion)
      && payload.updateSuggestion?.kind === 'data_model_update'
      && Array.isArray(payload.updateSuggestion.updates)
      && payload.updateSuggestion.updates.length >= 1
      && payload.updateSuggestion.updates.length <= 8;
  }

  private isValidFeedback(value: IIAPFeedback): boolean {
    if (!value || value.type !== 'iiap.feedback' || value.iiapVersion !== IIAP_VERSION
      || !value.packetId || !value.decisionId || !value.surfaceInstanceId
      || !['text_assistance', 'update_suggestion'].includes(value.offerType)
      || !['accepted', 'dismissed', 'rejected', 'ignored', 'timed_out'].includes(value.interaction)
      || (value.reasonCode !== undefined && !/^[a-z0-9][a-z0-9_.-]{0,63}$/.test(value.reasonCode))) return false;
    return value.interaction === 'accepted'
      ? value.outcome === 'succeeded' || value.outcome === 'failed'
      : value.outcome === undefined;
  }
}

export function createIIAPRuntime(options: RuntimeOptions = {}): IIAPRuntime {
  const clock = options.clock ?? systemClock;
  const policy = resolvePolicy(options.policy);
  const sessions = new Map<string, Session>();
  let disposed = false;
  return {
    createSession(context) {
      if (disposed) throw new Error('IIAPRuntime is disposed');
      const previous = sessions.get(context.sessionId);
      if (previous) previous.deactivate('session-ended');
      const session = new Session(context, clock, policy, options);
      sessions.set(context.sessionId, session);
      return session;
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      for (const session of sessions.values()) session.deactivate('runtime-disposed');
      sessions.clear();
    },
  };
}
