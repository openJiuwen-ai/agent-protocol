/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * A4P SDK - Public API Entry Point
 *
 * This is the main entry point of the A4P (Agent-to-Agent Payment) SDK.
 * It exports all public APIs, interfaces, and data types.
 *
 * @packageDocumentation
 */

// API
import { A4p } from './src/main/ets/api/a4p'
import { Signer } from './src/main/ets/api/signer'
import { SignatureVerifier } from './src/main/ets/api/verifier'

// Models
import {
  CreateIntentRequest, IntentIssueContext, VerificationResult, A4pError, A4pErrorCode, a4pErrorMessage
} from './src/main/ets/models/common'
import { PaymentConstraints } from './src/main/ets/models/constraint'
import { UserIdentity, AgentIdentity, ServiceProviderIdentity, SigningEnv } from './src/main/ets/models/identity'
import {
  StructuredIntent, IntentL1, IntentL2, IntentL3, SignatureInfo, IntentActionType, AlgorithmType
} from './src/main/ets/models/intent'
import { Product, ItemWhiteList, ShoppingItems, CartItem, SellerInfo, SpuSkuInfo } from './src/main/ets/models/product'
import {
  IntentExecutionStatus, ExecutionStatus,
  ExecutionRecord, PaymentExecutionInfo,
  IntentExecutionContext, Order, Execution
} from './src/main/ets/models/execution'

export {
  // Core API
  A4p,

  // Cryptography
  Signer, SignatureVerifier, VerificationResult,

  // Common types
  CreateIntentRequest, IntentIssueContext, A4pError, A4pErrorCode, a4pErrorMessage,

  // Constraints
  PaymentConstraints,

  // Products
  Product, ItemWhiteList, ShoppingItems, CartItem, SellerInfo, SpuSkuInfo,

  // Identity
  UserIdentity, AgentIdentity, ServiceProviderIdentity, SigningEnv,

  // Intent lifecycle
  StructuredIntent, IntentL1, IntentL2, IntentL3, SignatureInfo,
  IntentActionType, AlgorithmType,

  // Execution
  Order, Execution,
  IntentExecutionStatus, ExecutionStatus,
  ExecutionRecord, PaymentExecutionInfo as ExecutionInfo,
  IntentExecutionContext
}
