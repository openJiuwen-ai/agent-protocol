/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { A4pError, A4pErrorCode } from '../models/common';
import { logErrorDetail } from './log';

/**
 * Raises an A4pError carrying the given code: the specific cause is
 * recorded via hilog first, then the error is thrown with the fixed
 * message for the code (see a4pErrorMessage), so identical codes always
 * carry identical messages. All A4P API failures — validation, signature,
 * state, serialization — surface through this helper.
 * @param code - The A4P error code identifying the failure category
 * @param detail - The specific cause distinguishing this failure; recorded via hilog, never embedded in the thrown message
 */
export function a4pThrow(code: A4pErrorCode, detail?: string): never {
  if (detail) {
    logErrorDetail(code, detail);
  }
  throw new A4pError(code);
}
