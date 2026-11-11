/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { CreateIntentRequest, IntentIssueContext, VerificationResult, A4pErrorCode, PROTOCOL_VERSION } from '../models/common';
import {
  IntentExecutionContext, IntentExecutionStatus, ExecutionStatus, ExecutionRecord, Execution
} from '../models/execution';
import { IntentActionType, IntentL1, IntentL2, IntentL3, SignatureInfo } from '../models/intent';
import { SigningEnv } from '../models/identity';
import { PaymentConstraints } from '../models/constraint';
import { ItemWhiteList, Product, CartItem, ShoppingItems } from '../models/product';
import { Signer } from './signer';
import { SignatureVerifier } from './verifier';
import { a4pThrow } from '../internal/result';
import { toSignableBytes, deepClone, deepFreeze } from '../internal/utils';
import {
  computeCurrentPeriodIndex, recomputeAccounting, computeExecutionInfo, computeExecutionCount,
  computePeriodExecutionCount, computeAccumulatedAmount, periodRuleOf, PeriodRule
} from '../internal/accounting';
import {
  validateCreateIntentRequest, validateIssueContext, validateNow,
  validateSigningEnv, validateExecution, validateIntentExecutionContext,
  validateIntentL1, validateIntentL2, validateIntentL3, validateSignatureInfo,
  checkChainVersions, enforceWhitelistScope, enforceTemplateMatch, A4pValidationFailure
} from '../internal/validation';
import { util } from '@kit.ArkTS';

/**
 * Throws the validation failure as an A4pError: the specific cause is
 * recorded via hilog, the thrown error carries the fixed message for
 * the code.
 * @param err - The validation failure to raise, or null to do nothing
 */
function raise(err: A4pValidationFailure | null): void {
  if (err) {
    a4pThrow(err.code, err.detail);
  }
}

/**
 * Runs a verifier call and maps any thrown exception to A4P_VERIFY_FAIL:
 * verifiers are expected to report failure through VerificationResult, but
 * a throwing verifier must still surface as an A4pError per the
 * throw-on-failure convention, never as a leaked raw exception. A verifier
 * returning no result at all is treated the same way. A verdict whose
 * parts agree is returned as-is; a self-contradictory one — `valid` true
 * with a non-success code, or `valid` false with the success code — is
 * not a verdict the SDK can trust or relay and is reported the same way.
 * @param verifier - The verifier to invoke
 * @param data - The signed bytes to verify
 * @param signature - The signature information to check
 * @param who - Whose signature is being checked (for the logged detail)
 * @returns The verifier's result (present and self-consistent)
 * @throws A4pError with A4P_VERIFY_FAIL when the verifier throws, returns no result, or returns a contradictory one
 */
function runVerifier(verifier: SignatureVerifier, data: Uint8Array, signature: SignatureInfo,
                     who: string): VerificationResult {
  let result: VerificationResult;
  try {
    result = verifier.verify(data, signature);
  } catch (e) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL, who + ' signature verification threw: ' + (e as Error).message);
  }
  // A verifier that returns no verdict at all is as broken as one that
  // throws: report it the same way instead of leaking a raw TypeError from
  // reading the absent result.
  if (!result) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL, who + ' signature verifier returned no result');
  }
  // The verdict's parts must agree: code 0 is success, anything else a
  // failure, so a valid verdict carrying a failure code — or an invalid
  // one carrying the success code — is self-contradictory. Such a result
  // is a broken verifier's output, not a verdict to trust or relay, so it
  // is reported as a verification failure like the ones above.
  const contradictory: boolean = (result.valid && result.code !== A4pErrorCode.A4P_SUCCESS) ||
    (!result.valid && result.code === A4pErrorCode.A4P_SUCCESS);
  if (contradictory) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL,
      who + ' signature verifier returned a contradictory result: valid is ' + result.valid +
      ' but code is ' + result.code);
  }
  return result;
}

/**
 * Runs a verifier and requires a valid verdict: the calling lifecycle API
 * must not endorse or activate an intent whose signature did not verify,
 * so an invalid result is mapped to A4P_VERIFY_FAIL. Use runVerifier
 * directly when the verdict itself is the answer (verifyIntentAuthenticity).
 * @param verifier - The verifier to invoke
 * @param data - The signed bytes to verify
 * @param signature - The signature information to check
 * @param who - Whose signature is being checked (for the logged detail)
 * @returns The verifier's result (a valid verdict)
 * @throws A4pError with A4P_VERIFY_FAIL when the verifier throws or reports an invalid signature
 */
function verifyOrThrow(verifier: SignatureVerifier, data: Uint8Array, signature: SignatureInfo,
                       who: string): VerificationResult {
  const result: VerificationResult = runVerifier(verifier, data, signature, who);
  if (!result.valid) {
    a4pThrow(A4pErrorCode.A4P_VERIFY_FAIL,
      who + ' signature verification failed: ' + (result.message ?? 'code ' + result.code));
  }
  return result;
}

