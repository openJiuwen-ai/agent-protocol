import type { Clock } from './clock.js';
import type { FeedbackBackoffCategory, FeedbackInteraction, FeedbackOutcome } from './types.js';
import type { RuntimePolicy } from './policy.js';

export class FeedbackController {
  private blockedUntil = 0;
  private readonly clock: Clock;
  private readonly policy: RuntimePolicy;
  constructor(clock: Clock, policy: RuntimePolicy) {
    this.clock = clock;
    this.policy = policy;
  }
  canOffer(): boolean { return this.clock.now() >= this.blockedUntil; }
  record(interaction: FeedbackInteraction, outcome?: FeedbackOutcome): void {
    const category: FeedbackBackoffCategory = interaction === 'accepted' && outcome === 'failed'
      ? 'execution_failed'
      : interaction;
    this.blockedUntil = Math.max(this.blockedUntil, this.clock.now() + this.policy.feedbackBackoffMs[category]);
  }
}
