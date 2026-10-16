/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

import { describe, it, expect } from '@ohos/hypium';
import {
  A4pHttp, buildErrorResponse, A4pHttpResponse, IntentActivationResponse
} from '../../../main/ets/internal/http';
import {
  A4p, A4pErrorCode, A4pError,
  IntentL1, IntentL2, IntentL3,
  PaymentConstraints, AlgorithmType
} from '../../../../Index';
import { MockSigner, MockVerifier } from './mock/MockSigner';
import {
  createTestIntentIssueContext,
  createTestSigningEnv,
  createTestL1, createTestL2, createTestL3
} from './mock/TestDataFactory';

function buildHttpResponse(statusCode: number, body: string): A4pHttpResponse {
  const headers: Map<string, string> = new Map<string, string>();
  headers.set('Content-Type', 'application/json');
  return {
    statusCode: statusCode,
    headers: headers,
    body: encodeUtf8ForTest(body)
  };
}

function createL1(): IntentL1 {
  return createTestL1();
}

function createL2(): IntentL2 {
  return createTestL2();
}

function createL3(): IntentL3 {
  return createTestL3();
}

function encodeUtf8ForTest(str: string): Uint8Array {
  const bytes: number[] = [];
  for (let i = 0; i < str.length; i++) {
    const c: number = str.charCodeAt(i);
    if (c < 0x80) {
      bytes.push(c);
    } else if (c < 0x800) {
      bytes.push(0xc0 | (c >> 6));
      bytes.push(0x80 | (c & 0x3f));
    } else {
      bytes.push(0xe0 | (c >> 12));
      bytes.push(0x80 | ((c >> 6) & 0x3f));
      bytes.push(0x80 | (c & 0x3f));
    }
  }
  return new Uint8Array(bytes);
}

function decodeUtf8ForTest(bytes: Uint8Array): string {
  let str: string = '';
  for (let i = 0; i < bytes.length; i++) {
    str += String.fromCharCode(bytes[i]);
  }
  return str;
}

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

// Class instance used to verify serialize rejects non-plain objects.
class CustomInfo {
  /** Arbitrary payload field */
  field: string = 'value';
}

