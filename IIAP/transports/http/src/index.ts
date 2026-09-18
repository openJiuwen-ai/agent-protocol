import type {
  AssistanceRequest, AssistanceResponse, DecisionTransport, IIAPDecisionEnvelope,
  IIAPFeedback, IntentContextPacket,
} from '@openjiuwen/iiap';
import { IIAPError, validateAssistanceText } from '@openjiuwen/iiap';
import { parseDecisionEnvelope } from '@openjiuwen/iiap/decision';

export interface HTTPTransportOptions {
  baseUrl: string;
  fetch?: typeof globalThis.fetch;
  headers?: Record<string, string>;
  timeoutMs?: number;
  feedbackUpload?: boolean;
}

export class HTTPDecisionTransport implements DecisionTransport {
  private readonly fetcher: typeof globalThis.fetch;
  constructor(private readonly options: HTTPTransportOptions) {
    if (!options.fetch && !globalThis.fetch) throw new Error('fetch implementation is required');
    this.fetcher = options.fetch ?? globalThis.fetch;
  }
  async decide(packet: IntentContextPacket): Promise<IIAPDecisionEnvelope> {
    const response = await this.post('/decision', {
      type: 'iiap.intent_context_packet', iiapVersion: '0.1', packet,
    });
    const decision = parseDecisionEnvelope(response, packet);
    if (!decision) throw new IIAPError('INVALID_DECISION', 'decision endpoint returned an invalid envelope');
    return decision;
  }
  async assist(request: AssistanceRequest): Promise<AssistanceResponse> {
    const value = await this.post('/assistance', request);
    if (!isRecord(value) || Object.keys(value).sort().join(',') !== 'iiapVersion,message,requestId,type'
      || value.type !== 'iiap.assistance.response' || value.iiapVersion !== '0.1'
      || value.requestId !== request.requestId || typeof value.message !== 'string') {
      throw new Error('INVALID_ASSISTANCE_RESPONSE');
    }
    const validation = validateAssistanceText(value.message);
    if (!validation.valid) {
      throw new IIAPError('ASSISTANCE_UNSAFE_OUTPUT', validation.reason ?? 'unsafe assistance output');
    }
    return value as unknown as AssistanceResponse;
  }
  async sendFeedback(feedback: IIAPFeedback): Promise<void> {
    if (!this.options.feedbackUpload) return;
    await this.post('/feedback', feedback);
  }
  private async post(path: string, body: unknown): Promise<unknown> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.options.timeoutMs ?? 10000);
    try {
      const response = await this.fetcher(`${this.options.baseUrl.replace(/\/$/, '')}${path}`, {
        method: 'POST', headers: { 'content-type': 'application/json', ...this.options.headers },
        body: JSON.stringify(body), signal: controller.signal,
      });
      if (!response.ok) {
        throw new IIAPError('TRANSPORT_ERROR', `IIAP endpoint returned HTTP ${response.status}`, response.status >= 500 || response.status === 429);
      }
      return await response.json();
    } catch (error) {
      if (error instanceof IIAPError) throw error;
      if (controller.signal.aborted) {
        throw new IIAPError('MODEL_TIMEOUT', 'IIAP endpoint request timed out', true);
      }
      throw new IIAPError('TRANSPORT_ERROR', 'IIAP endpoint request failed', true);
    } finally { clearTimeout(timer); }
  }
}
const isRecord = (value: unknown): value is Record<string, unknown> => Boolean(value) && typeof value === 'object' && !Array.isArray(value);
