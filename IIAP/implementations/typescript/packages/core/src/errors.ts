export type IIAPErrorCode =
  | 'INVALID_PACKET' | 'PRIVACY_REJECTED' | 'PACKET_TOO_LARGE'
  | 'MODEL_TIMEOUT' | 'INVALID_DECISION' | 'STALE_SURFACE'
  | 'UNSAFE_SUGGESTION' | 'ASSISTANCE_UNSAFE_OUTPUT' | 'TRANSPORT_ERROR';

export class IIAPError extends Error {
  constructor(
    readonly code: IIAPErrorCode,
    message: string,
    readonly retryable = false,
  ) { super(message); this.name = 'IIAPError'; }
}
