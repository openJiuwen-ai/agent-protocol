/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { describe, it, expect } from '@ohos/hypium';
import {
  A4p, A4pErrorCode,
  IntentL1, IntentL2, IntentL3,
  IntentExecutionContext, IntentExecutionStatus, ExecutionStatus, Execution,
  CreateIntentRequest, PaymentConstraints, Product, AlgorithmType, IntentActionType,
  IntentIssueContext, SigningEnv, VerificationResult
} from '../../../../Index';
import { A4pError } from '../../../main/ets/models/common';
import {
  MockSigner, MockVerifier, FailingSigner, FailingVerifier, NoResultVerifier, NoResultSigner, BlankSignatureSigner
} from './mock/MockSigner';
import {
  createTestCreateIntentRequest,
  createTestCreateIntentRequestAround,
  createTestIntentIssueContext,
  createTestSigningEnv,
  createTestExecution,
  createTestOrder,
  createTestProductWithIds,
  createTestL1,
  createTestL2
} from './mock/TestDataFactory';

// Deterministic evaluation instant used by execution-period tests:
// constraints built with createTestCreateIntentRequestAround(FIXED_NOW)
// stay valid there.
const FIXED_NOW: number = 1730000000000;

/**
 * A fully issued intent chain plus its activated execution context,
 * as built by createFullLifecycle.
 */
class FullLifecycle {
  /** Signed Level 1 intent (created by the agent) */
  l1: IntentL1 = {} as IntentL1;
  /** Issued Level 2 intent (endorsed by the server) */
  l2: IntentL2 = {} as IntentL2;
  /** User-signed Level 3 intent */
  l3: IntentL3 = {} as IntentL3;
  /** Execution context after activation */
  ctx: IntentExecutionContext = {} as IntentExecutionContext;
}

/**
 * Builds a full lifecycle around the fixed clock so validity windows and
 * period rollovers are deterministic. Callers may customize the request
 * before it is signed. The whole chain (and the activation) is stamped at
 * FIXED_NOW by default — the monotonic checks are non-strict, so one
 * shared instant is a valid chain, and the fixed default keeps the
 * context's activatedTime at or before every FIXED_NOW reservation the
 * execution tests make. Pass another instant to shift the chain.
 */
function createFullLifecycle(request: CreateIntentRequest =
  createTestCreateIntentRequestAround(FIXED_NOW), now: number = FIXED_NOW): FullLifecycle {
  const lifecycle: FullLifecycle = new FullLifecycle();
  const agentSigner = new MockSigner();
  lifecycle.l1 = A4p.createIntent(request, agentSigner, now);

  const agentVerifier = new MockVerifier();
  const issueContext = createTestIntentIssueContext();
  const serverSigner = new MockSigner(AlgorithmType.RSA3072, 'server-key-001');
  lifecycle.l2 = A4p.issueIntent(lifecycle.l1, agentVerifier, issueContext, serverSigner, now);

  const serverVerifier = new MockVerifier();
  const signingEnv = createTestSigningEnv();
  const userSigner = new MockSigner(AlgorithmType.RSA3072, 'user-key-001');
  lifecycle.l3 = A4p.signIntent(lifecycle.l2, serverVerifier, signingEnv, userSigner, now);

  const userVerifier = new MockVerifier();
  lifecycle.ctx = A4p.activateIntent(lifecycle.l3, userVerifier, now);

  return lifecycle;
}

/**
 * Builds a request with period constraint fields around FIXED_NOW. The
 * resource is an order template, so repeated orders are bounded only by
 * the constraint caps, never by a quantity budget.
 */
function createPeriodicRequest(maxExecutionCountPerPeriod: number): CreateIntentRequest {
  const request = createTestCreateIntentRequestAround(FIXED_NOW);
  request.structuredIntent.constraints.anchorTime = FIXED_NOW - 60000;
  request.structuredIntent.constraints.periodSeconds = 3600;
  request.structuredIntent.constraints.maxExecutionCountPerPeriod = maxExecutionCountPerPeriod;
  return request;
}

/**
 * Builds a request whose constraints omit every optional field: no
 * single-order cap, no count cap, and no period rule (non-periodic).
 */
function createOptionalFieldsOmittedRequest(): CreateIntentRequest {
  const request = createTestCreateIntentRequestAround(FIXED_NOW);
  request.structuredIntent.constraints = {
    totalMaxAmount: 500000,
    currency: 'CNY',
    validFrom: FIXED_NOW - 60000,
    validUntil: FIXED_NOW + 3600000
  };
  return request;
}

/**
 * Builds a request whose period rule is partial: `missing` names the one
 * field of anchorTime/periodSeconds/maxExecutionCountPerPeriod that is
 * omitted while the other two stay present.
 */
function createRequestMissingPeriodField(missing: string): CreateIntentRequest {
  const request = createTestCreateIntentRequestAround(FIXED_NOW);
  const base: PaymentConstraints = request.structuredIntent.constraints;
  const constraints: PaymentConstraints = {
    singleOrderMaxAmount: base.singleOrderMaxAmount,
    totalMaxAmount: base.totalMaxAmount,
    currency: base.currency,
    validFrom: base.validFrom,
    validUntil: base.validUntil,
    maxExecuteCount: base.maxExecuteCount
  };
  if (missing !== 'anchorTime') {
    constraints.anchorTime = base.anchorTime;
  }
  if (missing !== 'periodSeconds') {
    constraints.periodSeconds = base.periodSeconds;
  }
  if (missing !== 'maxExecutionCountPerPeriod') {
    constraints.maxExecutionCountPerPeriod = base.maxExecutionCountPerPeriod;
  }
  request.structuredIntent.constraints = constraints;
  return request;
}

/**
 * Runs `fn` and expects it to throw an A4pError with the given code.
 * Returns the caught error for optional further assertions.
 */
function expectThrows(code: A4pErrorCode, fn: () => void): A4pError {
  let caught: A4pError | undefined = undefined;
  try {
    fn();
  } catch (e) {
    if (e instanceof A4pError) {
      caught = e;
    } else {
      throw new Error('Expected A4pError with code ' + code + ', got: ' + (e as Error));
    }
  }
  if (!caught) {
    throw new Error('Expected A4pError with code ' + code + ', but no error was thrown');
  }
  expect(caught.code).assertEqual(code);
  return caught;
}