export default function a4pHttpTest() {
  describe('A4pHttpTest', () => {

    describe('IntentProposal', () => {
      it('buildRequest_success', 0, () => {
        const l1 = createL1();

        const request = A4pHttp.buildIntentProposalRequest(l1);

        expect(request.method).assertEqual('POST');
        expect(request.path).assertEqual('/a4p/v1/intent/proposal');
        expect(request.headers.get('Content-Type')).assertEqual('application/a4p+json');
        expect(request.body.length).assertLarger(0);
      });

      it('parseRequest_success', 0, () => {
        const l1 = createL1();
        const request = A4pHttp.buildIntentProposalRequest(l1);

        const result = A4pHttp.parseIntentProposalRequest(request);

        expect(result.intentId).assertEqual(l1.intentId);
        expect(result.sign?.sign).assertEqual(l1.sign?.sign);
      });

      it('buildRequest_fails_whenSerializeFails', 0, () => {
        const l1 = createL1();
        const tainted: IntentL1 = {
          version: l1.version,
          intentId: l1.intentId,
          intentConversion: l1.intentConversion,
          intentSummary: l1.intentSummary,
          structuredIntent: {
            action: l1.structuredIntent.action,
            resource: l1.structuredIntent.resource,
            constraints: l1.structuredIntent.constraints
          },
          timestamp: l1.timestamp
        }
        tainted.sign = l1.sign;
        // canonicalStringify rejects class instances, forcing serialize to
        // fail; the builder throws A4pError per the throw-on-failure
        // convention.
        tainted.structuredIntent.constraints = new CustomInfo() as Object as PaymentConstraints;

        expectThrows(A4pErrorCode.A4P_SERIALIZE_FAIL, () => {
          A4pHttp.buildIntentProposalRequest(tainted);
        });
      });

      it('buildResponse_success', 0, () => {
        const l2 = createL2();

        const response = A4pHttp.buildIntentProposalResponse(l2);

        expect(response.statusCode).assertEqual(200);
        expect(response.headers.get('Content-Type')).assertEqual('application/a4p+json');
        // Envelope: {"data": "<canonical L2 JSON string>"}.
        expect(decodeUtf8ForTest(response.body).indexOf('"data"')).assertLarger(0);
      });

      it('buildErrorResponse_mapsVerifyFailTo401', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_VERIFY_FAIL));

        expect(response.statusCode).assertEqual(401);
        expect(decodeUtf8ForTest(response.body).indexOf('"code":2002')).assertLarger(0);
      });

      it('buildErrorResponse_mapsInvalidParameterTo400', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_INVALID_PARAMETER));

        expect(response.statusCode).assertEqual(400);
      });

      it('buildErrorResponse_mapsInvalidStatusTo409', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_INVALID_STATUS));

        expect(response.statusCode).assertEqual(409);
      });

      it('buildErrorResponse_mapsDuplicateIdTo409', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_DUPLICATE_ID));

        expect(response.statusCode).assertEqual(409);
        expect(decodeUtf8ForTest(response.body).indexOf('"code":1003')).assertLarger(0);
      });

      it('buildErrorResponse_mapsInternalErrorTo500', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_INTERNAL_ERROR));

        expect(response.statusCode).assertEqual(500);
      });

      it('buildErrorResponse_mapsUnsupportedVersionTo426', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_UNSUPPORTED_VERSION));

        expect(response.statusCode).assertEqual(426);
      });

      it('parseResponse_fails_whenHttpError', 0, () => {
        const body: string = '{"code":2002,"message":"Server error"}';
        const response = buildHttpResponse(500, body);

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4pHttp.parseIntentProposalResponse(response);
        });
      });

      it('parseResponse_fails_whenHttpErrorWithInvalidJson', 0, () => {
        const response = buildHttpResponse(502, 'bad gateway');

        expectThrows(A4pErrorCode.A4P_INTERNAL_ERROR, () => {
          A4pHttp.parseIntentProposalResponse(response);
        });
      });

      it('parseResponse_fails_whenErrorBodyWith200', 0, () => {
        const body: string = '{"code":2002,"message":"Verification failed"}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4pHttp.parseIntentProposalResponse(response);
        });
      });

      it('parseResponse_fails_whenDataIsNotValidIntentL2', 0, () => {
        const body: string = '{"data":"{\\"version\\":\\"1.0\\"}"}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pHttp.parseIntentProposalResponse(response);
        });
      });

      it('parseResponse_fails_whenDataFieldMissing', 0, () => {
        // A 2xx body carrying no data field must not surface as success.
        const body: string = '{"message":"ok"}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pHttp.parseIntentProposalResponse(response);
        });
      });

      it('parseResponse_mapsUnknownCodeToInternalError', 0, () => {
        // Codes on the wire that are not defined A4P error codes must not
        // leak into the enum as arbitrary numbers.
        const body: string = '{"code":424242,"message":"weird"}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_INTERNAL_ERROR, () => {
          A4pHttp.parseIntentProposalResponse(response);
        });
      });

      it('parseResponse_success_roundTrip', 0, () => {
        const l2 = createL2();
        const response = A4pHttp.buildIntentProposalResponse(l2);

        const parsed = A4pHttp.parseIntentProposalResponse(response);

        expect(parsed.intentL1.intentId).assertEqual(l2.intentL1.intentId);
        expect(parsed.sign?.sign).assertEqual(l2.sign?.sign);
      });
    });

    describe('IntentAuthorization', () => {
      it('buildRequest_success', 0, () => {
        const l2 = createL2();

        const request = A4pHttp.buildIntentAuthorizationRequest(l2);

        expect(request.method).assertEqual('POST');
        expect(request.path).assertEqual('/a4p/v1/intent/authorization');
      });

      it('parseRequest_success', 0, () => {
        const l2 = createL2();
        const request = A4pHttp.buildIntentAuthorizationRequest(l2);

        const result = A4pHttp.parseIntentAuthorizationRequest(request);

        expect(result.intentL1.intentId).assertEqual(l2.intentL1.intentId);
      });

      it('buildResponse_success', 0, () => {
        const l3 = createL3();

        const response = A4pHttp.buildIntentAuthorizationResponse(l3);

        expect(response.statusCode).assertEqual(200);
      });

      it('buildErrorResponse_failure', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_VERIFY_FAIL));

        expect(response.statusCode).assertEqual(401);
      });

      it('parseResponse_fails_whenHttpError', 0, () => {
        const body: string = '{"code":2002,"message":"Server error"}';
        const response = buildHttpResponse(500, body);

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4pHttp.parseIntentAuthorizationResponse(response);
        });
      });

      it('parseResponse_fails_whenErrorBodyWith200', 0, () => {
        const body: string = '{"code":2002,"message":"Verification failed"}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4pHttp.parseIntentAuthorizationResponse(response);
        });
      });

      it('parseResponse_fails_whenDataIsNotValidIntentL3', 0, () => {
        const body: string = '{"data":"{\\"version\\":\\"1.0\\"}"}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_INVALID_PARAMETER, () => {
          A4pHttp.parseIntentAuthorizationResponse(response);
        });
      });

      it('parseResponse_success_roundTrip', 0, () => {
        const l3 = createL3();
        const response = A4pHttp.buildIntentAuthorizationResponse(l3);

        const parsed = A4pHttp.parseIntentAuthorizationResponse(response);

        expect(parsed.intentL2.intentL1.intentId).assertEqual(l3.intentL2.intentL1.intentId);
      });
    });

    describe('IntentSubmission', () => {
      it('buildRequest_success', 0, () => {
        const l3 = createL3();

        const request = A4pHttp.buildIntentSubmissionRequest(l3);

        expect(request.method).assertEqual('POST');
        expect(request.path).assertEqual('/a4p/v1/intent/submission');
      });

      it('parseRequest_success', 0, () => {
        const l3 = createL3();
        const request = A4pHttp.buildIntentSubmissionRequest(l3);

        const result = A4pHttp.parseIntentSubmissionRequest(request);

        expect(result.intentL2.intentL1.intentId).assertEqual(l3.intentL2.intentL1.intentId);
      });

      it('buildResponse_success', 0, () => {
        const activationResponse: IntentActivationResponse = {
          status: 'activated',
          message: 'Intent activated'
        }

        const response = A4pHttp.buildIntentSubmissionResponse(activationResponse);

        expect(response.statusCode).assertEqual(200);
      });

      it('buildErrorResponse_failure', 0, () => {
        const response = buildErrorResponse(new A4pError(A4pErrorCode.A4P_VERIFY_FAIL));

        expect(response.statusCode).assertEqual(401);
      });

      it('parseResponse_success', 0, () => {
        const activationResponse: IntentActivationResponse = {
          status: 'activated',
          message: 'Intent activated'
        }
        const response = A4pHttp.buildIntentSubmissionResponse(activationResponse);

        const parseResult = A4pHttp.parseIntentSubmissionResponse(response);

        expect(parseResult.status).assertEqual('activated');
      });

      it('parseResponse_fails_whenHttpError', 0, () => {
        const body: string = '{"code":2002,"message":"Verification failed"}';
        const response = buildHttpResponse(400, body);

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4pHttp.parseIntentSubmissionResponse(response);
        });
      });

      it('parseResponse_fails_whenHttpErrorWithEmptyBody', 0, () => {
        const response = buildHttpResponse(400, '{}');

        // {} carries no known code, so it maps to A4P_INTERNAL_ERROR.
        expectThrows(A4pErrorCode.A4P_INTERNAL_ERROR, () => {
          A4pHttp.parseIntentSubmissionResponse(response);
        });
      });

      it('parseResponse_fails_whenErrorBodyWith200', 0, () => {
        const body: string = '{"code":2002,"message":"Verification failed"}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4pHttp.parseIntentSubmissionResponse(response);
        });
      });

      it('parseResponse_fails_whenActivationStatusMissing', 0, () => {
        // Activation responses must carry a status; a body that parses but
        // lacks it fails closed.
        const body: string = '{"data":{"message":"ok"}}';
        const response = buildHttpResponse(200, body);

        expectThrows(A4pErrorCode.A4P_DESERIALIZE_FAIL, () => {
          A4pHttp.parseIntentSubmissionResponse(response);
        });
      });
    });

    describe('fullFlow', () => {
      it('proposal_authorization_submission', 0, () => {
        const l1 = createL1();

        const proposalReq = A4pHttp.buildIntentProposalRequest(l1);
        const parsedL1 = A4pHttp.parseIntentProposalRequest(proposalReq);

        const l2 = A4p.issueIntent(parsedL1, new MockVerifier(),
          createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));

        const proposalResp = A4pHttp.buildIntentProposalResponse(l2);
        const parsedL2 = A4pHttp.parseIntentProposalResponse(proposalResp);

        const authReq = A4pHttp.buildIntentAuthorizationRequest(parsedL2);
        const parsedL2FromReq = A4pHttp.parseIntentAuthorizationRequest(authReq);

        const l3 = A4p.signIntent(parsedL2FromReq, new MockVerifier(),
          createTestSigningEnv(), new MockSigner(AlgorithmType.RSA3072, 'user-key-001'));

        const authResp = A4pHttp.buildIntentAuthorizationResponse(l3);
        const parsedL3 = A4pHttp.parseIntentAuthorizationResponse(authResp);

        const submissionReq = A4pHttp.buildIntentSubmissionRequest(parsedL3);
        const parsedL3FromReq = A4pHttp.parseIntentSubmissionRequest(submissionReq);
        expect(parsedL3FromReq.intentL2.intentL1.intentId).assertEqual('test-intent-001');

        const activationResp: IntentActivationResponse = { status: 'activated', message: 'Success' }
        const submissionResp = A4pHttp.buildIntentSubmissionResponse(activationResp);
        const parsedActivation = A4pHttp.parseIntentSubmissionResponse(submissionResp);
        expect(parsedActivation.status).assertEqual('activated');
      });

      it('errorResponse_roundTrip', 0, () => {
        // A server catching A4pError from an API call maps it to an HTTP
        // error response; the client side re-parses it into the same code.
        const caught = expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4p.issueIntent(createL1(), new MockVerifier(false),
            createTestIntentIssueContext(), new MockSigner(AlgorithmType.RSA3072, 'server-key-001'));
        });
        const response = buildErrorResponse(caught);
        expect(response.statusCode).assertEqual(401);

        // The wire body, sent back as an error response, parses to the
        // same A4P error code on the client side.
        const body: string = decodeUtf8ForTest(response.body);
        expectThrows(A4pErrorCode.A4P_VERIFY_FAIL, () => {
          A4pHttp.parseIntentProposalResponse(buildHttpResponse(401, body));
        });
      });
    });
  });
}