/**
 * Runs a signer call and validates its result: a throwing signer is
 * mapped to A4P_SIGNING_FAIL, and so is one that returns no SignatureInfo
 * at all or a malformed one. The signer is user-supplied code — its
 * return value is a boundary input, exactly like a verifier's — and an
 * intent signed with a malformed result would only fail at the far end
 * of the chain (every downstream validation rejects it), so the failure
 * is surfaced here instead. Mirrors runVerifier's treatment of a
 * verifier's return value.
 * @param signer - The signer to invoke
 * @param data - The bytes to sign
 * @param who - What is being signed (for the logged detail)
 * @returns The signer's SignatureInfo (present and well-formed)
 * @throws A4pError with A4P_SIGNING_FAIL when the signer throws or returns no result or a malformed one
 */
function runSigner(signer: Signer, data: Uint8Array, who: string): SignatureInfo {
  let info: SignatureInfo;
  try {
    info = signer.sign(data);
  } catch (e) {
    a4pThrow(A4pErrorCode.A4P_SIGNING_FAIL, 'Failed to sign ' + who + ': ' + (e as Error).message);
  }
  const err: A4pValidationFailure | null = validateSignatureInfo(info, who);
  if (err) {
    a4pThrow(A4pErrorCode.A4P_SIGNING_FAIL, who + ' signer returned an invalid result: ' + err.detail);
  }
  return info;
}

/**
 * Core A4P protocol operations: the intent lifecycle (createIntent,
 * issueIntent, signIntent, activateIntent), execution accounting
 * (createExecution, completeExecution, cancelExecution), revocation,
 * and authenticity verification. All functions return their data
 * directly and throw on any failure an error in the HarmonyOS
 * BusinessError shape whose numeric code is an A4pErrorCode (the codes
 * each function may throw are listed on its @throws lines; the concrete
 * A4pError class is module-internal and not exported).
 */
export namespace A4p {

