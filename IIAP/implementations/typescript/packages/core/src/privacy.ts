const FORBIDDEN_KEYS = new Set([
  'rawvalue', 'rawtext', 'rawevents', 'rawevent', 'valuehash', 'texthash',
  'password', 'passwd', 'passcode', 'secret', 'authtoken', 'accesstoken', 'refreshtoken',
  'apikey', 'authorization', 'credential', 'credentials', 'cookie', 'sessioncookie',
]);

const normalizedKey = (key: string): string => key.replace(/[^a-z0-9]/giu, '').toLowerCase();

export function containsForbiddenKey(value: unknown, ancestors = new Set<object>()): boolean {
  if (!value || typeof value !== 'object') return false;
  if (ancestors.has(value)) throw new TypeError('Circular value');
  ancestors.add(value);
  try {
    if (Array.isArray(value)) return value.some((child) => containsForbiddenKey(child, ancestors));
    return Object.entries(value as Record<string, unknown>)
      .some(([key, child]) => FORBIDDEN_KEYS.has(normalizedKey(key)) || containsForbiddenKey(child, ancestors));
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

export function privacyStatus(
  value: unknown,
  maxBytes = Number.POSITIVE_INFINITY,
): 'valid' | 'privacy_rejected' | 'packet_too_large' {
  try {
    const byteSize = packetByteSize(value);
    if (containsForbiddenKey(value) || containsNonFiniteNumber(value) || !Number.isFinite(byteSize)) {
      return 'privacy_rejected';
    }
    return byteSize <= maxBytes ? 'valid' : 'packet_too_large';
  } catch {
    return 'privacy_rejected';
  }
}

export function validatePrivacy(value: unknown, maxBytes = Number.POSITIVE_INFINITY): boolean {
  return privacyStatus(value, maxBytes) === 'valid';
}
