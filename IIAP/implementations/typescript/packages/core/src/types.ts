export const IIAP_VERSION = '0.1' as const;
export type ProtocolVersion = '0.8' | '0.9.1';
export type Scalar = string | number | boolean;
export type UpdateValue = Scalar | null;

export type InteractionCapability =
  | 'text_edit' | 'boolean_toggle' | 'scalar_adjust' | 'temporal_edit'
  | 'option_select' | 'action_invoke' | 'content_navigate'
  | 'overlay_reveal' | 'media_control' | 'viewport_navigate';

export type ComponentEventType =
  | 'change' | 'validation_error' | 'open' | 'close' | 'tab_change'
  | 'play' | 'pause' | 'seek' | 'scroll' | 'explicit_action';

export interface ComponentObservation {
  componentId: string;
  componentType: string;
  capability: InteractionCapability;
  componentRole: string;
  surfaceId: string;
  surfaceInstanceId: string;
  selectionMode?: 'single' | 'multiple';
  maxAllowedSelections?: number;
  bindingPath?: string;
  allowedValues?: UpdateValue[];
}

export interface ObservationPlan {
  sessionId: string;
  messageId: string;
  protocolVersion: ProtocolVersion;
  surface: { originalSurfaceId: string; surfaceInstanceId: string };
  surfaceContext: SurfaceContext;
  components: ComponentObservation[];
  explicitActionBoundaries: Array<{
    componentId: string; actionName: string; surfaceId: string; surfaceInstanceId: string;
  }>;
}

export interface ComponentEvent {
  componentId: string;
  eventType: ComponentEventType;
  valueToken?: string;
  optionToken?: string;
  selectionState?: 'selected' | 'cleared';
  messageId: string;
  surfaceInstanceId: string;
  validityState?: 'valid' | 'invalid';
  interactionKind?: string;
  timestamp?: number;
}

export interface ObservedEvent {
  eventId: string;
  sequence: number;
  offsetMs: number;
  componentId: string;
  eventType: ComponentEventType;
  valueToken?: string;
  optionToken?: string;
  selectionState?: 'selected' | 'cleared';
  validityState?: 'valid' | 'invalid';
  interactionKind?: string;
}
export interface BehaviorPattern {
  patternId: string;
  type: 'state_alternation' | 'repeated_change' | 'selection_reversal'
    | 'validation_failure_sequence' | 'repeated_open' | 'repeated_navigation'
    | 'repeated_media_control' | 'repeated_viewport_navigation';
  componentId: string;
  basedOnEvents: string[];
  metrics: Record<string, Scalar>;
}
export interface SurfaceContext {
  protocol: 'a2ui';
  protocolVersion: ProtocolVersion;
  snapshotType: 'sanitized_effective_definition';
  definition: Array<Record<string, unknown>>;
  redaction: {
    dataModelExcluded: true;
    actionContextValuesExcluded: true;
    unreachableComponentsExcluded: boolean;
    unknownCustomPropertiesExcluded: boolean;
    truncated: boolean;
  };
}
export interface AllowedUpdateTarget {
  componentId: string; componentType: string; originalSurfaceId: string;
  bindingPath: string; allowedValues: UpdateValue[];
  selectionMode?: 'single' | 'multiple';
  maxAllowedSelections?: number;
}
export interface PacketWindow { startTime: string; endTime: string; durationMs: number }
export interface ReportHistoryEntry {
  packetId: string; timestamp: string; window: PacketWindow;
  observations: { tokenScope: 'component_within_surface_instance'; events: ObservedEvent[]; completeness: ObservationCompleteness };
  patterns: BehaviorPattern[];
}
export interface ObservationCompleteness { complete: boolean; droppedEventCount: number }

export interface IntentContextPacket {
  iiapVersion: typeof IIAP_VERSION;
  packetId: string;
  sessionId: string;
  messageId: string;
  originalSurfaceId: string;
  surfaceInstanceId: string;
  protocolVersion: ProtocolVersion;
  timestamp: string;
  window: PacketWindow;
  surfaceContext: SurfaceContext;
  observations: {
    tokenScope: 'component_within_surface_instance';
    events: ObservedEvent[];
    completeness: ObservationCompleteness;
  };
  patterns: BehaviorPattern[];
  reportHistory: ReportHistoryEntry[];
  allowedOperations: { updateTargets: AllowedUpdateTarget[] };
}

export interface IntentContextPacketEnvelope {
  type: 'iiap.intent_context_packet';
  iiapVersion: typeof IIAP_VERSION;
  packet: IntentContextPacket;
}