export default function a4pTest() {
  describe('A4pTest', () => {

    describe('createIntent', () => {
      it('createIntent_success', 0, () => {
        const request = createTestCreateIntentRequest();
        const signer = new MockSigner();

        const intentL1 = A4p.createIntent(request, signer);

        expect(intentL1.intentId).assertEqual('test-intent-001');
        expect(intentL1.version).assertEqual('1.0');
        // `sign` is optional at the type level; createIntent always
        // attaches it on success, so assert presence, then read via `?.`.
        expect(intentL1.sign !== undefined).assertTrue();
        // alg/keyIdentifier live in the detached sign object, outside the
        // signed bytes; the signature itself is bare base64url (no '.' segments).
        expect(intentL1.sign?.alg).assertEqual(AlgorithmType.RSA3072);
        expect(intentL1.sign?.keyIdentifier).assertEqual('key-001');
        expect(intentL1.sign?.sign.indexOf('.')).assertEqual(-1);
      });

      it('createIntent_usesInjectedClock_forTimestamp', 0, () => {
        // The injected signing instant lands in the signed timestamp, and
        // with the factory-fixed intentId and request content it makes the
        // signed bytes reproducible: two independent runs at the same
        // instant yield identical signatures.
        const signer = new MockSigner();

        const first = A4p.createIntent(createTestCreateIntentRequestAround(FIXED_NOW), signer, FIXED_NOW);
        const second = A4p.createIntent(createTestCreateIntentRequestAround(FIXED_NOW), signer, FIXED_NOW);

        expect(first.timestamp).assertEqual(FIXED_NOW);
        expect(second.timestamp).assertEqual(FIXED_NOW);
        expect(first.sign !== undefined).assertTrue();
        expect(second.sign?.sign).assertEqual(first.sign?.sign);
      });

      it('createIntent_generatesId_whenNotProvided', 0, () => {
        const request = createTestCreateIntentRequest();
        const requestWithoutId: CreateIntentRequest = {
          intentConversation: request.intentConversation,
          intentSummary: request.intentSummary,
          structuredIntent: request.structuredIntent
        }
        const signer = new MockSigner();

        const intentL1 = A4p.createIntent(requestWithoutId, signer);

        expect(intentL1.intentId !== undefined).assertTrue();
        expect(intentL1.intentId.length).assertLarger(0);
      });

      it('createIntent_fails_whenValidFromAfterValidUntil', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.validFrom = Date.now() + 3600000;
        request.structuredIntent.constraints.validUntil = Date.now() - 3600000;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenMaxExecuteCountIsZero', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.maxExecuteCount = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenSigningThrows', 0, () => {
        const request = createTestCreateIntentRequest();
        const signer = new FailingSigner();

        expectThrows(A4pErrorCode.A4P_SIGNING_FAIL, () => {
          A4p.createIntent(request, signer);
        });
      });

      it('createIntent_fails_whenSignerReturnsNoResult', 0, () => {
        // A signer returning no result is as broken as one that throws:
        // the failure must surface here, not as a signed intent whose
        // sign field fails every downstream check.
        const request = createTestCreateIntentRequest();

        expectThrows(A4pErrorCode.A4P_SIGNING_FAIL, () => {
          A4p.createIntent(request, new NoResultSigner());
        });
      });

      it('createIntent_fails_whenSignerReturnsMalformedResult', 0, () => {
        const request = createTestCreateIntentRequest();

        expectThrows(A4pErrorCode.A4P_SIGNING_FAIL, () => {
          A4p.createIntent(request, new BlankSignatureSigner());
        });
      });

      it('createIntent_fails_whenValidFromIsNegative', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.validFrom = -1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenValidUntilIsNegative', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.validUntil = -1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenMaxExecuteCountIsNegative', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.maxExecuteCount = -5;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenMaxExecuteCountIsDecimal', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.maxExecuteCount = 1.5;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenAmountExceedsSafeIntegerRange', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.totalMaxAmount = 9007199254740992; // 2^53

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenSingleOrderMaxAmountIsNegative', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.singleOrderMaxAmount = -100;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenTotalMaxAmountIsNegative', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.totalMaxAmount = -1000;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenSingleOrderMaxAmountIsZero', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.singleOrderMaxAmount = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenTotalMaxAmountIsZero', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.totalMaxAmount = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenCurrencyIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.currency = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenStructuredIntentActionIsInvalid', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.action = 99 as Object as IntentActionType;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenWhiteListGroupIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: []
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenGroupWhiteListIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [],
            quantity: 1
          }]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenGroupQuantityIsZero', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [{ seller: { id: '' }, spusku: { spuId: '', skuId: '' } }],
            quantity: 0
          }]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenGroupProductSellerIsMissing', 0, () => {
        const request = createTestCreateIntentRequest();
        const product: Product = JSON.parse('{"spusku":{}}') as Product;
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [product],
            quantity: 1
          }]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenProductSellerIdBlank', 0, () => {
        // seller.id is part of the whitelist matching key: whitespace-only
        // ids are rejected like empty ones.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [{ seller: { id: '   ' }, spusku: { spuId: 'spu-001', skuId: 'sku-001' } }],
            quantity: 1
          }]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenProductHasNoSpuOrSkuId', 0, () => {
        // A product with neither spuId nor skuId carries no variant
        // identity and could match whitelist entries vacuously.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [{ seller: { id: 'seller-001' }, spusku: {} }],
            quantity: 1
          }]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenProductSpuIdBlankButProvided', 0, () => {
        // A provided id must be non-blank: a blank spuId alongside a real
        // skuId is still a malformed product.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [{ seller: { id: 'seller-001' }, spusku: { spuId: '   ', skuId: 'sku-001' } }],
            quantity: 1
          }]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_succeeds_whenProductCarriesOnlySkuId', 0, () => {
        // At least one of spuId/skuId is enough: a sku-only product is a
        // valid whitelist entry.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [{ seller: { id: 'seller-001' }, spusku: { skuId: 'sku-001' } }],
            quantity: 1
          }]
        };

        const l1 = A4p.createIntent(request, new MockSigner());

        expect(l1.intentId).assertEqual('test-intent-001');
      });

      it('createIntent_fails_whenGroupWhiteListHasDuplicateProduct', 0, () => {
        // A product may appear at most once per group: duplicates would
        // make the group's OR semantics ambiguous.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [{
            whiteList: [
              { seller: { id: 'seller-001' }, spusku: { spuId: 'spu-001', skuId: 'sku-001' } },
              { seller: { id: 'seller-001' }, spusku: { spuId: 'spu-001', skuId: 'sku-001' } }
            ],
            quantity: 1
          }]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenProductAppearsInTwoGroups', 0, () => {
        // A product must uniquely determine its group (the cart-to-group
        // correspondence of createExecution relies on it), so the same
        // product may not be whitelisted by two groups.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.resource = {
          itemsWhiteList: [
            {
              whiteList: [{ seller: { id: 'seller-001' }, spusku: { spuId: 'spu-001', skuId: 'sku-001' } }],
              quantity: 1
            },
            {
              whiteList: [{ seller: { id: 'seller-001' }, spusku: { spuId: 'spu-001', skuId: 'sku-001' } }],
              quantity: 2
            }
          ]
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenAnchorTimeIsNegative', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.anchorTime = -1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenPeriodSecondsIsZero', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.periodSeconds = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenMaxExecutionCountPerPeriodIsNotPositiveInteger', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.maxExecutionCountPerPeriod = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_succeeds_whenOptionalConstraintsOmitted', 0, () => {
        // Optional caps absent and non-periodic: the required fields alone
        // make a valid constraint set.
        const request = createOptionalFieldsOmittedRequest();

        const intentL1 = A4p.createIntent(request, new MockSigner());

        expect(intentL1.intentId).assertEqual('test-intent-001');
      });

      it('createIntent_fails_whenPeriodRulePartiallyProvided_anchorTimeMissing', 0, () => {
        // The period rule is all-or-nothing: two of three fields is a
        // partial rule and must be rejected.
        const request = createRequestMissingPeriodField('anchorTime');

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenPeriodRulePartiallyProvided_periodSecondsMissing', 0, () => {
        const request = createRequestMissingPeriodField('periodSeconds');

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenPeriodRulePartiallyProvided_maxExecutionCountPerPeriodMissing', 0, () => {
        const request = createRequestMissingPeriodField('maxExecutionCountPerPeriod');

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenIntentConversationIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        request.intentConversation = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenIntentSummaryIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        request.intentSummary = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenIntentIdIsProvidedButEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        request.intentId = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenCurrencyIsBlank', 0, () => {
        // Whitespace-only strings are rejected, not just empty ones.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.currency = '   ';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenIntentSummaryIsBlank', 0, () => {
        const request = createTestCreateIntentRequest();
        request.intentSummary = '   ';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenPeriodSecondsExceedsSafeRange', 0, () => {
        // periodSeconds × 1000 must stay a safe integer: one past the cap
        // (MAX_SAFE_INTEGER / 1000) is rejected.
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.periodSeconds = 9007199254741;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_succeeds_whenPeriodSecondsAtBound', 0, () => {
        const request = createTestCreateIntentRequest();
        request.structuredIntent.constraints.periodSeconds = 9007199254740;

        const l1 = A4p.createIntent(request, new MockSigner());

        expect(l1.intentId).assertEqual(request.intentId);
      });

      it('createIntent_fails_whenAnchorTimeLaterThanValidFrom', 0, () => {
        // Reservations happen at now >= validFrom: an anchor later than
        // validFrom would give pre-anchor reservations negative period
        // indexes, each with a fresh per-period budget.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.anchorTime = FIXED_NOW + 1000;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner());
        });
      });

      it('createIntent_fails_whenNowIsInvalid', 0, () => {
        const request = createTestCreateIntentRequest();

        // Negative, fractional, and unsafe-integer clocks are rejected.
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner(), -1);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner(), FIXED_NOW + 0.5);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(request, new MockSigner(), 9007199254740992);
        });
      });
    });

    describe('issueIntent', () => {
      it('issueIntent_success', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());

        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        expect(l2.intentL1 !== undefined).assertTrue();
        expect(l2.userIdentity !== undefined).assertTrue();
        expect(l2.agentIdentity !== undefined).assertTrue();
        expect(l2.serviceProviderIdentity !== undefined).assertTrue();
        expect(l2.sign !== undefined).assertTrue();
      });

      it('issueIntent_usesInjectedClock_forTimestamp', 0, () => {
        // The chain is anchored on the fixed clock so the issuance stamp
        // stays monotonic with the L1 stamp (issueIntent rejects a now
        // earlier than the embedded L1's timestamp).
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        const l1 = A4p.createIntent(request, new MockSigner(), FIXED_NOW);

        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'),
          FIXED_NOW);

        expect(l2.timestamp).assertEqual(FIXED_NOW);
      });

      it('issueIntent_fails_whenNowEarlierThanL1Timestamp', 0, () => {
        // The issuance stamp must not predate the intent it endorses: a
        // skewed clock is rejected here, instead of producing an L2 whose
        // regressive timestamp every later validation step would refuse.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        const l1 = A4p.createIntent(request, new MockSigner(), FIXED_NOW + 1000);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1, new MockVerifier(), createTestIntentIssueContext(),
            new MockSigner(AlgorithmType.RSA3072, 'server-key-001'), FIXED_NOW);
        });
      });

      it('issueIntent_succeeds_whenAdditionalInfoOmitted', 0, () => {
        // additionalInfo is optional on all three identities: omitting it
        // must pass validation at issuance and at the user-signing step.
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());

        const issueContext = createTestIntentIssueContext();
        issueContext.userIdentity.additionalInfo = undefined;
        issueContext.agentIdentity.additionalInfo = undefined;
        issueContext.serviceProviderIdentity.additionalInfo = undefined;

        const l2 = A4p.issueIntent(l1, new MockVerifier(), issueContext,
          new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));
        A4p.signIntent(l2, new MockVerifier(), createTestSigningEnv(), new MockSigner());

        expect(l2.userIdentity.additionalInfo === undefined).assertTrue();
        expect(l2.agentIdentity.additionalInfo === undefined).assertTrue();
        expect(l2.serviceProviderIdentity.additionalInfo === undefined).assertTrue();
      });

      it('issueIntent_fails_whenVerificationFails', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.issueIntent(l1, new MockVerifier(false),
            createTestIntentIssueContext(), new MockSigner());
        });
      });

      it('issueIntent_fails_whenIntentL1HasNoSignature', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1Data = A4p.createIntent(request, new MockSigner());
        const l1WithoutSign: IntentL1 = {
          version: l1Data.version,
          intentId: l1Data.intentId,
          intentConversation: l1Data.intentConversation,
          intentSummary: l1Data.intentSummary,
          structuredIntent: l1Data.structuredIntent,
          timestamp: l1Data.timestamp,
        }

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1WithoutSign, new MockVerifier(),
            createTestIntentIssueContext(), new MockSigner());
        });
      });

      it('issueIntent_fails_whenL1StructurallyInvalid', 0, () => {
        // Validate-before-endorse: a signature over structurally invalid
        // bytes is not an endorsement-worthy L1. A missing required field
        // must be rejected even though the (mock) signature checks out.
        const request = createTestCreateIntentRequest();
        const l1Data = A4p.createIntent(request, new MockSigner());
        // Rebuild without intentSummary (ArkTS has no delete, and the
        // typed interface requires the field — so build a raw record).
        const l1Invalid: Record<string, Object> = {
          'version': l1Data.version,
          'intentId': l1Data.intentId,
          'intentConversation': l1Data.intentConversation,
          'structuredIntent': l1Data.structuredIntent,
          'timestamp': l1Data.timestamp,
          'sign': l1Data.sign as Object
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1Invalid as Object as IntentL1, new MockVerifier(),
            createTestIntentIssueContext(), new MockSigner());
        });
      });

      it('issueIntent_fails_whenSignatureIsNotValidBase64url', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        // Not a valid base64url string: must surface as a verification
        // failure, not as an escaped decode exception.
        if (!l1.sign) {
          throw new Error('Expected sign to be present on the created L1');
        }
        l1.sign.sign = '!!!';

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            createTestIntentIssueContext(), new MockSigner());
        });
      });

      it('issueIntent_fails_whenAlgIsNotAnAlgorithmType', 0, () => {
        // alg is detached from the signed bytes, so rewriting it never
        // invalidates the signature itself. The structural check is the
        // first line of defense: a value outside the AlgorithmType enum is
        // rejected before any verification attempt; binding an enum-member
        // alg to the key resolved from keyIdentifier remains the verifier
        // contract's job (see issueIntent_fails_whenKeyIdentifierTampered).
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        if (!l1.sign) {
          throw new Error('Expected sign to be present on the created L1');
        }
        l1.sign.alg = 'HS256' as Object as AlgorithmType;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            createTestIntentIssueContext(), new MockSigner());
        });
      });

      it('issueIntent_fails_whenKeyIdentifierTamperedAfterSigning', 0, () => {
        // The verifier resolves the key solely from keyIdentifier: a key
        // identifier rewritten after signing resolves to no known key and
        // must be rejected.
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        if (!l1.sign) {
          throw new Error('Expected sign to be present on the created L1');
        }
        l1.sign.keyIdentifier = 'attacker-key';

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            createTestIntentIssueContext(), new MockSigner());
        });
      });

      it('issueIntent_fails_whenL1TamperedAfterSigning', 0, () => {
        // The signature is verified over the passed object's bytes, so a
        // tampered field must invalidate verification.
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        l1.intentSummary = 'tampered-after-signing';

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            createTestIntentIssueContext(), new MockSigner());
        });
      });

      it('issueIntent_fails_whenUserIdentityProviderIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const issueContext = createTestIntentIssueContext();
        issueContext.userIdentity.identityProvider = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            issueContext, new MockSigner());
        });
      });

      it('issueIntent_fails_whenUserUidIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const issueContext = createTestIntentIssueContext();
        issueContext.userIdentity.id = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            issueContext, new MockSigner());
        });
      });

      it('issueIntent_fails_whenAgentIdentityIdIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const issueContext = createTestIntentIssueContext();
        issueContext.agentIdentity.id = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            issueContext, new MockSigner());
        });
      });

      it('issueIntent_fails_whenSigningThrows', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());

        expectThrows(A4pErrorCode.A4P_SIGNING_FAIL, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            createTestIntentIssueContext(), new FailingSigner());
        });
      });

      it('issueIntent_fails_whenSignerReturnsNoResult', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());

        expectThrows(A4pErrorCode.A4P_SIGNING_FAIL, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            createTestIntentIssueContext(), new NoResultSigner());
        });
      });

      it('issueIntent_fails_whenAgentIdentityProviderIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const issueContext = createTestIntentIssueContext();
        issueContext.agentIdentity.identityProvider = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            issueContext, new MockSigner());
        });
      });

      it('issueIntent_fails_whenServiceProviderIdIsEmpty', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const issueContext = createTestIntentIssueContext();
        issueContext.serviceProviderIdentity.id = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(l1, new MockVerifier(),
            issueContext, new MockSigner());
        });
      });

      it('issueIntent_embeddedL1IsIndependentOfCallerObject', 0, () => {
        // The embedded L1 is a deep copy: mutating the caller's L1 after
        // issuance must not change the L2 or silently invalidate its
        // signature coverage.
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        const originalSummary: string = l2.intentL1.intentSummary;
        l1.intentSummary = 'tampered-after-issuance';

        expect(l2.intentL1.intentSummary).assertEqual(originalSummary);

        // And the L2 signature still verifies against its own content.
        const l3 = A4p.signIntent(l2, new MockVerifier(), createTestSigningEnv(),
          new MockSigner(AlgorithmType.RSA3072, 'user-key-001'));
        const result: VerificationResult = A4p.verifyIntentAuthenticity(l3, new MockVerifier());
        expect(result.valid).assertTrue();
      });

      it('issueIntent_identitiesAreIndependentOfCallerObjects', 0, () => {
        // The three identities are deep copies: mutating the caller's
        // issueContext after issuance must not leak into the L2.
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const issueContext = createTestIntentIssueContext();
        const l2 = A4p.issueIntent(l1, new MockVerifier(), issueContext,
          new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        const originalUid: string = l2.userIdentity.id;
        issueContext.userIdentity.id = 'tampered-after-issuance';

        expect(l2.userIdentity.id).assertEqual(originalUid);
        expect(l2.userIdentity !== issueContext.userIdentity).assertTrue();
        expect(l2.agentIdentity !== issueContext.agentIdentity).assertTrue();
        expect(l2.serviceProviderIdentity !== issueContext.serviceProviderIdentity).assertTrue();
      });
    });

    describe('signIntent', () => {
      it('signIntent_success', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        const l3 = A4p.signIntent(l2, new MockVerifier(),
          createTestSigningEnv(), new MockSigner(AlgorithmType.RSA3072, 'user-key-001'));

        expect(l3.intentL2 !== undefined).assertTrue();
        expect(l3.signingEnv !== undefined).assertTrue();
        expect(l3.sign !== undefined).assertTrue();
        expect(l3.sign?.sign.indexOf('.')).assertEqual(-1);
      });

      it('signIntent_usesInjectedClock_forTimestamp', 0, () => {
        // The chain is anchored on the fixed clock so the user's signing
        // stamp stays monotonic with the L2 stamp (signIntent rejects a now
        // earlier than the embedded L2's timestamp).
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        const l1 = A4p.createIntent(request, new MockSigner(), FIXED_NOW);
        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'),
          FIXED_NOW);

        const l3 = A4p.signIntent(l2, new MockVerifier(),
          createTestSigningEnv(), new MockSigner(AlgorithmType.RSA3072, 'user-key-001'),
          FIXED_NOW);

        expect(l3.timestamp).assertEqual(FIXED_NOW);
      });

      it('signIntent_fails_whenNowEarlierThanL2Timestamp', 0, () => {
        // The user's signing stamp must not predate the issuance it
        // authorizes: a skewed clock is rejected here, instead of producing
        // an L3 whose regressive timestamp activation would always refuse.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        const l1 = A4p.createIntent(request, new MockSigner(), FIXED_NOW + 2000);
        const l2 = A4p.issueIntent(l1, new MockVerifier(), createTestIntentIssueContext(),
          new MockSigner(AlgorithmType.RSA3072, 'server-key-001'), FIXED_NOW + 2000);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.signIntent(l2, new MockVerifier(), createTestSigningEnv(),
            new MockSigner(AlgorithmType.RSA3072, 'user-key-001'), FIXED_NOW);
        });
      });

      it('signIntent_fullChainReproducible_withInjectedClock', 0, () => {
        // With the factory-fixed request content and the clock injected at
        // every signing step, two independent runs produce byte-identical
        // L3 signatures: the wall clock is no longer a source of
        // nondeterminism in the signed bytes.
        const signatures: string[] = [];
        for (let run = 0; run < 2; run++) {
          const request = createTestCreateIntentRequestAround(FIXED_NOW);
          const l1 = A4p.createIntent(request, new MockSigner(), FIXED_NOW);
          const l2 = A4p.issueIntent(l1, new MockVerifier(),
            createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'),
            FIXED_NOW);
          const l3 = A4p.signIntent(l2, new MockVerifier(),
            createTestSigningEnv(), new MockSigner(AlgorithmType.RSA3072, 'user-key-001'),
            FIXED_NOW);
          if (!l3.sign) {
            throw new Error('Expected sign to be present on the signed L3');
          }
          signatures.push(l3.sign.sign);
        }

        expect(signatures[0]).assertEqual(signatures[1]);
      });

      it('signIntent_fails_whenVerificationFails', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.signIntent(l2, new MockVerifier(false),
            createTestSigningEnv(), new MockSigner());
        });
      });

      it('signIntent_fails_whenIntentL2HasNoSignature', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2Data = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));
        const l2WithoutSign: IntentL2 = {
          version: l2Data.version,
          intentL1: l2Data.intentL1,
          userIdentity: l2Data.userIdentity,
          agentIdentity: l2Data.agentIdentity,
          serviceProviderIdentity: l2Data.serviceProviderIdentity,
          timestamp: l2Data.timestamp,
        }

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.signIntent(l2WithoutSign, new MockVerifier(),
            createTestSigningEnv(), new MockSigner());
        });
      });

      it('signIntent_fails_whenL2StructurallyInvalid', 0, () => {
        // Validate-before-endorse at the user-signing step as well.
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2Data = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));
        // Rebuild without userIdentity (raw record, see above).
        const l2Invalid: Record<string, Object> = {
          'version': l2Data.version,
          'intentL1': l2Data.intentL1,
          'agentIdentity': l2Data.agentIdentity,
          'serviceProviderIdentity': l2Data.serviceProviderIdentity,
          'timestamp': l2Data.timestamp,
          'sign': l2Data.sign as Object
        };

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.signIntent(l2Invalid as Object as IntentL2, new MockVerifier(),
            createTestSigningEnv(), new MockSigner());
        });
      });

      it('signIntent_fails_whenReceivedL2TimestampEarlierThanL1', 0, () => {
        // issueIntent refuses to produce a regressive L2 itself (see
        // issueIntent_fails_whenNowEarlierThanL1Timestamp), but a
        // non-compliant issuer can still hand one over: the user-signing
        // step validates the received L2 and rejects it before endorsing.
        // (Tampering breaks the signature, but structural validation fires
        // first, so the INVALID_PARAMETER path is what runs.)
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        const l1 = A4p.createIntent(request, new MockSigner(), FIXED_NOW);
        const l2 = A4p.issueIntent(l1, new MockVerifier(), createTestIntentIssueContext(),
          new MockSigner(AlgorithmType.RSA3072, 'server-key-001'), FIXED_NOW);
        l2.timestamp = FIXED_NOW - 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.signIntent(l2, new MockVerifier(), createTestSigningEnv(), new MockSigner());
        });
      });

      it('signIntent_fails_whenNestedL1VersionUnsupported', 0, () => {
        // The embedded L1's version is checked along with the L2's: the
        // chain the user is about to endorse must be uniformly versioned —
        // a mixed-version chain is signature-valid but semantically
        // unsupported.
        const lifecycle = createFullLifecycle();
        lifecycle.l2.intentL1.version = '9.9';

        expectThrows(A4pErrorCode.A4P_UNSUPPORTED_VERSION, () => {
          A4p.signIntent(lifecycle.l2, new MockVerifier(),
            createTestSigningEnv(), new MockSigner(AlgorithmType.RSA3072, 'user-key-001'));
        });
      });

      it('signIntent_fails_whenSigningThrows', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        expectThrows(A4pErrorCode.A4P_SIGNING_FAIL, () => {
          A4p.signIntent(l2, new MockVerifier(),
            createTestSigningEnv(), new FailingSigner());
        });
      });

      it('signIntent_fails_whenSignerReturnsNoResult', 0, () => {
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        expectThrows(A4pErrorCode.A4P_SIGNING_FAIL, () => {
          A4p.signIntent(l2, new MockVerifier(),
            createTestSigningEnv(), new NoResultSigner());
        });
      });

      it('signIntent_signingEnvIsIndependentOfCallerObject', 0, () => {
        // The signing environment is a deep copy: mutating the caller's
        // env after signing must not leak into the L3.
        const request = createTestCreateIntentRequest();
        const l1 = A4p.createIntent(request, new MockSigner());
        const l2 = A4p.issueIntent(l1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));
        const signingEnv = createTestSigningEnv();
        const l3 = A4p.signIntent(l2, new MockVerifier(), signingEnv,
          new MockSigner(AlgorithmType.RSA3072, 'user-key-001'));

        const originalEnv: string = JSON.stringify(l3.signingEnv);
        signingEnv.device = { model: 'TamperedDevice', os: 'HarmonyOS' };

        expect(JSON.stringify(l3.signingEnv)).assertEqual(originalEnv);
        expect(l3.signingEnv !== signingEnv).assertTrue();
        expect(l3.signingEnv.device !== signingEnv.device).assertTrue();
        expect(l3.signingEnv.app !== signingEnv.app).assertTrue();

        // And the L3 signature still verifies against its own content:
        // activateIntent is the API that checks the user's L3 signature
        // (verifyIntentAuthenticity verifies the embedded L2 instead).
        A4p.activateIntent(l3, new MockVerifier());
      });
    });

    describe('activateIntent', () => {
      it('activateIntent_success', 0, () => {
        const lifecycle = createFullLifecycle();

        const ctx = A4p.activateIntent(lifecycle.l3, new MockVerifier());

        expect(ctx.status).assertEqual(IntentExecutionStatus.ACTIVE);
        expect(ctx.intentId).assertEqual('test-intent-001');
        expect(ctx.executions.length).assertEqual(0);
        expect(ctx.executionInfo.leftExecutionCount).assertEqual(10);
        expect(ctx.executionInfo.leftAmount).assertEqual(500000);
      });

      it('activateIntent_usesInjectedClock_forPeriodIndex', 0, () => {
        // Periodic intent with a 1-second period anchored so FIXED_NOW
        // falls in period 61 and FIXED_NOW + 1500 in period 62: the initial
        // executionInfo must reflect the injected activation instant, not
        // the wall clock (which would land in a far later period). The
        // chain is stamped at FIXED_NOW so the later activation instant
        // also satisfies the not-earlier-than-L3-timestamp rule.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.anchorTime = FIXED_NOW - 61000;
        request.structuredIntent.constraints.periodSeconds = 1;
        request.structuredIntent.constraints.maxExecutionCountPerPeriod = 5;
        const lifecycle = createFullLifecycle(request, FIXED_NOW);

        const ctx = A4p.activateIntent(lifecycle.l3, new MockVerifier(), FIXED_NOW + 1500);

        expect(ctx.executionInfo.periodIndex).assertEqual(62);
        expect(ctx.executionInfo.leftPeriodExecutionCount).assertEqual(5);
      });

      it('activateIntent_fails_whenVerificationFails', 0, () => {
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.activateIntent(lifecycle.l3, new MockVerifier(false));
        });
      });

      it('activateIntent_fails_whenVerifierReturnsNoResult', 0, () => {
        // The lifecycle path goes through the same null-result guard as the
        // query path (runVerifier): no leaked raw TypeError.
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.activateIntent(lifecycle.l3, new NoResultVerifier());
        });
      });

      it('activateIntent_fails_whenIntentL3HasNoSignature', 0, () => {
        const lifecycle = createFullLifecycle();
        lifecycle.l3.sign = undefined;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.activateIntent(lifecycle.l3, new MockVerifier());
        });
      });

      it('activateIntent_fails_whenL3StructurallyInvalid', 0, () => {
        // A broken chain fails the structural check before verification:
        // even a verifier that accepts everything must not activate a
        // malformed snapshot (a non-positive totalMaxAmount would make the
        // amount-quota checks silently pass).
        const lifecycle = createFullLifecycle();
        lifecycle.l3.intentL2.intentL1.structuredIntent.constraints.totalMaxAmount = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.activateIntent(lifecycle.l3, new MockVerifier());
        });
      });

      it('activateIntent_fails_whenReceivedL3TimestampEarlierThanL2', 0, () => {
        // signIntent refuses to produce a regressive L3 itself (see
        // signIntent_fails_whenNowEarlierThanL2Timestamp), but a
        // non-compliant signer can still hand one over: activation
        // validates the received chain and rejects it before freezing the
        // snapshot. (Tampering breaks the signature, but structural
        // validation fires first.)
        const lifecycle = createFullLifecycle();
        lifecycle.l3.timestamp = lifecycle.l3.intentL2.timestamp - 1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.activateIntent(lifecycle.l3, new MockVerifier());
        });
      });

      it('activateIntent_fails_whenNowEarlierThanL3Timestamp', 0, () => {
        // The chain's monotonic rule extends to the activation step: an
        // activation instant before the L3's signing timestamp would create
        // a context for an authorization that did not exist yet (also the
        // default-clock guard against a wall clock stepped back past the
        // L3 stamp).
        const lifecycle = createFullLifecycle(createTestCreateIntentRequestAround(FIXED_NOW), FIXED_NOW);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.activateIntent(lifecycle.l3, new MockVerifier(), FIXED_NOW - 1);
        });
      });

      it('activateIntent_allowsNowEqualToL3Timestamp', 0, () => {
        // Equality is allowed, matching the non-strict monotonic stamps of
        // issueIntent/signIntent: activating in the same millisecond the
        // L3 was signed is legitimate.
        const lifecycle = createFullLifecycle(createTestCreateIntentRequestAround(FIXED_NOW), FIXED_NOW);

        const ctx = A4p.activateIntent(lifecycle.l3, new MockVerifier(), FIXED_NOW);

        expect(ctx.status).assertEqual(IntentExecutionStatus.ACTIVE);
        expect(ctx.intentId).assertEqual('test-intent-001');
      });

      it('activateIntent_recordsActivatedTime', 0, () => {
        // The activation instant is recorded as the context's
        // activatedTime — the floor createExecution judges reservations
        // against.
        const lifecycle = createFullLifecycle();

        const ctx = A4p.activateIntent(lifecycle.l3, new MockVerifier(), FIXED_NOW + 1000);

        expect(ctx.activatedTime).assertEqual(FIXED_NOW + 1000);
      });

      it('activateIntent_fails_whenL3VersionUnsupported', 0, () => {
        const lifecycle = createFullLifecycle();
        lifecycle.l3.version = '9.9';

        expectThrows(A4pErrorCode.A4P_UNSUPPORTED_VERSION, () => {
          A4p.activateIntent(lifecycle.l3, new MockVerifier());
        });
      });

      it('activateIntent_fails_whenNestedL2VersionUnsupported', 0, () => {
        const lifecycle = createFullLifecycle();
        lifecycle.l3.intentL2.version = '9.9';

        expectThrows(A4pErrorCode.A4P_UNSUPPORTED_VERSION, () => {
          A4p.activateIntent(lifecycle.l3, new MockVerifier());
        });
      });

      it('activateIntent_freezesAuthorizationSnapshot', 0, () => {
        const lifecycle = createFullLifecycle();

        // The snapshot is frozen: tampering with quota-affecting
        // constraints after activation must throw instead of silently
        // moving limits.
        let threw: boolean = false;
        try {
          lifecycle.ctx.authorizedIntent.intentL2.intentL1.structuredIntent.constraints
            .maxExecuteCount = 999;
        } catch (e) {
          threw = true;
        }
        expect(threw).assertTrue();
      });
    });

    describe('verifyIntentAuthenticity', () => {
      it('verifyIntentAuthenticity_success', 0, () => {
        const lifecycle = createFullLifecycle();

        const result: VerificationResult =
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());

        expect(result.valid).assertTrue();
      });

      it('verifyIntentAuthenticity_returnsInvalidResult_whenVerificationFails', 0, () => {
        // A pure query: an invalid verdict is returned to the caller (with
        // the verifier's own code), not thrown as A4P_VERIFY_FAIL.
        const lifecycle = createFullLifecycle();

        const result: VerificationResult =
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier(false));

        expect(result.valid).assertFalse();
        expect(result.code).assertEqual(A4pErrorCode.A4P_VERIFY_FAIL);
      });

      it('verifyIntentAuthenticity_throws_whenVerifierThrows', 0, () => {
        // A malfunctioning verifier is an error, not a verdict: it still
        // surfaces as A4P_VERIFY_FAIL, never as a leaked raw exception.
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.verifyIntentAuthenticity(lifecycle.l3, new FailingVerifier());
        });
      });

      it('verifyIntentAuthenticity_throws_whenVerifierReturnsNoResult', 0, () => {
        // A verifier returning no verdict at all is the same failure class
        // as a throwing one: A4P_VERIFY_FAIL, never a raw TypeError from
        // reading the absent result.
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.verifyIntentAuthenticity(lifecycle.l3, new NoResultVerifier());
        });
      });

      it('verifyIntentAuthenticity_returnsInvalidResult_whenL2ContentTampered', 0, () => {
        const lifecycle = createFullLifecycle();

        // The verified bytes are the embedded L2's own: tampering with L2
        // content must come back as an invalid verdict on the returned
        // result.
        lifecycle.l3.intentL2.userIdentity.id = 'tampered-uid';

        const result: VerificationResult =
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());

        expect(result.valid).assertFalse();
      });

      it('verifyIntentAuthenticity_returnsInvalidResult_whenNestedL1SignTampered', 0, () => {
        const lifecycle = createFullLifecycle();

        // The L2 signature must cover the nested L1's signature:
        // tampering with intentL1.sign.sign must come back as an
        // invalid verdict on the returned result.
        const nestedL1: IntentL1 = lifecycle.l3.intentL2.intentL1;
        if (!nestedL1.sign) {
          throw new Error('Expected sign to be present on the nested L1');
        }
        nestedL1.sign.sign = 'tampered-signature';

        const result: VerificationResult =
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());

        expect(result.valid).assertFalse();
      });

      it('verifyIntentAuthenticity_succeeds_whenIntentL3HasNoSignature', 0, () => {
        // The L3 is only the carrier: its own (user) signature is
        // activateIntent's to verify, not this query's — an unsigned L3
        // still carries a verifiable L2.
        const lifecycle = createFullLifecycle();
        lifecycle.l3.sign = undefined;

        const result: VerificationResult =
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());

        expect(result.valid).assertTrue();
      });

      it('verifyIntentAuthenticity_succeeds_whenL3CarrierFieldsTampered', 0, () => {
        // Carrier-level noise — a tampered L3 signature, a foreign L3
        // version, a regressive L3 timestamp, a missing signingEnv — must
        // not touch the verdict: the query is about the embedded L2.
        const lifecycle = createFullLifecycle();
        if (!lifecycle.l3.sign) {
          throw new Error('Expected sign to be present on the L3');
        }
        lifecycle.l3.sign.sign = 'tampered-signature';
        lifecycle.l3.version = '0.9';
        lifecycle.l3.timestamp = -1;
        lifecycle.l3.signingEnv = JSON.parse('null') as SigningEnv;

        const result: VerificationResult =
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());

        expect(result.valid).assertTrue();
      });

      it('verifyIntentAuthenticity_fails_whenIntentL3DoesNotContainIntentL2', 0, () => {
        // The one L3-level requirement: the carrier must contain the L2
        // whose signature is verified.
        const lifecycle = createFullLifecycle();
        lifecycle.l3.intentL2 = JSON.parse('null') as IntentL2;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());
        });
      });

      it('verifyIntentAuthenticity_fails_whenL2StructurallyInvalid', 0, () => {
        // Same structural gate as signIntent, but on the verified L2: it
        // fires before verification, independent of the verifier's verdict.
        const lifecycle = createFullLifecycle();
        lifecycle.l3.intentL2.timestamp = -1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());
        });
      });

      it('verifyIntentAuthenticity_fails_whenL2VersionUnsupported', 0, () => {
        const lifecycle = createFullLifecycle();
        lifecycle.l3.intentL2.version = '0.9';

        expectThrows(A4pErrorCode.A4P_UNSUPPORTED_VERSION, () => {
          A4p.verifyIntentAuthenticity(lifecycle.l3, new MockVerifier());
        });
      });
    });

    describe('revokeIntent', () => {
      it('revokeIntent_success', 0, () => {
        const lifecycle = createFullLifecycle();

        const ctx = A4p.revokeIntent(lifecycle.ctx);

        expect(ctx.status).assertEqual(IntentExecutionStatus.REVOKED);
      });

      it('revokeIntent_fails_whenNotActive', 0, () => {
        const lifecycle = createFullLifecycle();
        lifecycle.ctx.status = IntentExecutionStatus.REVOKED;

        expectThrows(A4pErrorCode.A4P_INVALID_STATUS, () => {
          A4p.revokeIntent(lifecycle.ctx);
        });
      });

      it('revokeIntent_allowsPendingExecutionToComplete', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);

        A4p.revokeIntent(lifecycle.ctx);

        const ctx = A4p.completeExecution(lifecycle.ctx, execution.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.COMPLETED);
      });

      it('revokeIntent_allowsPendingExecutionToCancel', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);

        A4p.revokeIntent(lifecycle.ctx);

        const ctx = A4p.cancelExecution(lifecycle.ctx, execution.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.CANCELED);
      });

      it('revokeIntent_blocksNewReservations', 0, () => {
        const lifecycle = createFullLifecycle();

        A4p.revokeIntent(lifecycle.ctx);

        const execution = createTestExecution();
        expectThrows(A4pErrorCode.A4P_INVALID_STATUS, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

    });

    describe('createExecution', () => {
      it('createExecution_success', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();

        const ctx = A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);

        expect(ctx.executions.length).assertEqual(1);
        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.PENDING);
        // Execution info is re-derived from the ledger: one reserved order
        // of 5000 against a 500000 budget and 10 allowed executions.
        expect(ctx.executionInfo.leftExecutionCount).assertEqual(9);
        expect(ctx.executionInfo.leftAmount).assertEqual(495000);
        expect(ctx.executionInfo.leftPeriodExecutionCount).assertEqual(9);
      });

      it('createExecution_generatesId_whenNotProvided', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution: Execution = {
          action: IntentActionType.AI_PAY,
          executionContent: createTestOrder()
        };

        const ctx = A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);

        // The generated ID is recorded both as the ledger key and on the
        // stored copy of the execution.
        expect(ctx.executions[0].executionId !== undefined).assertTrue();
        expect(ctx.executions[0].executionId.length).assertLarger(0);
        expect(ctx.executions[0].execution.id).assertEqual(ctx.executions[0].executionId);
      });

      it('createExecution_doesNotMutateCallerObject', 0, () => {
        // The id is resolved locally and lands only on the ledger's deep
        // copy: neither a successful nor a rejected reservation may write
        // an id back to the caller's object.
        const lifecycle = createFullLifecycle();

        const execNoId: Execution = {
          action: IntentActionType.AI_PAY,
          executionContent: createTestOrder()
        };
        const ctx = A4p.createExecution(lifecycle.ctx, execNoId, FIXED_NOW);
        expect(execNoId.id === undefined).assertTrue();
        expect(ctx.executions[0].execution.id).assertEqual(ctx.executions[0].executionId);

        const rejectedOrder = createTestOrder();
        rejectedOrder.amount = 200000; // exceeds singleOrderMaxAmount (100000)
        const execRejected: Execution = {
          action: IntentActionType.AI_PAY,
          executionContent: rejectedOrder
        };
        expectThrows(A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED, () => {
          A4p.createExecution(lifecycle.ctx, execRejected, FIXED_NOW);
        });
        expect(execRejected.id === undefined).assertTrue();
        expect(lifecycle.ctx.executions.length).assertEqual(1);
      });

      it('createExecution_fails_whenDuplicateOrderId', 0, () => {
        // orderId is the payment idempotency key: the same order cannot
        // reserve quota twice, even under a fresh execution id.
        const lifecycle = createFullLifecycle();
        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);

        const exec2 = createTestExecution();
        exec2.executionContent.orderId = exec1.executionContent.orderId;

        expectThrows(A4pErrorCode.A4P_DUPLICATE_ID, () => {
          A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);
        });
        expect(lifecycle.ctx.executions.length).assertEqual(1);
      });

      it('createExecution_duplicateOrderId_countsCanceledRecords', 0, () => {
        // The idempotency check scans the whole ledger — canceled records
        // included — so a canceled reservation cannot be re-reserved under
        // a fresh execution id.
        const lifecycle = createFullLifecycle();
        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        A4p.cancelExecution(lifecycle.ctx, exec1.id, FIXED_NOW);

        const exec2 = createTestExecution();
        exec2.executionContent.orderId = exec1.executionContent.orderId;

        expectThrows(A4pErrorCode.A4P_DUPLICATE_ID, () => {
          A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenExecutionIdIsBlank', 0, () => {
        // A blank (whitespace-only) id must not be silently regenerated.
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        exec.id = '   ';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        });
        expect(lifecycle.ctx.executions.length).assertEqual(0);
      });

      it('createExecution_fails_whenOrderIdIsBlank', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        exec.executionContent.orderId = '   ';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenNowIsInvalid', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();

        // Negative, fractional, and unsafe-integer clocks are rejected.
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, exec, -1);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW + 0.5);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, exec, 9007199254740992);
        });
        expect(lifecycle.ctx.executions.length).assertEqual(0);
      });

      it('createExecution_fails_whenDuplicateId', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec1 = createTestExecution();
        exec1.id = 'duplicate-exec-id';
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);

        const exec2 = createTestExecution();
        exec2.id = 'duplicate-exec-id';

        expectThrows(A4pErrorCode.A4P_DUPLICATE_ID, () => {
          A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenActionIsInvalid', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.action = 99 as Object as IntentActionType;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenAmountExceedsSingleOrderLimit', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.amount = 200000;

        expectThrows(A4pErrorCode.A4P_SINGLE_ORDER_AMOUNT_EXCEEDED, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenAmountExceedsTotalLimit', 0, () => {
        const lifecycle = createFullLifecycle();

        for (let i = 0; i < 5; i++) {
          const exec = createTestExecution();
          exec.id = 'exec-' + (i + 1);
          exec.executionContent.amount = 100000;
          A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        }

        const exec6 = createTestExecution();
        exec6.id = 'exec-6';
        exec6.executionContent.amount = 100000;

        expectThrows(A4pErrorCode.A4P_AMOUNT_EXCEEDED, () => {
          A4p.createExecution(lifecycle.ctx, exec6, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenCurrencyMismatch', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.currency = 'USD';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenCurrencyMismatch_beatsQuotaChecks', 0, () => {
        // Currency is part of the request's consistency with the frozen
        // snapshot, so it is checked before any quota accounting: a wrong
        // currency must report A4P_INVALID_PARAMETER even when the amount
        // also exceeds the per-order cap.
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.currency = 'USD';
        execution.executionContent.amount = 200000;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenBeforeValidFrom', 0, () => {
        // Build a request whose validFrom lies in the future relative to
        // the fixed clock (the frozen snapshot cannot be tampered with).
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.validFrom = FIXED_NOW + 3600000;
        request.structuredIntent.constraints.validUntil = FIXED_NOW + 7200000;
        const lifecycle = createFullLifecycle(request);

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY, () => {
          A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW);
        });
      });

      it('createExecution_fails_whenBeforeActivatedTime', 0, () => {
        // The activation instant is a reservation floor alongside
        // validFrom: a now earlier than activatedTime is rejected even
        // though the signed validity window has already opened (validFrom
        // is a minute before FIXED_NOW). Equality is allowed — the floor
        // is non-strict, like validFrom.
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_TOO_EARLY, () => {
          A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW - 1);
        });

        const ctx = A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW);
        expect(ctx.executions.length).assertEqual(1);
      });

      it('createExecution_fails_whenAfterValidUntil', 0, () => {
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.validFrom = FIXED_NOW - 7200000;
        request.structuredIntent.constraints.validUntil = FIXED_NOW - 3600000;
        // Keep the anchor at (or before) the shifted validFrom: an anchor
        // later than validFrom is rejected at intent creation.
        request.structuredIntent.constraints.anchorTime = FIXED_NOW - 7200000;
        const lifecycle = createFullLifecycle(request);

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_EXPIRED, () => {
          A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW);
        });
        // The failed attempt must lazily mark the intent as EXPIRED.
        expect(lifecycle.ctx.status).assertEqual(IntentExecutionStatus.EXPIRED);
      });

      it('createExecution_duplicateOrder_afterExpiry_reportsExpiryAndFlips', 0, () => {
        // The validity window is checked before duplicate ids: replaying a
        // used orderId after expiry reports the expiry (and flips the lazy
        // EXPIRED status), not the duplicate.
        const lifecycle = createFullLifecycle();
        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);

        const exec2 = createTestExecution();
        exec2.executionContent.orderId = exec1.executionContent.orderId;

        expectThrows(A4pErrorCode.A4P_TIMESTAMP_EXPIRED, () => {
          A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW + 7200000);
        });
        expect(lifecycle.ctx.status).assertEqual(IntentExecutionStatus.EXPIRED);
        expect(lifecycle.ctx.executions.length).assertEqual(1);
      });

      it('createExecution_fails_whenExecutionCountExceeded', 0, () => {
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecuteCount = 1;
        const lifecycle = createFullLifecycle(request);

        A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW);

        expectThrows(A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED, () => {
          A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW);
        });
      });

      it('createExecution_cancelReleasesCountSlot', 0, () => {
        // maxExecuteCount caps non-canceled executions: reserve → cancel →
        // reserve frees the count slot up again.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecuteCount = 1;
        const lifecycle = createFullLifecycle(request);

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        A4p.cancelExecution(lifecycle.ctx, exec1.id, FIXED_NOW);
        expect(lifecycle.ctx.executions.length).assertEqual(1); // ledger stays append-only
        expect(lifecycle.ctx.executionInfo.leftExecutionCount).assertEqual(1); // ...but the slot is released

        const exec2 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);

        // The replacement execution occupies the released slot.
        expectThrows(A4pErrorCode.A4P_EXECUTION_COUNT_EXCEEDED, () => {
          A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW);
        });
      });

      it('createExecution_fails_whenIntentNotActive', 0, () => {
        const lifecycle = createFullLifecycle();
        lifecycle.ctx.status = IntentExecutionStatus.REVOKED;

        expectThrows(A4pErrorCode.A4P_INVALID_STATUS, () => {
          A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW);
        });
      });

      it('createExecution_fails_whenOrderIdIsEmpty', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.orderId = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenOrderAmountIsNegative', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.amount = -100;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenOrderAmountIsZero', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.amount = 0;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenOrderAmountIsDecimal', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.amount = 99.99;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenOrderCurrencyIsEmpty', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.currency = '';

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenOrderCartIsEmpty', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.cartItems = [];

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenOrderTimestampIsNegative', 0, () => {
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.timestamp = -1;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenCartProductNotInWhitelist', 0, () => {
        // Authorization scope enforcement: an AI-pay intent authorizes
        // only whitelisted products — an out-of-scope product is a scope
        // violation (A4P_PRODUCT_NOT_AUTHORIZED), distinct from the
        // order-template mismatch of A4P_CART_MISMATCH.
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        // Different seller/sku than the whitelisted test product.
        execution.executionContent.cartItems = [{
          product: {
            seller: { id: 'seller-999' },
            spusku: { spuId: 'spu-999', skuId: 'sku-999' }
          },
          quantity: 1
        }];

        expectThrows(A4pErrorCode.A4P_PRODUCT_NOT_AUTHORIZED, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenCartQuantityDiffersFromTemplate', 0, () => {
        // The resource is an order template: the cart item's quantity must
        // equal the group's per-execution quantity exactly — more and less
        // are both a mismatch (the user signed exactly this purchase).
        const lifecycle = createFullLifecycle();
        const execution = createTestExecution();
        execution.executionContent.cartItems[0].quantity = 2;

        expectThrows(A4pErrorCode.A4P_CART_MISMATCH, () => {
          A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);
        });

        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.resource.itemsWhiteList[0].quantity = 2;
        const lifecycle2 = createFullLifecycle(request);
        const execution2 = createTestExecution();
        execution2.executionContent.cartItems[0].quantity = 1;

        expectThrows(A4pErrorCode.A4P_CART_MISMATCH, () => {
          A4p.createExecution(lifecycle2.ctx, execution2, FIXED_NOW);
        });
      });

      it('createExecution_fails_whenCartDoesNotCoverGroupsExactly', 0, () => {
        // AND across groups: the cart must carry one item per group — a
        // missing group's item and a second item on an already-covered
        // group are both a mismatch.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.resource.itemsWhiteList = [
          { whiteList: [createTestProductWithIds('seller-001', 'spu-001', 'sku-001')], quantity: 1 },
          { whiteList: [createTestProductWithIds('seller-002', 'spu-002', 'sku-002')], quantity: 2 }
        ];
        const lifecycle = createFullLifecycle(request);

        // Fewer items than groups: group 1 stays uncovered.
        const missingGroupOrder = createTestExecution();
        missingGroupOrder.executionContent.cartItems = [
          { product: createTestProductWithIds('seller-001', 'spu-001', 'sku-001'), quantity: 1 }
        ];
        expectThrows(A4pErrorCode.A4P_CART_MISMATCH, () => {
          A4p.createExecution(lifecycle.ctx, missingGroupOrder, FIXED_NOW);
        });

        // Same length, but both items land on group 0.
        const duplicateGroupOrder = createTestExecution();
        duplicateGroupOrder.executionContent.cartItems = [
          { product: createTestProductWithIds('seller-001', 'spu-001', 'sku-001'), quantity: 1 },
          { product: createTestProductWithIds('seller-001', 'spu-001', 'sku-001'), quantity: 1 }
        ];
        expectThrows(A4pErrorCode.A4P_CART_MISMATCH, () => {
          A4p.createExecution(lifecycle.ctx, duplicateGroupOrder, FIXED_NOW);
        });
      });

      it('createExecution_acceptsCartMatchingTemplateInAnyOrder', 0, () => {
        // The cart and the whitelist groups need not be in the same
        // order: a product determines its group, so any permutation of
        // the template's items is accepted.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.resource.itemsWhiteList = [
          { whiteList: [createTestProductWithIds('seller-001', 'spu-001', 'sku-001')], quantity: 2 },
          { whiteList: [createTestProductWithIds('seller-002', 'spu-002', 'sku-002')], quantity: 1 }
        ];
        const lifecycle = createFullLifecycle(request);

        const execution = createTestExecution();
        execution.executionContent.cartItems = [
          { product: createTestProductWithIds('seller-002', 'spu-002', 'sku-002'), quantity: 1 },
          { product: createTestProductWithIds('seller-001', 'spu-001', 'sku-001'), quantity: 2 }
        ];
        const ctx = A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);

        expect(ctx.executions.length).assertEqual(1);
      });

      it('createExecution_acceptsAnyWhitelistedProductPerGroup', 0, () => {
        // OR within a group: any single product of the group's whitelist
        // satisfies the group's slot.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.resource.itemsWhiteList = [{
          whiteList: [
            createTestProductWithIds('seller-001', 'spu-001', 'sku-001'),
            createTestProductWithIds('seller-001', 'spu-001', 'sku-002')
          ],
          quantity: 1
        }];
        const lifecycle = createFullLifecycle(request);

        const execution = createTestExecution();
        execution.executionContent.cartItems = [{
          product: createTestProductWithIds('seller-001', 'spu-001', 'sku-002'),
          quantity: 1
        }];
        const ctx = A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);

        expect(ctx.executions.length).assertEqual(1);
      });

      it('createExecution_allowsRepeatedTemplateOrders', 0, () => {
        // The group quantity is per-execution, not a budget: identical
        // orders repeat freely, bounded by the constraint caps (count,
        // amount, period), never by an accumulated product quantity.
        const lifecycle = createFullLifecycle();

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        const exec2 = createTestExecution();
        const ctx = A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);

        expect(ctx.executions.length).assertEqual(2);
      });

      it('createExecution_periodRollover_resetsOrderCount', 0, () => {
        // Deterministic period rollover: anchor one full period before the
        // fixed clock with a 1-second period, so FIXED_NOW is in period 61.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.anchorTime = FIXED_NOW - 61000; // 61 s before FIXED_NOW
        request.structuredIntent.constraints.periodSeconds = 1;
        request.structuredIntent.constraints.maxExecutionCountPerPeriod = 1;
        const lifecycle = createFullLifecycle(request);

        const exec1 = createTestExecution();
        const r1 = A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        expect(r1.executionInfo.periodIndex).assertEqual(61);
        expect(r1.executionInfo.leftPeriodExecutionCount).assertEqual(0);

        // Same period: the per-period execution count is exhausted.
        const exec2 = createTestExecution();
        expectThrows(A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED, () => {
          A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);
        });

        // Advance the evaluation instant into the next period: reset.
        const r2 = A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW + 1500);
        expect(r2.executionInfo.periodIndex).assertEqual(62);
        expect(r2.executionInfo.leftPeriodExecutionCount).assertEqual(0);
      });

      it('createExecution_fails_whenPeriodOrderCountExceeded', 0, () => {
        const lifecycle = createFullLifecycle(createPeriodicRequest(2));

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        const exec2 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);

        const exec3 = createTestExecution();
        expectThrows(A4pErrorCode.A4P_PERIOD_EXECUTION_COUNT_EXCEEDED, () => {
          A4p.createExecution(lifecycle.ctx, exec3, FIXED_NOW);
        });
      });

      it('createExecution_periodQuotaReleasesOnCancel', 0, () => {
        // Cancelling an order in the current period frees a period slot.
        const lifecycle = createFullLifecycle(createPeriodicRequest(1));

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        A4p.cancelExecution(lifecycle.ctx, exec1.id, FIXED_NOW);

        const exec2 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);
      });

      it('createExecution_nonPeriodicIntent_omitsPeriodAccounting', 0, () => {
        // Non-periodic intents omit the period rule: records carry no
        // periodIndex, the execution info carries no period figures, and
        // there is no per-period cap to exhaust.
        const lifecycle = createFullLifecycle(createOptionalFieldsOmittedRequest());

        const exec1 = createTestExecution();
        const ctx = A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        // A second reservation at the same instant: no period cap fires.
        const exec2 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);

        expect(ctx.executions[0].periodIndex === undefined).assertTrue();
        expect(ctx.executionInfo.periodIndex === undefined).assertTrue();
        expect(ctx.executionInfo.leftPeriodExecutionCount === undefined).assertTrue();
      });

      it('createExecution_succeeds_whenSingleOrderCapOmitted', 0, () => {
        // Without singleOrderMaxAmount an order above the usual per-order
        // cap is fine as long as the total budget holds.
        const lifecycle = createFullLifecycle(createOptionalFieldsOmittedRequest());
        const execution = createTestExecution();
        execution.executionContent.amount = 400000; // > factory default cap (100000), < total (500000)

        const ctx = A4p.createExecution(lifecycle.ctx, execution, FIXED_NOW);

        expect(ctx.executionInfo.leftAmount).assertEqual(100000);
      });

      it('createExecution_succeeds_whenExecuteCountCapOmitted', 0, () => {
        // Without maxExecuteCount executions are budget-bound only: the
        // count check is skipped and leftExecutionCount is omitted.
        const lifecycle = createFullLifecycle(createOptionalFieldsOmittedRequest());

        for (let i = 0; i < 11; i++) { // 11 > the factory default cap of 10
          const exec = createTestExecution();
          exec.id = 'exec-' + i;
          A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        }

        expect(lifecycle.ctx.executions.length).assertEqual(11);
        expect(lifecycle.ctx.executionInfo.leftExecutionCount === undefined).assertTrue();
        expect(lifecycle.ctx.executionInfo.leftAmount).assertEqual(445000); // 500000 - 11 * 5000
      });
    });

    describe('completeExecution', () => {
      it('completeExecution_success', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);

        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.COMPLETED);
        expect(ctx.executions[0].completedAt !== undefined).assertTrue();
      });

      it('completeExecution_updatesPeriodFigures', 0, () => {
        // Completion books no quota (a PENDING record already counts) but
        // moves the evaluation instant: the snapshot's period figures are
        // re-derived for the period containing the completion instant,
        // not the one containing the reservation.
        const lifecycle = createFullLifecycle(createPeriodicRequest(10));
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        expect(lifecycle.ctx.executionInfo.periodIndex).assertEqual(0);

        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW + 7200000);

        // Two hours later is period 2 (1-hour periods anchored a minute
        // before FIXED_NOW); the completed record sits in period 0, so
        // period 2's budget reads full again.
        expect(ctx.executionInfo.periodIndex).assertEqual(2);
        expect(ctx.executionInfo.leftPeriodExecutionCount).assertEqual(10);
      });

      it('completeIntent_completes_whenAllExecutionsDoneAndCountReached', 0, () => {
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecuteCount = 1;
        const lifecycle = createFullLifecycle(request);

        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.status).assertEqual(IntentExecutionStatus.CONSUMED);
      });

      it('completeExecution_completes_whenAmountExhausted', 0, () => {
        // The amount trigger fires independently of the count: one order
        // consuming the whole totalMaxAmount flips the intent to CONSUMED
        // even though the count cap is far from reached.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.totalMaxAmount = 5000;
        const lifecycle = createFullLifecycle(request);

        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.status).assertEqual(IntentExecutionStatus.CONSUMED);
      });

      it('completeExecution_completes_whenAmountExhausted_andCountCapOmitted', 0, () => {
        // Without maxExecuteCount auto-completion rests on the amount
        // trigger alone — an uncapped intent still consumes once its
        // totalMaxAmount is exhausted.
        const request = createOptionalFieldsOmittedRequest();
        request.structuredIntent.constraints.totalMaxAmount = 5000;
        const lifecycle = createFullLifecycle(request);

        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.status).assertEqual(IntentExecutionStatus.CONSUMED);
      });

      it('completeExecution_completes_whenLastPendingFinalizesAmount', 0, () => {
        // Two orders together exhaust the budget: completing the first
        // leaves the second pending (the flip must not strand it — finalize
        // APIs require a non-CONSUMED status), and completing the last one
        // finalizes the ledger and consumes the intent.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.totalMaxAmount = 10000;
        const lifecycle = createFullLifecycle(request);

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        const exec2 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);

        const ctx1 = A4p.completeExecution(lifecycle.ctx, exec1.id, FIXED_NOW);
        expect(ctx1.status).assertEqual(IntentExecutionStatus.ACTIVE);

        const ctx2 = A4p.completeExecution(lifecycle.ctx, exec2.id, FIXED_NOW);
        expect(ctx2.status).assertEqual(IntentExecutionStatus.CONSUMED);
      });

      it('completeExecution_doesNotAutoComplete_fromRevoked', 0, () => {
        // Auto-complete applies only from ACTIVE: a revoked intent keeps
        // its status when its last pending record finalizes — the
        // revocation is a fact about the intent's history.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecuteCount = 1;
        const lifecycle = createFullLifecycle(request);

        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        lifecycle.ctx.status = IntentExecutionStatus.REVOKED;
        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.COMPLETED);
        expect(ctx.status).assertEqual(IntentExecutionStatus.REVOKED);
      });

      it('completeExecution_doesNotAutoComplete_fromExpired', 0, () => {
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecuteCount = 1;
        const lifecycle = createFullLifecycle(request);

        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        lifecycle.ctx.status = IntentExecutionStatus.EXPIRED;
        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.COMPLETED);
        expect(ctx.status).assertEqual(IntentExecutionStatus.EXPIRED);
      });

      it('completeExecution_remainsActive_whenCountNotReached', 0, () => {
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecuteCount = 5;
        const lifecycle = createFullLifecycle(request);

        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.status).assertEqual(IntentExecutionStatus.ACTIVE);
      });

      it('completeExecution_remainsActive_whenExecuteCountCapOmitted', 0, () => {
        // Without maxExecuteCount there is no count trigger for
        // auto-completion, and with the amount budget far from exhausted
        // the intent stays open for further executions.
        const lifecycle = createFullLifecycle(createOptionalFieldsOmittedRequest());
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);

        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.status).assertEqual(IntentExecutionStatus.ACTIVE);
      });

      it('completeExecution_succeeds_whenRevokedAfterReserve', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        lifecycle.ctx.status = IntentExecutionStatus.REVOKED;

        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.COMPLETED);
      });

      it('completeExecution_succeeds_whenExpiredAfterReserve', 0, () => {
        // Pending reservations made before expiry can still be finalized.
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        lifecycle.ctx.status = IntentExecutionStatus.EXPIRED;

        const ctx = A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.COMPLETED);
      });

      it('completeExecution_fails_whenNotFound', 0, () => {
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.completeExecution(lifecycle.ctx, 'non-existent', FIXED_NOW);
        });
      });

      it('completeExecution_fails_whenAlreadyCompleted', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expectThrows(A4pErrorCode.A4P_INVALID_STATUS, () => {
          A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);
        });
      });

      it('completeExecution_remainsActive_whenHasCanceledRecord', 0, () => {
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecuteCount = 2;
        const lifecycle = createFullLifecycle(request);

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        const exec2 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);
        A4p.cancelExecution(lifecycle.ctx, exec1.id, FIXED_NOW);

        const ctx = A4p.completeExecution(lifecycle.ctx, exec2.id, FIXED_NOW);

        // Canceled records do not count toward maxExecuteCount: only 1
        // non-canceled record < 2, so the intent stays active.
        expect(ctx.status).assertEqual(IntentExecutionStatus.ACTIVE);
      });
    });

    describe('cancelExecution', () => {
      it('cancelExecution_success', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);

        const ctx = A4p.cancelExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.CANCELED);
        // The ledger is append-only: the canceled record stays...
        expect(ctx.executions.length).assertEqual(1);
        // ...but its count slot is released (back to the default cap of 10).
        expect(ctx.executionInfo.leftExecutionCount).assertEqual(10);
      });

      it('cancelExecution_fails_whenNotFound', 0, () => {
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.cancelExecution(lifecycle.ctx, 'non-existent', FIXED_NOW);
        });
      });

      it('cancelExecution_fails_whenAlreadyCompleted', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        A4p.completeExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expectThrows(A4pErrorCode.A4P_INVALID_STATUS, () => {
          A4p.cancelExecution(lifecycle.ctx, exec.id, FIXED_NOW);
        });
      });

      it('cancelExecution_fails_whenAlreadyCanceled', 0, () => {
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        A4p.cancelExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expectThrows(A4pErrorCode.A4P_INVALID_STATUS, () => {
          A4p.cancelExecution(lifecycle.ctx, exec.id, FIXED_NOW);
        });
      });

      it('cancelExecution_succeeds_whenIntentExpired', 0, () => {
        // Pending reservations made before expiry can still be finalized:
        // cancel releases the quota the expired reservation still holds.
        const lifecycle = createFullLifecycle();
        const exec = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec, FIXED_NOW);
        lifecycle.ctx.status = IntentExecutionStatus.EXPIRED;

        const ctx = A4p.cancelExecution(lifecycle.ctx, exec.id, FIXED_NOW);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.CANCELED);
        expect(ctx.executionInfo.leftExecutionCount).assertEqual(10);
      });

      it('cancelExecution_succeeds_afterExpiryFlip', 0, () => {
        // The lazy EXPIRED flip happens in createExecution past validUntil;
        // afterwards the pending reservation can still be canceled, and the
        // released quota is visible in the re-derived executionInfo.
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.validFrom = FIXED_NOW - 60000;
        request.structuredIntent.constraints.validUntil = FIXED_NOW + 60000;
        const lifecycle = createFullLifecycle(request);

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        expectThrows(A4pErrorCode.A4P_TIMESTAMP_EXPIRED, () => {
          A4p.createExecution(lifecycle.ctx, createTestExecution(), FIXED_NOW + 120000);
        });
        expect(lifecycle.ctx.status).assertEqual(IntentExecutionStatus.EXPIRED);

        const ctx = A4p.cancelExecution(lifecycle.ctx, exec1.id, FIXED_NOW + 120000);

        expect(ctx.executions[0].status).assertEqual(ExecutionStatus.CANCELED);
        expect(ctx.executionInfo.leftExecutionCount).assertEqual(10);
      });

      it('cancelExecution_revertsCountPeriodAndAmount', 0, () => {
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.maxExecutionCountPerPeriod = 10;
        const lifecycle = createFullLifecycle(request);
        const exec1 = createTestExecution();
        exec1.executionContent.amount = 7000;
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);
        const exec2 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec2, FIXED_NOW);

        expect(lifecycle.ctx.executionInfo.leftPeriodExecutionCount).assertEqual(8);
        expect(lifecycle.ctx.executionInfo.leftAmount).assertEqual(488000);

        const ctx = A4p.cancelExecution(lifecycle.ctx, exec1.id, FIXED_NOW);

        expect(ctx.executionInfo.leftPeriodExecutionCount).assertEqual(9);
        expect(ctx.executionInfo.leftAmount).assertEqual(495000); // 7000 released
        // The count slot is released along with the amount and period slot.
        expect(ctx.executionInfo.leftExecutionCount).assertEqual(9);
      });

      it('cancelExecution_acrossPeriodRollover_doesNotGoNegative', 0, () => {
        // Canceling a record from an earlier period must not touch the
        // current period's counters (it is excluded by periodIndex).
        const request = createTestCreateIntentRequestAround(FIXED_NOW);
        request.structuredIntent.constraints.anchorTime = FIXED_NOW - 61000;
        request.structuredIntent.constraints.periodSeconds = 1;
        request.structuredIntent.constraints.maxExecutionCountPerPeriod = 5;
        const lifecycle = createFullLifecycle(request);

        const exec1 = createTestExecution();
        A4p.createExecution(lifecycle.ctx, exec1, FIXED_NOW);

        // Cancel from the next period, where nothing has been ordered yet.
        const ctx = A4p.cancelExecution(lifecycle.ctx, exec1.id, FIXED_NOW + 1500);

        expect(ctx.executionInfo.periodIndex).assertEqual(62);
        expect(ctx.executionInfo.leftPeriodExecutionCount).assertEqual(5);
      });
    });

    describe('nullInputGuards', () => {
      // The throw-on-failure convention covers null roots: a null input to
      // a public API must surface as A4pError (A4P_INVALID_PARAMETER),
      // never as a raw TypeError from dereferencing it. JSON.parse('null')
      // yields null while still typing as the parameter (the same typed-null
      // escape the codec uses).

      it('lifecycleApis_failAsA4pError_whenRootInputIsNull', 0, () => {
        const nullRequest: CreateIntentRequest = JSON.parse('null') as CreateIntentRequest;
        const nullIssueContext: IntentIssueContext = JSON.parse('null') as IntentIssueContext;
        const nullSigningEnv: SigningEnv = JSON.parse('null') as SigningEnv;
        const nullL1: IntentL1 = JSON.parse('null') as IntentL1;
        const nullL2: IntentL2 = JSON.parse('null') as IntentL2;
        const nullL3: IntentL3 = JSON.parse('null') as IntentL3;

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createIntent(nullRequest, new MockSigner());
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(nullL1, new MockVerifier(), createTestIntentIssueContext(), new MockSigner());
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.issueIntent(createTestL1(), new MockVerifier(), nullIssueContext, new MockSigner());
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.signIntent(nullL2, new MockVerifier(), createTestSigningEnv(), new MockSigner());
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.signIntent(createTestL2(), new MockVerifier(), nullSigningEnv, new MockSigner());
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.activateIntent(nullL3, new MockVerifier());
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.verifyIntentAuthenticity(nullL3, new MockVerifier());
        });
      });

      it('executionApis_failAsA4pError_whenContextOrExecutionIsNull', 0, () => {
        const nullContext: IntentExecutionContext = JSON.parse('null') as IntentExecutionContext;
        const nullExecution: Execution = JSON.parse('null') as Execution;
        const lifecycle = createFullLifecycle();

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.revokeIntent(nullContext);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(nullContext, createTestExecution(), FIXED_NOW);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.createExecution(lifecycle.ctx, nullExecution, FIXED_NOW);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.completeExecution(nullContext, 'some-exec', FIXED_NOW);
        });
        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4p.cancelExecution(nullContext, 'some-exec', FIXED_NOW);
        });
        // The valid context is untouched by the rejected calls.
        expect(lifecycle.ctx.executions.length).assertEqual(0);
        expect(lifecycle.ctx.status).assertEqual(IntentExecutionStatus.ACTIVE);
      });
    });
  });
}
