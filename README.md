# Agent Protocol

English | [简体中文](./README_zh.md)

## Introduction

**Agent Protocol** provides protocol SDKs and a service registry for agent interoperability in the [openJiuwen](https://github.com/openJiuwen-ai) ecosystem. It helps developers connect agents to tools, communicate with remote agents, discover available services, route requests across LLM deployments, and enforce verifiable user authorization before sensitive operations.

An agent application needs more than a model and task logic: it needs a way to access external capabilities, locate suitable collaborators, exchange messages and task results, and constrain what it can execute on a user's behalf. This repository addresses these needs through seven complementary components: **MCP C++ SDK**, **A2A C++ SDK**, **A2X Registry**, **A4P Python SDK**, **A4P Java SDK**, **A4P ArkTS SDK**, and **IntelliRouter**. Developers can choose individual components or combine them according to their integration needs.

## Core Capabilities

### Tool and resource integration with MCP

The Model Context Protocol (MCP) C++ SDK provides both client and server capabilities, supporting **Streamable HTTP** and **stdio** transports. Developers can expose tools, resources, and prompts through an MCP server, or build a client that connects to existing MCP services. The SDK also includes support for authentication, sampling, progress notifications, and roots.

### Agent communication with A2A

The Agent2Agent (A2A) C++ SDK implements agent communication over **HTTP and JSON-RPC**. Clients can discover Agent Cards, send messages, query or cancel tasks, and receive streaming events. Servers can register an `AgentExecutor`, manage task lifecycles, and expose JSON-RPC endpoints, with extension points such as request interceptors and custom task stores.

### Service registration and discovery with A2X

A2X Registry manages agents and services that agents can invoke. It supports registration and deregistration, field-based queries, service-card updates, online/busy/offline states, ownership-based access control, and short-lived reservation leases for Agent Team coordination.

For capability discovery, A2X search combines an automatically constructed **hierarchical capability catalog** with **LLM-guided semantic navigation**, allowing agents to locate services from natural-language requests. The registry also provides optional vector search, dataset isolation, HTTP APIs, a Python client, and a web UI for service management and search exploration. A2X search and catalog construction require an LLM configuration; basic registry operations do not.

### Verifiable authorization with A4P

The Agentic Authentication, Authorization, and Audit Protocol (A4P) Python SDK supports **Operation Mandates** for specific operations and **Intent Mandates/Tokens** that allow repeated execution within specified operation types and parameter constraints. It separates the Agent, A4P Server, local User Authorizer, and protected Tool Server so that authorization checks are enforced outside the agent's decision-making logic.

The SDK provides Ed25519-signed authorization objects, Ed25519 and WebAuthn/passkey user-signature methods, parameter-scope checks, and execution limits with SQLite-backed atomic consumption. Its two-stage prepare/complete interfaces support asynchronous user confirmation and integration with existing tool services.

### Payment-intent authorization with A4P Java

The A4P Java SDK is currently released as SDK version `0.1.0` and implements the payment-intent protocol with wire-format version `1.0` with an L1/L2/L3 signature chain for agent creation, intent-center issuance, and user authorization. It supports intent activation, authenticity verification and revocation, plus payment reservation, completion and cancellation with amount, execution-count, product-whitelist and periodic quota checks.

The SDK provides immutable models, canonical JSON serialization, execution-context serialization and restoration, pluggable signing and verification interfaces, and an optional RSA-3072 adapter. Its execution ledger is in memory; applications must provide durable storage, database locking, replay prevention and key-to-identity binding. See the [Java SDK guide](./A4P-java-sdk/README.md) for integration details.

### Payment-intent authorization with A4P ArkTS

The A4P ArkTS SDK brings the payment-intent protocol to **HarmonyOS**, delivered as a HAR library (module name `a4p`) implementing wire-format version `1.0`. It covers the signed intent lifecycle (create → issue → authorize → activate), payment execution accounting against the frozen authorization snapshot, and the canonical JSON codec used for signing and persistence, with the same L1/L2/L3 signature chain for agent creation, intent-center issuance, and user authorization as the Java SDK.

Cryptography is pluggable through `Signer` / `SignatureVerifier` interfaces — nothing is bundled, and adapters built on `@kit.CryptoArchitectureKit` interoperate with the Java SDK's `RSA3072` suite. The execution ledger is in memory; applications must provide durable storage, database locking, replay prevention, and key-to-identity binding. The Java SDK was ported from this SDK and shares its canonical JSON rules, error-code table, and wire field names. See the [ArkTS SDK guide](./A4P-arkts-sdk/README.md) for integration details.

### Intelligent LLM routing with IntelliRouter

IntelliRouter is a lightweight **LLM proxy SDK** that maps a single model name to multiple deployment endpoints and routes each request intelligently. It pools deployments across providers — OpenAI, Anthropic, Bedrock, DeepSeek, Gemini, and SiliconFlow, plus any OpenAI-compatible endpoint — and selects among them with pluggable strategies: SimpleShuffle, LowestLatency, TagBased, TokenAware, RateLimitAware, Adaptive, OrderedFailover, and TagFiltered.

The router tracks per-deployment health and quota state (latency, failure counts, TPM/RPM usage, cooling windows), runs background health checks, retries failed requests with automatic failover, and exposes observability through an event bus, Prometheus metrics, a web dashboard, and logging hooks. Streaming responses and JSON/Markdown output parsers are also included.

## Choosing a Component

Each component addresses a different integration need and can be used independently, without adopting the others.

| Integration need | Component | Role in an application |
| --- | --- | --- |
| Access tools, resources, and prompts | MCP C++ SDK | Connect an agent to external capabilities through MCP clients and servers. |
| Delegate work to another agent | A2A C++ SDK | Exchange messages, track tasks, and receive streaming results. |
| Find an appropriate service or collaborator | A2X Registry | Register service metadata, discover capabilities, and coordinate availability. |
| Execute within user-approved boundaries | A4P Python SDK | Obtain verifiable authorization and validate its scope before a protected operation. |
| Authorize and execute payments within an approved intent | A4P Java SDK | Verify the signature chain and enforce payment constraints throughout the execution lifecycle. |
| Run the payment-intent lifecycle on HarmonyOS | A4P ArkTS SDK | Build the signed intent chain and enforce payment constraints inside a HarmonyOS application. |
| Route requests across multiple LLM deployments | IntelliRouter | Pool model endpoints, pick the best deployment per request, and fail over automatically. |

## Repository Layout

```text
agent-protocol/
├── MCP/cpp-sdk/          # MCP C++ client/server SDK and examples
├── A2A/cpp-sdk/          # A2A C++ client/server SDK and examples
├── AgentRegistry/        # A2X registry, Python client, web UI, and documentation
├── A4P-python-sdk/       # A4P Python SDK, protocol documentation, and examples
├── A4P-java-sdk/         # A4P Java payment-intent SDK, canonical JSON rules, and examples
├── A4P-arkts-sdk/        # A4P ArkTS (HarmonyOS) payment-intent HAR library and on-device tests
└── intelli_router/       # IntelliRouter LLM proxy SDK, tests, and examples
```

## Getting Started

Clone the repository, then follow the guide for the component you need:

```bash
git clone https://github.com/openJiuwen-ai/agent-protocol.git
cd agent-protocol
```

| Component | Prerequisites | First steps |
| --- | --- | --- |
| [MCP C++ SDK](./MCP/cpp-sdk/README.md) | C++17, CMake ≥ 3.15, and system dependencies; Linux/macOS | Install dependencies, build the SDK, and run the tool client/server examples. |
| [A2A C++ SDK](./A2A/cpp-sdk/README.md) | C++17, CMake ≥ 3.15, and system dependencies; Linux is the primary validation platform | Build with examples enabled, then run the Hello World and streaming examples. |
| [A2X Registry](./AgentRegistry/README.md) | Python ≥ 3.10; LLM configuration for A2X search | Install the registry, start the service, and register/query services through HTTP APIs or the UI. |
| [A4P Python SDK](./A4P-python-sdk/README.md) | Python ≥ 3.10 and uv | Install dependencies and run the non-interactive Ed25519 authorization example. |
| [A4P Java SDK](./A4P-java-sdk/README.md) | JDK ≥ 17 and Maven ≥ 3.9 | Run `mvn test` and `mvn package` in `A4P-java-sdk`, then run `PaymentExample` following the SDK guide. |
| [A4P ArkTS SDK](./A4P-arkts-sdk/README.md) | DevEco Studio 5.0+ with HarmonyOS SDK API 12; `ohpm`, `hvigorw`, and Bash for command-line builds | Run `scripts/build.sh` to build the HAR and the on-device test HAP, then run the hypium suites in DevEco Studio or on a device. |
| [IntelliRouter](./intelli_router/README.md) | Python ≥ 3.9 | Install the SDK, run the unit tests, then try the end-to-end and observability examples. |

The components are built and installed separately. See each guide for complete dependency requirements and platform-specific instructions.

## Documentation

- **MCP:** [API reference](./MCP/cpp-sdk/docs/api/README.md) · [Dependencies](./MCP/cpp-sdk/docs/dependencies.md) · [Testing](./MCP/cpp-sdk/docs/testing.md)
- **A2A:** [API reference](./A2A/cpp-sdk/docs/api/README.md) · [Dependencies](./A2A/cpp-sdk/docs/dependencies.md) · [Error handling](./A2A/cpp-sdk/docs/errors.md)
- **A2X:** [Registry guide](./AgentRegistry/README.md) · [Python client](./AgentRegistry/client/README.md) · [Agent Team deployment](./AgentRegistry/README_forAgentTeam.md)
- **A4P:** [Protocol](./A4P-python-sdk/A4P_PROTOCOL.md) · [SDK design](./A4P-python-sdk/A4P_SDK_DESIGN.md) · [MCP integration example](./A4P-python-sdk/examples/note_mcp_a4p/README.md)
- **A4P Java:** [SDK guide](./A4P-java-sdk/README.md) · [Canonical JSON and signed bytes](./A4P-java-sdk/docs/canonical-json-spec.md) · [Payment example](./A4P-java-sdk/src/main/java/io/openjiuwen/a4p/examples/PaymentExample.java)
- **A4P ArkTS:** [SDK guide](./A4P-arkts-sdk/README.md) · [Canonical JSON and signed bytes](./A4P-java-sdk/docs/canonical-json-spec.md)
- **IntelliRouter:** [SDK guide](./intelli_router/README.md) · [API reference](./intelli_router/API.md) · [Examples](./intelli_router/examples/)

## Contributing

We welcome all forms of contribution, including but not limited to:

- Reporting issues and suggesting features
- Improving documentation
- Submitting code
- Sharing usage experience

## License

This project is licensed under the Apache-2.0 License.

This product serves solely as a workflow orchestration tool and does not embed any AI model capabilities. When users integrate AI models for specific business scenarios, they shall bear full responsibility for compliance obligations under the EU AI Act and other relevant regulatory frameworks.