export type FeedbackInteraction = 'accepted' | 'dismissed' | 'rejected' | 'ignored' | 'timed_out';
export type FeedbackOutcome = 'succeeded' | 'failed';
export type FeedbackBackoffCategory = FeedbackInteraction | 'execution_failed';
export interface IIAPFeedback {
  type: 'iiap.feedback'; iiapVersion: typeof IIAP_VERSION;
  packetId: string; decisionId: string; surfaceInstanceId: string;
  offerType: 'text_assistance' | 'update_suggestion';
  interaction: FeedbackInteraction; outcome?: FeedbackOutcome;
  timestamp: string; reasonCode?: string;
}

export interface SafeDataModelUpdate {
  surfaceId: string; surfaceInstanceId?: string; path: string; value: UpdateValue | UpdateValue[];
}
export interface SafeSuggestion<TUpdate = SafeDataModelUpdate> {
  kind: 'data_model_update'; updates: TUpdate[];
}
export interface IIAPDecision<TUpdate = SafeDataModelUpdate> {
  decision: 'offer_help' | 'no_intervention' | 'defer';
  reason: string;
  offerType: 'update_suggestion' | 'text_assistance' | 'none';
  helpTopic?: 'compare_options' | 'explain_rules' | 'fix_block' | 'save_progress';
  uiStyle: 'inline_card' | 'none';
  message: string;
  updateSuggestion?: SafeSuggestion<TUpdate>;
}
export interface IIAPDecisionEnvelope<TUpdate = SafeDataModelUpdate> {
  type: 'iiap.decision';
  iiapVersion: typeof IIAP_VERSION;
  decisionId: string;
  packetId: string;
  surfaceInstanceId: string;
  payload: IIAPDecision<TUpdate>;
}

export interface AssistanceRequest {
  type: 'iiap.assistance.request'; iiapVersion: typeof IIAP_VERSION;
  requestId: string; packetId: string; decisionId: string; surfaceInstanceId: string;
  topic: NonNullable<IIAPDecision['helpTopic']>; language: string;
}
export interface AssistanceResponse {
  type: 'iiap.assistance.response'; iiapVersion: typeof IIAP_VERSION;
  requestId: string; message: string;
}

export type DeactivationReason = 'surface-deleted' | 'surface-replaced' | 'session-ended' | 'runtime-disposed' | 'host-request';
export interface SessionContext { sessionId: string }
export interface SuggestionContext {
  surfaceInstanceId: string;
  allowedTargets: IntentContextPacket['allowedOperations']['updateTargets'];
  accepted: boolean;
}
export interface ProtocolInput<TMessage> {
  sessionId: string; messageId: string; namespace: string; messages: TMessage[];
}
export interface SuggestionPolicyContext {
  originalSurfaceId: string;
  componentId: string;
  componentType: string;
  bindingPath: string;
  selectionMode?: 'single' | 'multiple';
  maxAllowedSelections?: number;
  declaredValues?: UpdateValue[];
}
export interface SuggestionPolicy {
  resolveAllowedValues(context: SuggestionPolicyContext): UpdateValue[] | undefined;
}

export interface UIProtocolAdapter<TMessage, TUpdate> {
  buildObservationPlans(input: ProtocolInput<TMessage>): ObservationPlan[];
  validateSuggestion(suggestion: unknown, context: SuggestionContext): SafeSuggestion<TUpdate> | null;
}
export interface DecisionTransport {
  decide(packet: IntentContextPacket): Promise<IIAPDecisionEnvelope>;
  assist(request: AssistanceRequest): Promise<AssistanceResponse>;
  sendFeedback?(feedback: IIAPFeedback): Promise<void>;
}
export interface HelpPresenter {
  present(decision: IIAPDecision, context: { packet: IntentContextPacket; decisionId: string }): Promise<FeedbackInteraction>;
  dismiss?(surfaceInstanceId: string): void;
}
export interface SuggestionExecutor<TUpdate = SafeDataModelUpdate> {
  execute(suggestion: SafeSuggestion<TUpdate>, context: SuggestionContext): Promise<void> | void;
}
export interface ObservationHandle {
  observe(event: ComponentEvent): void;
  flush(): Promise<IntentContextPacket | null>;
  deactivate(reason: DeactivationReason): void;
}
export interface ActivationOptions { focused?: boolean }
export interface IIAPSession {
  activate(plan: ObservationPlan, options?: ActivationOptions): ObservationHandle;
  focus(surfaceInstanceId: string): void;
  suspend(): void;
  handleDecision(decision: IIAPDecisionEnvelope): boolean;
  cancelDecision(packetId: string): boolean;
  recordFeedback(feedback: IIAPFeedback): boolean;
  deactivate(reason: DeactivationReason): void;
}
export interface IIAPRuntime {
  createSession(context: SessionContext): IIAPSession;
  dispose(): void;
}
