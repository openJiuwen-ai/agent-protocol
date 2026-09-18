import type { SafeDataModelUpdate, SafeSuggestion, SuggestionContext } from './types.js';

function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function sameValue(left: unknown, right: unknown): boolean {
  return left === right && (left === null || typeof left === typeof right);
}

/**
 * `maxAllowedSelections` is a value-shape constraint, not an authorization gate: an authorized
 * target without a declared bound still only accepts a duplicate-free subset of its allowlist, it
 * just cannot be compared against an upper limit. That is why no bound is required here.
 */
function safeValue(value: unknown, target: SuggestionContext['allowedTargets'][number]): value is SafeDataModelUpdate['value'] {
  const selectionLimit = target.maxAllowedSelections;
  if (!target.selectionMode && selectionLimit !== undefined) return false;
  if (selectionLimit !== undefined && (!Number.isInteger(selectionLimit) || selectionLimit < 1
    || (target.selectionMode === 'single' && selectionLimit !== 1)
    || (target.selectionMode === 'multiple' && selectionLimit < 2))) return false;
  if (Array.isArray(value)) {
    if (target.selectionMode !== 'multiple' || value.length > 16) return false;
    if (selectionLimit !== undefined && value.length > selectionLimit) return false;
    if (!value.every((item) => item === null || ['string', 'boolean'].includes(typeof item)
      || (typeof item === 'number' && Number.isFinite(item)))) return false;
    if (value.some((item, index) => value.slice(0, index).some((existing) => sameValue(existing, item)))) return false;
    return value.every((item) => target.allowedValues.some((allowed) => sameValue(allowed, item)));
  }
  if (target.selectionMode === 'multiple') return false;
  if (!(value === null || ['string', 'boolean'].includes(typeof value)
    || (typeof value === 'number' && Number.isFinite(value)))) return false;
  return target.allowedValues.some((allowed) => sameValue(allowed, value));
}

export function validateDataModelSuggestion(
  value: unknown,
  context: SuggestionContext,
): SafeSuggestion<SafeDataModelUpdate> | null {
  if (!context.accepted || !isRecord(value) || value.kind !== 'data_model_update' || !Array.isArray(value.updates)) return null;
  if (value.updates.length < 1 || value.updates.length > 8) return null;
  const seenTargets = new Set<string>();
  const updates: SafeDataModelUpdate[] = [];
  for (const item of value.updates) {
    if (!isRecord(item) || typeof item.surfaceId !== 'string' || typeof item.path !== 'string') return null;
    const target = context.allowedTargets.find((candidate) => candidate.originalSurfaceId === item.surfaceId
      && candidate.bindingPath === item.path);
    const key = `${item.surfaceId}\u0000${item.path}`;
    if (item.surfaceId.includes(':') || !target?.allowedValues.length || seenTargets.has(key)) return null;
    if (item.surfaceInstanceId !== undefined && item.surfaceInstanceId !== context.surfaceInstanceId) return null;
    if (!safeValue(item.value, target)) return null;
    seenTargets.add(key);
    updates.push(item as unknown as SafeDataModelUpdate);
  }
  return { kind: 'data_model_update', updates };
}
