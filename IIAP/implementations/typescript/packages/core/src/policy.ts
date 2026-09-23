import type { FeedbackBackoffCategory } from './types.js';

export interface RuntimePolicy {
  componentQuietMs: number; surfaceIdleMs: number; minReportIntervalMs: number;
  maxReportsPerSurface: number; maxReportsPerComponent: number;
  reportHistoryLimit: number; packetMaxBytes: number; maxEventsPerPacket: number;
  feedbackBackoffMs: Record<FeedbackBackoffCategory, number>;
  thresholds: {
    textValidationFailureCount: number; booleanToggleCount: number;
    scalarChangeCount: number; temporalChangeCount: number; optionChangeCount: number;
    overlayOpenCount: number; navigationChangeCount: number;
    mediaControlCount: number; viewportChangeCount: number;
  };
}

export const DEFAULT_POLICY: RuntimePolicy = {
  componentQuietMs: 2000, surfaceIdleMs: 10000, minReportIntervalMs: 30000,
  maxReportsPerSurface: 4, maxReportsPerComponent: 1,
  reportHistoryLimit: 3, packetMaxBytes: 32768, maxEventsPerPacket: 128,
  feedbackBackoffMs: { accepted: 15000, dismissed: 120000, ignored: 60000, timed_out: 60000, rejected: 300000, execution_failed: 300000 },
  thresholds: {
    textValidationFailureCount: 2, booleanToggleCount: 3, scalarChangeCount: 4,
    temporalChangeCount: 3, optionChangeCount: 3, overlayOpenCount: 2,
    navigationChangeCount: 3, mediaControlCount: 5, viewportChangeCount: 6,
  },
};

export type PolicyOverrides = Partial<Omit<RuntimePolicy, 'thresholds' | 'feedbackBackoffMs'>> & {
  thresholds?: Partial<RuntimePolicy['thresholds']>;
  feedbackBackoffMs?: Partial<RuntimePolicy['feedbackBackoffMs']>;
};

const MAX_REPORT_HISTORY = 3;
const MAX_EVENTS_PER_PACKET = 128;

function boundedInteger(value: number | undefined, fallback: number, minimum: number, maximum: number): number {
  if (!Number.isFinite(value)) return fallback;
  return Math.min(maximum, Math.max(minimum, Math.trunc(value!)));
}

export function resolvePolicy(overrides: PolicyOverrides = {}): RuntimePolicy {
  return {
    ...DEFAULT_POLICY, ...overrides,
    reportHistoryLimit: boundedInteger(overrides.reportHistoryLimit, DEFAULT_POLICY.reportHistoryLimit, 0, MAX_REPORT_HISTORY),
    maxEventsPerPacket: boundedInteger(overrides.maxEventsPerPacket, DEFAULT_POLICY.maxEventsPerPacket, 1, MAX_EVENTS_PER_PACKET),
    thresholds: { ...DEFAULT_POLICY.thresholds, ...overrides.thresholds },
    feedbackBackoffMs: { ...DEFAULT_POLICY.feedbackBackoffMs, ...overrides.feedbackBackoffMs },
  };
}