  /**
   * Creates and signs a Level 1 intent. The signer returns a `SignatureInfo`
   * that is attached as the intent's top-level `sign` field; that whole
   * field is stripped when computing the signable bytes, so `alg`,
   * `keyIdentifier`, and `sign` are all detached from the signed content.
   *
   * `now` is the signing instant (epoch milliseconds) recorded in the
   * intent's `timestamp` field and thereby covered by the signature. Unlike
   * the evaluation instant of the execution APIs it drives no decision —
   * creation applies no validity-window check; the window's upper bound is
   * first judged at L2 issuance (see issueIntent) and the full window at
   * reservation time. Defaults to the system clock; pass a fixed value in
   * tests to make the signed bytes reproducible. A provided `now` must be a
   * non-negative safe integer.
   * @param { CreateIntentRequest } request - The create-intent request containing intent details and constraints.
   * @param { Signer } agentSigner - The signer used to produce the agent's L1 signature.
   * @param { number } now - Optional signing instant in epoch milliseconds; defaults to Date.now().
   * @returns { IntentL1 } The signed Level 1 intent.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 2001 - Signing failed.
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function createIntent(request: CreateIntentRequest, agentSigner: Signer, now?: number): IntentL1 {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateCreateIntentRequest(request));

    const intentId: string = request.intentId ?? util.generateRandomUUID();
    const timestamp: number = now !== undefined ? now : Date.now();

    const intentL1: IntentL1 = {
      version: PROTOCOL_VERSION,
      intentId: intentId,
      intentConversation: request.intentConversation,
      intentSummary: request.intentSummary,
      structuredIntent: deepClone(request.structuredIntent),
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL1);
    intentL1.sign = runSigner(agentSigner, dataToSign, 'IntentL1');

    return intentL1;
  }

  /**
   * Verifies the agent's L1 signature and issues a Level 2 intent signed by
   * the server.
   *
   * The `alg`/`keyIdentifier` used for verification come from the L1's own
   * `sign` field, which is detached from the signed bytes: they are
   * attacker-influenced inputs the signature cannot authenticate, so the
   * verifier must resolve the key solely from `keyIdentifier` and bind
   * `alg` to the resolved key type (see SignatureVerifier).
   *
   * `now` is the signing instant in epoch milliseconds (see createIntent).
   * It is recorded as given: the L1/L2/L3 timestamps take no part in any
   * monotonicity check. It must not be later than the intent's `validUntil`:
   * the server cannot issue an intent whose validity window has already
   * closed — every execution against it would be refused as expired — so a
   * post-validUntil issuance instant is rejected here with
   * A4P_TIMESTAMP_EXPIRED. It may be earlier than `validFrom`.
   * The embedded L1 and the three identities are deep copies — the caller's
   * objects are never aliased into the returned intent.
   * @param { IntentL1 } intentL1 - The Level 1 intent to verify and endorse.
   * @param { SignatureVerifier } agentVerifier - The verifier used to check the agent's L1 signature.
   * @param { IntentIssueContext } issueContext - The issue context carrying user, agent, and service-provider identities.
   * @param { Signer } serverSigner - The signer used to produce the server's L2 signature.
   * @param { number } now - Optional signing instant in epoch milliseconds; defaults to Date.now().
   * @returns { IntentL2 } The signed Level 2 intent with the embedded L1.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 1002 - Unsupported version.
   * @throws { BusinessError } 2001 - Signing failed.
   * @throws { BusinessError } 2002 - Verification failed.
   * @throws { BusinessError } 3002 - Timestamp expired.
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function issueIntent(intentL1: IntentL1, agentVerifier: SignatureVerifier, issueContext: IntentIssueContext,
                              serverSigner: Signer, now?: number): IntentL2 {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateIssueContext(issueContext));
    raise(validateIntentL1(intentL1));
    if (intentL1.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL1 version ' + intentL1.version + ' is not supported');
    }

    // Check order is part of the contract: structure and versions are
    // judged before any clock verdict, so a malformed or mixed-version
    // chain is reported as such even when its validity window has also
    // lapsed.
    const timestamp: number = now !== undefined ? now : Date.now();
    // The issuance stamp may not fall past the validity window being
    // endorsed: an L2 issued after validUntil could never lead to an
    // executable intent (every reservation would be refused as expired),
    // so the closed window is reported here. The window is not required
    // to have opened yet (validFrom is not judged at issuance).
    const validUntil: number = intentL1.structuredIntent.constraints.validUntil;
    if (timestamp > validUntil) {
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_EXPIRED,
        'Issuance timestamp ' + timestamp + ' is after validUntil ' + validUntil);
    }

    const l1Data: Uint8Array = toSignableBytes(intentL1);
    // validateIntentL1 guarantees sign is present (validateSignatureInfo
    // rejects a missing or blank one); the cast only satisfies the compiler.
    verifyOrThrow(agentVerifier, l1Data, intentL1.sign as SignatureInfo, 'Agent');

    const intentL2: IntentL2 = {
      version: PROTOCOL_VERSION,
      intentL1: deepClone(intentL1),
      userIdentity: deepClone(issueContext.userIdentity),
      agentIdentity: deepClone(issueContext.agentIdentity),
      serviceProviderIdentity: deepClone(issueContext.serviceProviderIdentity),
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL2);
    intentL2.sign = runSigner(serverSigner, dataToSign, 'IntentL2');

    return intentL2;
  }

  /**
   * Verifies the server's L2 signature and produces the user-signed Level 3
   * intent. See issueIntent for the detached `alg`/`keyIdentifier`
   * verification caveat.
   * Both the L2's and the embedded L1's versions must be the protocol
   * version: a mixed-version chain is signature-valid but semantically
   * unsupported and is rejected before the user signs it.
   * `now` is the signing instant in epoch milliseconds (see createIntent).
   * It is recorded as given: the L1/L2/L3 timestamps take no part in any
   * monotonicity check, and signing judges no clock — in particular the
   * validity window (`validUntil`) is not checked here.
   * The embedded L2 and the signing environment are deep copies — the
   * caller's objects are never aliased into the returned intent.
   * @param { IntentL2 } intentL2 - The Level 2 intent to verify and sign.
   * @param { SignatureVerifier } serverVerifier - The verifier used to check the server's L2 signature.
   * @param { SigningEnv } signingEnv - The signing environment info for the user's signing context.
   * @param { Signer } userSigner - The signer used to produce the user's L3 signature.
   * @param { number } now - Optional signing instant in epoch milliseconds; defaults to Date.now().
   * @returns { IntentL3 } The signed Level 3 intent with the embedded L2.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 1002 - Unsupported version.
   * @throws { BusinessError } 2001 - Signing failed.
   * @throws { BusinessError } 2002 - Verification failed.
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function signIntent(intentL2: IntentL2, serverVerifier: SignatureVerifier, signingEnv: SigningEnv,
                             userSigner: Signer, now?: number): IntentL3 {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateSigningEnv(signingEnv));
    raise(validateIntentL2(intentL2));
    if (intentL2.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL2 version ' + intentL2.version + ' is not supported');
    }
    // The embedded L1's version is checked along with the L2's: the chain
    // the user is about to endorse must be uniformly versioned. A
    // mixed-version chain is signature-valid but semantically unsupported
    // — rejecting it here mirrors the L1 check issueIntent applies to the
    // chain it receives.
    if (intentL2.intentL1.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
        'IntentL1 version ' + intentL2.intentL1.version + ' is not supported');
    }

    const timestamp: number = now !== undefined ? now : Date.now();

    const l2Data: Uint8Array = toSignableBytes(intentL2);
    // validateIntentL2 guarantees sign is present (validateSignatureInfo
    // rejects a missing or blank one); the cast only satisfies the compiler.
    verifyOrThrow(serverVerifier, l2Data, intentL2.sign as SignatureInfo, 'Server');

    const intentL3: IntentL3 = {
      version: PROTOCOL_VERSION,
      intentL2: deepClone(intentL2),
      signingEnv: deepClone(signingEnv),
      timestamp: timestamp,
    };

    const dataToSign: Uint8Array = toSignableBytes(intentL3);
    intentL3.sign = runSigner(userSigner, dataToSign, 'IntentL3');

    return intentL3;
  }

  /**
   * Verifies the user's L3 signature and creates the execution context with
   * a frozen authorization snapshot. The full chain (embedded L2/L1,
   * identities, constraints, signingEnv) is structurally validated before
   * verification: activation must not freeze a malformed snapshot. See
   * issueIntent for the detached `alg`/`keyIdentifier` verification caveat.
   *
   * `now` is the activation instant in epoch milliseconds: it fixes the
   * period figures (`periodIndex`, `leftPeriodExecutionCount`) of the
   * initial `executionInfo` snapshot for periodic intents, and is recorded
   * as the context's `activatedTime`, which is not compared with any other
   * timestamp.
   * Non-periodic intents derive no period figure from it. It is not
   * compared with the L3's own `timestamp`. It must not be later than the intent's
   * `validUntil`: an intent whose validity window has already closed can
   * no longer be activated — every execution against it would be refused
   * as expired — so no context is created for it (an intent not yet valid,
   * in contrast, is activated and waits for validFrom).
   * Defaults
   * to the system clock; pass the same value intended for the execution
   * APIs to keep the activation snapshot and the first reservation in the
   * same period. A provided `now` must be a non-negative safe integer.
   * @param { IntentL3 } intentL3 - The Level 3 intent to verify and activate.
   * @param { SignatureVerifier } userVerifier - The verifier used to check the user's L3 signature.
   * @param { number } now - Optional activation instant in epoch milliseconds; defaults to Date.now().
   * @returns { IntentExecutionContext } The execution context with a frozen authorization snapshot.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 1002 - Unsupported version.
   * @throws { BusinessError } 2002 - Verification failed.
   * @throws { BusinessError } 3002 - Timestamp expired.
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function activateIntent(intentL3: IntentL3, userVerifier: SignatureVerifier,
                                 now?: number): IntentExecutionContext {
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateIntentL3(intentL3));
    raise(checkChainVersions(intentL3));

    // Check order is part of the contract: structure and versions are
    // judged before any clock verdict, so a malformed or mixed-version
    // chain is reported as such even when its validity window has also
    // lapsed.
    const nowMs: number = now !== undefined ? now : Date.now();
    const constraints: PaymentConstraints = intentL3.intentL2.intentL1.structuredIntent.constraints;
    // Nor may it fall past the validity window: a context created after
    // validUntil could never reserve (every createExecution would be
    // refused as expired), so the closed window is reported instead of
    // handing out a dead-end context. An intent not yet valid is
    // activated and waits for validFrom — reservations, not activation,
    // judge the window's opening.
    if (nowMs > constraints.validUntil) {
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_EXPIRED,
        'Activation timestamp ' + nowMs + ' is after validUntil ' + constraints.validUntil);
    }

    const l3Data: Uint8Array = toSignableBytes(intentL3);
    // validateIntentL3 guarantees sign is present (validateSignatureInfo
    // rejects a missing or blank one); the cast only satisfies the compiler.
    verifyOrThrow(userVerifier, l3Data, intentL3.sign as SignatureInfo, 'User');

    const intentId: string = intentL3.intentL2.intentL1.intentId;

    const authorizedIntent: IntentL3 = deepFreeze(deepClone(intentL3));

    const context: IntentExecutionContext = {
      version: PROTOCOL_VERSION,
      intentId: intentId,
      authorizedIntent: authorizedIntent,
      status: IntentExecutionStatus.ACTIVE,
      activatedTime: nowMs,
      executions: [],
      executionInfo: computeExecutionInfo(constraints, [], nowMs),
    };

    return context;
  }

  /**
   * Verifies the server's L2 signature of the intent carried by the L3,
   * without creating an execution context: the L2 is the server-issued
   * intent, so its signature is the intent's authenticity — and it covers
   * the whole chain below it, the embedded L1's signature included. The
   * user's L3 signature (the authorization) is verified separately by
   * activateIntent. See issueIntent for the detached
   * `alg`/`keyIdentifier` verification caveat.
   *
   * The L3 is structurally validated as a whole before verification —
   * its own `sign`, `signingEnv`, `timestamp`, and `version` must be
   * present and well-formed, exactly like any chain the SDK accepts —
   * but its signature value is not verified here (that is activateIntent's
   * job) and its version is not compared against the protocol version:
   * the carrier's version plays no role in the verdict. The signature is
   * verified over the embedded L2's own bytes, so a tampered L2 carrying
   * a signature over different bytes must fail.
   *
   * Unlike the lifecycle APIs this is a pure query: the verifier's verdict
   * is the answer, so an invalid signature is returned as a
   * VerificationResult with `valid: false` (carrying the verifier's own
   * `code`/`message`) instead of being thrown. Structural problems
   * (malformed L3/L2 chain, unsupported L2/L1 version, serialization
   * failure), a throwing verifier, and a self-contradictory verdict
   * (see runVerifier) still throw.
   * @param { IntentL3 } intentL3 - The Level 3 intent carrying the Level 2 intent to verify.
   * @param { SignatureVerifier } serverVerifier - The verifier used to check the server's L2 signature.
   * @returns { VerificationResult } The verifier's result, including invalid (`valid: false`) verdicts.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 1002 - Unsupported version.
   * @throws { BusinessError } 2002 - Verification failed.
   * @throws { BusinessError } 5001 - Serialization failed.
   */
  export function verifyIntentAuthenticity(intentL3: IntentL3, serverVerifier: SignatureVerifier): VerificationResult {
    // The L3 is structurally validated as a whole: the carrier must be a
    // well-formed chain (its own sign, signingEnv, timestamp, version
    // included) exactly like the one activateIntent freezes. Its own
    // (user) signature remains activateIntent's to verify, not this
    // query's — presence and shape are checked here, the value is not.
    // validateIntentL3 also covers the embedded L2 (with its L1), so no
    // separate L2 validation is needed.
    raise(validateIntentL3(intentL3));

    const intentL2: IntentL2 = intentL3.intentL2;
    // The verified chain is the L2 with its embedded L1: a mixed-version
    // chain is signature-valid but semantically unsupported. The carrier
    // L3's version plays no role in the verdict.
    if (intentL2.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION, 'IntentL2 version ' + intentL2.version + ' is not supported');
    }
    if (intentL2.intentL1.version != PROTOCOL_VERSION) {
      a4pThrow(A4pErrorCode.A4P_UNSUPPORTED_VERSION,
        'IntentL1 version ' + intentL2.intentL1.version + ' is not supported');
    }

