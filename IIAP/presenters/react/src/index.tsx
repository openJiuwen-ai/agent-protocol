import React, { useEffect, useState } from 'react';
import type { FeedbackInteraction, HelpPresenter, IIAPDecision, IntentContextPacket } from '@openjiuwen/iiap';

const DEFAULT_TTL_MS = 60_000;
const MIN_TTL_MS = 5_000;
const MAX_TTL_MS = 600_000;

export interface ReactHelpPresenterOptions { ttlMs?: number }
export type ReactHelpResponse = Extract<FeedbackInteraction, 'accepted' | 'dismissed' | 'rejected'>;
export interface ReactHelpOffer {
  decisionId: string;
  decision: IIAPDecision;
  packet: IntentContextPacket;
}

type InternalOffer = ReactHelpOffer & { resolve: (response: FeedbackInteraction) => void };
type Listener = (offer: ReactHelpOffer | null) => void;

export class ReactHelpPresenter implements HelpPresenter {
  private readonly ttlMs: number;
  private readonly listeners = new Set<Listener>();
  private current: InternalOffer | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;

  constructor(options: ReactHelpPresenterOptions = {}) {
    const ttlMs = options.ttlMs ?? DEFAULT_TTL_MS;
    if (!Number.isFinite(ttlMs) || ttlMs < MIN_TTL_MS || ttlMs > MAX_TTL_MS) {
      throw new RangeError(`ttlMs must be between ${MIN_TTL_MS} and ${MAX_TTL_MS}`);
    }
    this.ttlMs = ttlMs;
  }

  present(decision: IIAPDecision, context: { packet: IntentContextPacket; decisionId: string }): Promise<FeedbackInteraction> {
    const previous = this.current;
    if (previous) {
      this.clearTimer();
      this.current = null;
      previous.resolve('ignored');
    }
    return new Promise((resolve) => {
      const offer: InternalOffer = {
        decisionId: context.decisionId,
        decision,
        packet: context.packet,
        resolve,
      };
      this.current = offer;
      this.timer = setTimeout(() => this.settle(offer, 'timed_out'), this.ttlMs);
      this.publish();
    });
  }

  dismiss(surfaceInstanceId: string): void {
    if (this.current?.packet.surfaceInstanceId === surfaceInstanceId) {
      this.settle(this.current, 'ignored');
    }
  }

  respond(decisionId: string, response: ReactHelpResponse): boolean {
    const offer = this.current;
    return Boolean(offer && offer.decisionId === decisionId && this.settle(offer, response));
  }

  getSnapshot(): ReactHelpOffer | null {
    return this.snapshot();
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    listener(this.snapshot());
    return () => { this.listeners.delete(listener); };
  }

  dispose(): void {
    if (this.current) this.settle(this.current, 'ignored');
    this.clearTimer();
    this.listeners.clear();
  }

  private settle(offer: InternalOffer, response: FeedbackInteraction): boolean {
    if (this.current !== offer) return false;
    this.clearTimer();
    this.current = null;
    this.publish();
    offer.resolve(response);
    return true;
  }

  private clearTimer(): void {
    if (this.timer !== null) clearTimeout(this.timer);
    this.timer = null;
  }

  private publish(): void {
    const snapshot = this.snapshot();
    for (const listener of this.listeners) listener(snapshot);
  }

  private snapshot(): ReactHelpOffer | null {
    if (!this.current) return null;
    return {
      decisionId: this.current.decisionId,
      decision: this.current.decision,
      packet: this.current.packet,
    };
  }
}

export function IIAPHelpHost({ presenter }: { presenter: ReactHelpPresenter }): React.ReactElement | null {
  const [offer, setOffer] = useState<ReactHelpOffer | null>(() => presenter.getSnapshot());
  useEffect(() => {
    return presenter.subscribe(setOffer);
  }, [presenter]);
  if (!offer) return null;
  return <aside role="status" aria-live="polite" data-iiap-offer>
    <p>{offer.decision.message}</p>
    <button type="button" onClick={() => presenter.respond(offer.decisionId, 'accepted')}>接受帮助</button>
    <button type="button" onClick={() => presenter.respond(offer.decisionId, 'dismissed')}>暂不需要</button>
  </aside>;
}
