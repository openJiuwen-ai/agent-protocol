/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import {
  CreateIntentRequest, PaymentConstraints,
  UserIdentity, AgentIdentity, ServiceProviderIdentity, SigningEnv,
  StructuredIntent, IntentIssueContext, AlgorithmType,
  ItemWhiteList, ShoppingItems, Product,
  Execution, Order, IntentL1, IntentL2, IntentL3, IntentActionType, A4p
} from '../../../../../Index';
import { MockSigner, MockVerifier } from './MockSigner';
import { util } from '@kit.ArkTS';

// Note: all nested test data must be plain objects (object literals).
// canonicalStringify rejects class instances, so `new Xxx()` here would
// make every serialize/sign test fail with A4P_SERIALIZE_FAIL.

export function createTestProduct(): Product {
  return {
    seller: {
      id: 'seller-001'
    },
    spusku: {
      spuId: 'spu-001',
      skuId: 'sku-001'
    }
  };
}

export function createTestItemWhiteList(): ItemWhiteList {
  return {
    whiteList: [createTestProduct()],
    quantity: 1
  };
}

export function createTestShoppingItems(): ShoppingItems {
  return {
    itemsWhiteList: [createTestItemWhiteList()]
  };
}

/**
 * Payment constraints anchored around a fixed clock value, so tests can
 * inject the same clock into the execution APIs and get deterministic
 * validity-window and period-rollover behavior. All optional fields are
 * provided (a periodic intent with both caps); tests for the omitted
 * semantics build their own constraint literals.
 */
export function createTestPaymentConstraintsAround(now: number): PaymentConstraints {
  return {
    singleOrderMaxAmount: 100000,
    totalMaxAmount: 500000,
    currency: 'CNY',
    validFrom: now - 60000,
    validUntil: now + 3600000,
    maxExecuteCount: 10,
    anchorTime: now - 60000,
    periodSeconds: 3600,
    maxExecutionCountPerPeriod: 10
  };
}

export function createTestStructuredIntent(now: number): StructuredIntent {
  return {
    action: IntentActionType.AI_PAY,
    resource: createTestShoppingItems(),
    constraints: createTestPaymentConstraintsAround(now)
  };
}

export function createTestCreateIntentRequest(): CreateIntentRequest {
  return createTestCreateIntentRequestAround(Date.now());
}

/**
 * Creates a request whose constraints are anchored around `now`; combine
 * with the `now` parameter of the execution APIs for deterministic tests.
 */
export function createTestCreateIntentRequestAround(now: number): CreateIntentRequest {
  return {
    intentConversion: 'agent_conversion',
    intentSummary: 'Test payment intent',
    structuredIntent: createTestStructuredIntent(now),
    intentId: 'test-intent-001'
  };
}

export function createTestUserIdentity(): UserIdentity {
  return {
    identityProvider: 'test-provider',
    uid: 'user-001',
    additionalInfo: '{}'
  };
}

export function createTestAgentIdentity(): AgentIdentity {
  return {
    identityProvider: 'test-provider',
    id: 'agent-001',
    additionalInfo: '{}'
  };
}

export function createTestServiceProviderIdentity(): ServiceProviderIdentity {
  return {
    id: 'sp-001',
    additionalInfo: '{}'
  };
}

export function createTestSigningEnv(): SigningEnv {
  return {
    device: {
      model: 'TestDevice',
      os: 'HarmonyOS'
    },
    app: {
      name: 'TestApp',
      version: '1.0.0'
    }
  };
}

export function createTestIntentIssueContext(): IntentIssueContext {
  return {
    userIdentity: createTestUserIdentity(),
    agentIdentity: createTestAgentIdentity(),
    serviceProviderIdentity: createTestServiceProviderIdentity()
  };
}

export function createTestOrder(): Order {
  return {
    orderId: 'order-001',
    amount: 5000,
    currency: 'CNY',
    cartItems: [{
      product: createTestProduct(),
      quantity: 1
    }],
    timestamp: Date.now()
  };
}

export function createTestExecution(): Execution {
  return {
    action: IntentActionType.AI_PAY,
    id: util.generateRandomUUID(),
    version: '1.0',
    executionContent: createTestOrder()
  };
}

/**
 * Creates an L1 intent using the default test request and mock signer.
 */
export function createTestL1(request: CreateIntentRequest = createTestCreateIntentRequest()): IntentL1 {
  return A4p.createIntent(request, new MockSigner());
}

/**
 * Creates an L2 intent by issuing the given L1 (or a default L1).
 */
export function createTestL2(l1?: IntentL1): IntentL2 {
  const intentL1 = l1 ?? createTestL1();
  return A4p.issueIntent(intentL1, new MockVerifier(),
    createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));
}

/**
 * Creates an L3 intent by signing the given L2 (or a default L2).
 */
export function createTestL3(l2?: IntentL2): IntentL3 {
  const intentL2 = l2 ?? createTestL2();
  return A4p.signIntent(intentL2, new MockVerifier(),
    createTestSigningEnv(), new MockSigner(AlgorithmType.RSA3072, 'user-key-001'));
}
