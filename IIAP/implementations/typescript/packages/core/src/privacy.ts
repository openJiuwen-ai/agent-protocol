const FORBIDDEN_KEYS = new Set([
  'rawValue', 'rawText', 'rawEvents', 'rawEvent', 'valueHash', 'textHash',
]);

export function containsForbiddenKey(value: unknown, ancestors = new Set<object>()): boolean {
  if (!value || typeof value !== 'object') return false;
  if (ancestors.has(value)) throw new TypeError('Circular value');
  ancestors.add(value);
  try {
    if (Array.isArray(value)) return value.some((child) => containsForbiddenKey(child, ancestors));
    return Object.entries(value as Record<string, unknown>)
      .some(([key, child]) => FORBIDDEN_KEYS.has(key) || containsForbiddenKey(child, ancestors));
  } finally {
    ancestors.delete(value);
  }
}

export function packetByteSize(value: unknown): number {
  try {
    const serialized = JSON.stringify(value);
    return typeof serialized === 'string'
      ? new TextEncoder().encode(serialized).length
      : Number.POSITIVE_INFINITY;
  } catch {
    return Number.POSITIVE_INFINITY;
  }
}

function containsNonFiniteNumber(value: unknown, ancestors = new Set<object>()): boolean {
  if (typeof value === 'number') return !Number.isFinite(value);
  if (!value || typeof value !== 'object') return false;
  if (ancestors.has(value)) throw new TypeError('Circular value');
  ancestors.add(value);
  try {
    return Object.values(value).some((child) => containsNonFiniteNumber(child, ancestors));
  } finally {
    ancestors.delete(value);
  }
}

export function validatePrivacy(value: unknown, maxBytes = Number.POSITIVE_INFINITY): boolean {
  try {
    const byteSize = packetByteSize(value);
    return !containsForbiddenKey(value) && !containsNonFiniteNumber(value)
      && Number.isFinite(byteSize) && byteSize <= maxBytes;
  } catch {
    return false;
  }
}
