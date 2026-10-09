/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { A4pError, A4pErrorCode } from '../models/common';
import { logErrorDetail } from './log';

/**
 * Thrown by all A4P APIs on failure. All failures — validation, signature,
 * state, serialization — surface as a thrown A4pError carrying the error
 * code; APIs return their data directly on success. The thrown error's
 * message is fixed per code (see a4pErrorMessage), so identical codes
 * always carry identical messages; the specific cause — which field
 * failed validation, what the underlying error was — is recorded via
 * hilog, not embedded in the error message.
 * @param code - The A4P error code identifying the failure category
 * @param detail - The specific cause distinguishing this failure; logged via hilog
 */
export function a4pThrow(code: A4pErrorCode, detail?: string): never {
  if (detail) {
    logErrorDetail(code, detail);
  }
  throw new A4pError(code);
}
