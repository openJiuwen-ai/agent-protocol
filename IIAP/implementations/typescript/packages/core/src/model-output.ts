/**
 * Model output extraction shared by the decision client and the Host compatibility layer.
 *
 * Models routinely wrap the decision JSON in prose ("The user switched options…\n{...}") or bold
 * labels. Requiring a bare object turns those answers into a silent `no_intervention`, which is
 * indistinguishable from a genuine negative and quietly disables the whole capability.
 *
 * Extraction is strictly structural: a candidate must parse on its own, so prose that merely
 * contains braces cannot fabricate a decision, and the wire contract stays unchanged.
 */

function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

const FENCE = /^```(?:json)?[ \t]*\r?\n([\s\S]*?)\r?\n```[ \t]*$/i;

function extractJsonObject(text: string): Record<string, unknown> | null {
  const candidates = [text];
  const fenced = FENCE.exec(text.trim());
  if (fenced) candidates.unshift(fenced[1].trim());
  for (const candidate of candidates) {
    for (let start = candidate.indexOf('{'); start !== -1; start = candidate.indexOf('{', start + 1)) {
      let depth = 0;
      let inString = false;
      let escaped = false;
      for (let index = start; index < candidate.length; index += 1) {
        const char = candidate[index];
        if (inString) {
          if (escaped) escaped = false;
          else if (char === '\\') escaped = true;
          else if (char === '"') inString = false;
          continue;
        }
        if (char === '"') { inString = true; continue; }
        if (char === '{') depth += 1;
        else if (char === '}') {
          depth -= 1;
          if (depth === 0) {
            try {
              const parsed: unknown = JSON.parse(candidate.slice(start, index + 1));
              if (isRecord(parsed)) return parsed;
            } catch {
              // 该候选不是完整对象，继续向后寻找。
            }
            break;
          }
        }
      }
    }
  }
  return null;
}

/** Extract an object from already-parsed or raw model output. */
export function coerceModelObject(value: unknown): Record<string, unknown> | null {
  if (isRecord(value)) return value;
  if (typeof value !== 'string') return null;
  const text = value.trim();
  if (!text) return null;
  try {
    const parsed: unknown = JSON.parse(text);
    if (isRecord(parsed)) return parsed;
  } catch {
    // 回落结构化提取
  }
  return extractJsonObject(text);
}

/** Stable, content-free classification for diagnostics (never logs model text). */
export function describeModelOutput(value: unknown): string {
  if (isRecord(value)) return 'object';
  if (typeof value !== 'string') return 'not_text';
  const text = value.trim();
  if (!text) return 'empty';
  if (!coerceModelObject(value)) return 'no_json_object';
  return text.startsWith('{') ? 'recovered' : 'prose_wrapped';
}
