export type AssistanceTextViolation = 'empty' | 'too_large' | 'a2ui_tag' | 'a2ui_message';

export interface AssistanceTextValidation {
  valid: boolean;
  reason?: AssistanceTextViolation;
}

const A2UI_KEY_PATTERN = /["'](?:beginRendering|surfaceUpdate|dataModelUpdate|deleteSurface|createSurface|updateComponents|updateDataModel)["']\s*:/;

export function validateAssistanceText(message: unknown): AssistanceTextValidation {
  if (typeof message !== 'string' || !message.trim()) return { valid: false, reason: 'empty' };
  if (message.length > 4096) return { valid: false, reason: 'too_large' };
  if (/<\s*a2ui-json\b/i.test(message)) return { valid: false, reason: 'a2ui_tag' };
  if (A2UI_KEY_PATTERN.test(message)) return { valid: false, reason: 'a2ui_message' };
  return { valid: true };
}
