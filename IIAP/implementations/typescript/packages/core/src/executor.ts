import { IIAPError } from './errors.js';
import { validateDataModelSuggestion } from './suggestion.js';
import type { SafeDataModelUpdate, SafeSuggestion, SuggestionContext, SuggestionExecutor } from './types.js';

function targetsStaleSurface(suggestion: SafeSuggestion<SafeDataModelUpdate>, surfaceInstanceId: string): boolean {
  return Array.isArray(suggestion?.updates) && suggestion.updates.some((update) => (
    update && typeof update === 'object'
    && typeof update.surfaceInstanceId === 'string'
    && update.surfaceInstanceId !== surfaceInstanceId
  ));
}

export class ValidatedSuggestionExecutor implements SuggestionExecutor<SafeDataModelUpdate> {
  constructor(private readonly applyBatch: (updates: SafeDataModelUpdate[]) => Promise<void> | void) {}
  async execute(suggestion: SafeSuggestion<SafeDataModelUpdate>, context: SuggestionContext): Promise<void> {
    if (targetsStaleSurface(suggestion, context.surfaceInstanceId)) {
      throw new IIAPError('STALE_SURFACE', 'Suggestion targets a stale surface');
    }
    const safe = validateDataModelSuggestion(suggestion, context);
    if (!safe) throw new IIAPError(context.accepted ? 'UNSAFE_SUGGESTION' : 'UNSAFE_SUGGESTION', 'Suggestion was not explicitly accepted or is outside allowed targets');
    await this.applyBatch(safe.updates);
  }
}