    const l2Data: Uint8Array = toSignableBytes(intentL2);
    // validateIntentL3 guarantees the embedded L2's sign is present
    // (validateSignatureInfo rejects a missing or blank one); the cast
    // only satisfies the compiler.
    return runVerifier(serverVerifier, l2Data, intentL2.sign as SignatureInfo, 'Server');
  }

  /**
   * Revokes the intent: no new executions may be reserved; pending ones
   * can still complete or cancel.
   * `now` is the revocation instant in epoch milliseconds. It is validated
   * but never recorded — the status flip carries no timestamp — and it is
   * not compared with `activatedTime` or any other timestamp: the clock
   * plays no role in revocation. In particular the intent's validity window
   * is deliberately not judged: revocation is an administrative kill-switch,
   * and an intent whose window already closed is revoked rather than
   * reported as expired. A provided `now` must be a non-negative safe
   * integer.
   * @param { IntentExecutionContext } context - The execution context to revoke.
   * @param { number } now - Optional revocation instant in epoch milliseconds; defaults to Date.now().
   * @returns { IntentExecutionContext } The updated execution context with status set to REVOKED.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 3001 - Invalid status.
   */
  export function revokeIntent(context: IntentExecutionContext, now?: number): IntentExecutionContext {
    raise(validateIntentExecutionContext(context));
    if (now !== undefined) {
      raise(validateNow(now));
    }
    const nowMs: number = now !== undefined ? now : Date.now();
    if (context.status !== IntentExecutionStatus.ACTIVE) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'revokeIntent requires an active intent, current status: ' + context.status);
    }

    context.status = IntentExecutionStatus.REVOKED;
    context.revokedTime = nowMs;
    return context;
  }

  /**
   * Reserves an execution against the context: the order is validated
   * against the frozen authorization snapshot (scope, order template,
   * amounts, caps, period), appended to the ledger as a flat PENDING
   * record (executionId, action, executionContent), and the quota
   * figures are re-derived from the ledger. The caller's execution
   * object is neither mutated nor aliased — the id is resolved locally
   * (generated when absent) and stored as the record's executionId, and
   * the action plus a deep copy of the executionContent land on the
   * record only after every check has passed.
   *
   * `orderId` is the payment idempotency key: a reservation whose orderId
   * is already present in the context's ledger — canceled records included
   * — is rejected with A4P_DUPLICATE_ID, so the same order cannot reserve
   * quota twice under different execution ids.
   *
   * `amount` is caller-declared: the SDK validates it against the signed
   * caps (singleOrderMaxAmount, totalMaxAmount) and books it into the
   * quota accounting, but never reconciles it with the cart — Product
   * carries no price, so the signed scope binds products, quantities,
   * and budget caps, not prices. Binding the amount to actual prices is
   * the responsibility of the party holding the authoritative product
   * catalog (typically the service provider): it must recompute the
   * amount from its catalog and pass its own figure, never relay a
   * client-declared value.
   *
   * `now` is the evaluation instant in epoch milliseconds: one value used
   * for all time judgments of this call (validity window,
   * period rollover, record timestamps); a provided `now` must be a
   * non-negative safe integer. It must not be earlier than `validFrom`.
   * It is not compared with the context's `activatedTime`.
   * Defaults to the system clock; pass a fixed or offset
   * value in tests or to apply a caller-side clock-skew tolerance policy.
   * The SDK itself applies no tolerance — callers that want a window
   * adjust the value they pass.
   *
   * Failure semantics: a rejected reservation leaves the context unchanged
   * except when `now` is past `validUntil` — the status flips to EXPIRED
   * before the rejection is thrown, so a later call observing the context
   * sees the expiry. Callers relying on immutability of failed results
   * must copy. Checks run in a fixed order — request structure, parameter
   * consistency with the frozen snapshot (action, currency),
   * context status, validity window (validFrom, the lazy EXPIRED flip), duplicate
   * ids, then quota — so parameter-class rejections never depend on the
   * ledger state and the expiry flip is not skipped by duplicate rejections.
   * @param { IntentExecutionContext } context - The execution context to reserve against.
   * @param { Execution } execution - The execution to reserve.
   * @param { number } now - Optional evaluation instant in epoch milliseconds; defaults to Date.now().
   * @returns { IntentExecutionContext } The updated execution context with the new reservation appended.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 1003 - Duplicate ID.
   * @throws { BusinessError } 3001 - Invalid status.
   * @throws { BusinessError } 3002 - Timestamp expired.
   * @throws { BusinessError } 3003 - Timestamp too early.
   * @throws { BusinessError } 4001 - Execution count exceeded.
   * @throws { BusinessError } 4002 - Amount exceeded.
   * @throws { BusinessError } 4004 - Period execution count exceeded.
   * @throws { BusinessError } 4005 - Cart does not match the authorized resource.
   * @throws { BusinessError } 4006 - Single order amount exceeded.
   * @throws { BusinessError } 4007 - Product not authorized.
   */
  export function createExecution(context: IntentExecutionContext, execution: Execution,
                                  now?: number): IntentExecutionContext {
    raise(validateIntentExecutionContext(context));
    if (now !== undefined) {
      raise(validateNow(now));
    }
    raise(validateExecution(execution));
    // The caller's object is never mutated or aliased: the id is resolved
    // locally (generated when absent) and stored as the record's
    // executionId; the content lands as the record's own deep copy only
    // after every check has passed.
    const executionId: string = execution.id !== undefined ? execution.id : util.generateRandomUUID();

    // Checks run in a fixed order — request structure (above), parameter
    // consistency with the frozen snapshot (action, currency),
    // context status, validity window (the lazy EXPIRED flip), duplicate
    // ids, then quota — so parameter-class rejections never depend on the
    // ledger state, and an expired context flips before duplicate-id
    // rejections are reported.
    const l1: IntentL1 = context.authorizedIntent.intentL2.intentL1;
    const constraints: PaymentConstraints = l1.structuredIntent.constraints;
    const nowMs: number = now !== undefined ? now : Date.now();

    if (execution.action !== l1.structuredIntent.action) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Execution action ' + execution.action + ' does not match intent action ' + l1.structuredIntent.action);
    }
    if (execution.executionContent.currency !== constraints.currency) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER,
        'Currency mismatch: order is ' + execution.executionContent.currency +
        ', intent constraint is ' + constraints.currency);
    }

    if (context.status !== IntentExecutionStatus.ACTIVE) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'createExecution requires an active intent, current status: ' + context.status);
    }

    if (nowMs < constraints.validFrom) {
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
        'Current time ' + nowMs + ' is before validFrom ' + constraints.validFrom);
    }
    if (nowMs > constraints.validUntil) {
      context.status = IntentExecutionStatus.EXPIRED;
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_EXPIRED,
        'Current time ' + nowMs + ' is after validUntil ' + constraints.validUntil);
    }

    const duplicateExists: boolean = context.executions.some(
      (r: ExecutionRecord) => r.executionId === executionId);
    if (duplicateExists) {
      a4pThrow(A4pErrorCode.A4P_DUPLICATE_ID, 'Execution ID already exists: ' + executionId);
    }
    // orderId is the payment idempotency key: one order reserves quota at
    // most once per context — canceled records included, so a canceled
    // reservation cannot be re-reserved under a fresh execution id.
    const duplicateOrder: boolean = context.executions.some(
      (r: ExecutionRecord) => r.executionContent.orderId === execution.executionContent.orderId);
    if (duplicateOrder) {
      a4pThrow(A4pErrorCode.A4P_DUPLICATE_ID,
        'Order ID already exists: ' + execution.executionContent.orderId);
    }
    // Optional caps: an absent cap means no limit of that kind, so the
    // check is skipped entirely.
    const countCap: number | undefined = constraints.maxExecutionCount;
    if (countCap !== undefined) {
      const executionCount: number = computeExecutionCount(context.executions);
      if (executionCount >= countCap) {
        a4pThrow(A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED,
          'Execution count ' + executionCount + ' reached maxExecutionCount ' + countCap);
      }
    }

    const orderAmount: number = execution.executionContent.amount;
    const singleOrderCap: number | undefined = constraints.singleOrderMaxAmount;
    if (singleOrderCap !== undefined && orderAmount > singleOrderCap) {
      a4pThrow(A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED,
        'Order amount ' + orderAmount + ' exceeds singleOrderMaxAmount ' + singleOrderCap);
    }
    const accumulatedAmount: number = computeAccumulatedAmount(context.executions);
    if (accumulatedAmount + orderAmount > constraints.totalMaxAmount) {
      a4pThrow(A4pErrorCode.A4P_AMOUNT_EXCEEDED,
        'Total amount ' + (accumulatedAmount + orderAmount) + ' exceeds totalMaxAmount ' + constraints.totalMaxAmount);
    }

    if (l1.structuredIntent.action === IntentActionType.AI_PAY) {
      raise(enforceWhitelistScope(context, execution.executionContent.cartItems));
      raise(enforceTemplateMatch(context, execution.executionContent.cartItems));
    }

    // The period rule is all-or-nothing: periodic intents get a period
    // index on their records and a per-period cap; non-periodic intents
    // have neither.
    const periodRule: PeriodRule | null = periodRuleOf(constraints);
    let currentPeriodIndex: number | undefined = undefined;
    if (periodRule !== null) {
      currentPeriodIndex = computeCurrentPeriodIndex(periodRule, nowMs);
      const periodExecutionCount: number = computePeriodExecutionCount(context.executions, currentPeriodIndex);
      if (periodExecutionCount >= periodRule.maxExecutionCountPerPeriod) {
        a4pThrow(A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED,
          'Period ' + currentPeriodIndex + ' execution count ' + periodExecutionCount +
          ' reached maxExecutionCountPerPeriod ' + periodRule.maxExecutionCountPerPeriod);
      }
    }

    // The caller's object is never mutated and never aliased: the id is
    // resolved locally (generated when absent) and stored as the record's
    // executionId, while the action and a deep copy of the executionContent
    // land on the ledger's own flat record only after every check has
    // passed.
    const record: ExecutionRecord = {
      executionId: executionId,
      status: ExecutionStatus.PENDING,
      action: execution.action,
      executionContent: deepClone(execution.executionContent),
      reservedAt: nowMs,
    };
    if (currentPeriodIndex !== undefined) {
      record.periodIndex = currentPeriodIndex;
    }

    context.executions.push(record);
    recomputeAccounting(context, nowMs);

    return context;
  }

  /**
   * Marks a pending execution completed. Allowed when the context is
   * ACTIVE, REVOKED, or EXPIRED (so pending executions reserved before
   * revocation or expiry can still be finalized). Auto-completion
   * (status → CONSUMED when every record is finalized and either the
   * count cap is reached or the total amount budget is exhausted) applies
   * only to an ACTIVE context: a REVOKED or EXPIRED context keeps its
   * status when its last pending record finalizes.
   * `now` is the evaluation instant in epoch milliseconds (see
   * createExecution). The accounting snapshot is re-derived at it: a
   * completion books no quota (a PENDING record already counts), but the
   * evaluation instant moves, so the period figures (periodIndex,
   * leftPeriodExecutionCount) are updated to the period containing the
   * completion instant. It must not be earlier than the record's
   * `reservedAt`: a finalization predating its own reservation would
   * rewrite history (the ledger's timestamps are monotonic like the
   * chain's), so such a clock is rejected instead of persisted.
   * @param { IntentExecutionContext } context - The execution context containing the pending execution.
   * @param { string } executionId - The ID of the execution to mark completed.
   * @param { number } now - Optional completion timestamp in epoch milliseconds; defaults to Date.now().
   * @returns { IntentExecutionContext } The updated execution context.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 3001 - Invalid status.
   * @throws { BusinessError } 3003 - Timestamp too early.
   */
  export function completeExecution(context: IntentExecutionContext, executionId: string,
                                    now?: number): IntentExecutionContext {
    raise(validateIntentExecutionContext(context));
    if (now !== undefined) {
      raise(validateNow(now));
    }
    if (context.status !== IntentExecutionStatus.ACTIVE && context.status !== IntentExecutionStatus.REVOKED
      && context.status !== IntentExecutionStatus.EXPIRED) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'completeExecution requires an active, revoked, or expired intent, current status: ' + context.status);
    }

    const record: ExecutionRecord | undefined = context.executions.find(
      (r: ExecutionRecord) => r.executionId === executionId);
    if (!record) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Execution not found: ' + executionId);
    }
    if (record.status !== ExecutionStatus.PENDING) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS, 'Execution is not pending, current status: ' + record.status);
    }

    const nowMs: number = now !== undefined ? now : Date.now();
    // Record timestamps are monotonic like the chain's: a completion can
    // never predate the reservation it finalizes. A clock stepped back past
    // reservedAt is refused here rather than persisted as a record that
    // restore-side validation would reject as corrupted.
    if (nowMs < record.reservedAt) {
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
        'Completion timestamp ' + nowMs + ' is earlier than reservedAt ' + record.reservedAt);
    }
    record.status = ExecutionStatus.COMPLETED;
    record.completedAt = nowMs;

    // A completion books no quota — a PENDING record already counts toward
    // every figure — but the evaluation instant moves: re-derive the
    // accounting snapshot so the period figures (periodIndex,
    // leftPeriodExecutionCount) reflect the period containing the
    // completion instant, not the one containing the last ledger mutation.
    recomputeAccounting(context, nowMs);

    const allDone: boolean = context.executions.every(
      (r: ExecutionRecord) => r.status === ExecutionStatus.COMPLETED || r.status === ExecutionStatus.CANCELED);
    // Auto-complete requires an ACTIVE context, a drained budget, and a
    // settled ledger: the count cap is reached or the total amount budget
    // is exhausted, and every record is finalized (flipping while a record
    // is still PENDING would strand it — finalize APIs require a
    // non-CONSUMED status). Without maxExecutionCount the count trigger is
    // absent but the amount trigger remains (totalMaxAmount is always
    // present). A REVOKED or EXPIRED context keeps its status —
    // revocation/expiry is a fact about the intent's history, not erased
    // by the ledger draining.
    const constraints: PaymentConstraints =
      context.authorizedIntent.intentL2.intentL1.structuredIntent.constraints;
    const countCap: number | undefined = constraints.maxExecutionCount;
    const countReached: boolean = countCap !== undefined &&
      computeExecutionCount(context.executions) >= countCap;
    const amountExhausted: boolean = computeAccumulatedAmount(context.executions) >= constraints.totalMaxAmount;
    if (context.status === IntentExecutionStatus.ACTIVE && allDone && (countReached || amountExhausted)) {
      context.status = IntentExecutionStatus.CONSUMED;
    }

    return context;
  }

  /**
   * Cancels a pending execution and releases its quota: an explicit cancel
   * is a definitive outcome (the order will not happen), so the execution
   * count slot, amount, and period execution count roll back. Implicit paths
   * (timeouts, failures) do NOT roll back — that is the fail-closed rule
   * preventing over-commit when the downstream outcome is unknown. The
   * ledger itself stays append-only (`executions.length` never decreases).
   * `now` is the evaluation instant in epoch milliseconds (see createExecution).
   * Allowed when the context is ACTIVE, REVOKED, or EXPIRED (so pending
   * executions reserved before revocation or expiry can still be finalized).
   * `now` must not be earlier than the record's `reservedAt`: a
   * finalization predating its own reservation would rewrite history (the
   * ledger's timestamps are monotonic like the chain's), so such a clock is
   * rejected instead of persisted.
   * @param { IntentExecutionContext } context - The execution context containing the pending execution.
   * @param { string } executionId - The ID of the execution to cancel.
   * @param { number } now - Optional cancellation timestamp in epoch milliseconds; defaults to Date.now().
   * @returns { IntentExecutionContext } The updated execution context with quota released.
   * @throws { BusinessError } 1001 - Invalid parameter.
   * @throws { BusinessError } 3001 - Invalid status.
   * @throws { BusinessError } 3003 - Timestamp too early.
   */
  export function cancelExecution(context: IntentExecutionContext, executionId: string,
                                  now?: number): IntentExecutionContext {
    raise(validateIntentExecutionContext(context));
    if (now !== undefined) {
      raise(validateNow(now));
    }
    if (context.status !== IntentExecutionStatus.ACTIVE && context.status !== IntentExecutionStatus.REVOKED
      && context.status !== IntentExecutionStatus.EXPIRED) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS,
        'cancelExecution requires an active, revoked, or expired intent, current status: ' + context.status);
    }

    const record: ExecutionRecord | undefined = context.executions.find(
      (r: ExecutionRecord) => r.executionId === executionId);
    if (!record) {
      a4pThrow(A4pErrorCode.A4P_INVALID_PARAMETER, 'Execution not found: ' + executionId);
    }
    if (record.status !== ExecutionStatus.PENDING) {
      a4pThrow(A4pErrorCode.A4P_INVALID_STATUS, 'Execution is not pending, current status: ' + record.status);
    }

    const nowMs: number = now !== undefined ? now : Date.now();
    // Same monotonic rule as completeExecution: a cancellation predating
    // its own reservation is a rewound clock, not a fact to persist.
    if (nowMs < record.reservedAt) {
      a4pThrow(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY,
        'Cancellation timestamp ' + nowMs + ' is earlier than reservedAt ' + record.reservedAt);
    }
    record.status = ExecutionStatus.CANCELED;
    record.canceledAt = nowMs;

    // Quota release happens by re-deriving from the ledger — the canceled
    // record drops out of the execution count, accumulated amount, and
    // current-period counts, while earlier-period records are excluded by
    // their periodIndex.
    recomputeAccounting(context, nowMs);

    return context;
  }
}
