/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { hilog } from '@kit.PerformanceAnalysisKit';
import { A4pErrorCode } from '../models/common';

const LOG_DOMAIN: number = 0x00A4;
const LOG_TAG: string = 'A4P';

/**
 * Records the specific cause behind an A4P error code via hilog. Thrown
 * A4pErrors carry only the fixed message for their code, so this is where
 * the differing reasons (field paths, current statuses, underlying error
 * messages) are recorded for diagnostics.
 * @param code - The A4P error code the detail belongs to
 * @param detail - The specific cause distinguishing this failure
 */
export function logErrorDetail(code: A4pErrorCode, detail: string): void {
  hilog.error(LOG_DOMAIN, LOG_TAG, 'code %{public}d: %{public}s', code, detail);
}
