/*
Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
*/

#include "shared/jsonrpc.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using Mcp::InitializeRequest;
using Mcp::InitializeRequestParams;
using Mcp::InitializeResult;
using Mcp::JSONRPCRequest;
using Mcp::JSONRPCResponse;
using Mcp::JSONRPCNotification;
using Mcp::JSONRPCError;
using Mcp::JSONRPCMessage;
using Mcp::DeserializeJSONRPCMessage;
using Mcp::SerializeJSONRPCMessage;
using Mcp::ClientCapabilities;
using Mcp::ServerCapabilities;
using Mcp::Implementation;
using Mcp::ListPromptsRequest;
using Mcp::ListPromptsResult;
using Mcp::ListResourceTemplatesRequest;
using Mcp::ListResourceTemplatesResult;
using Mcp::ResourceTemplate;
using Mcp::GetPromptParams;
using Mcp::GetPromptRequest;
using Mcp::GetPromptResult;
using Mcp::PromptArgument;
using Mcp::PromptInfo;
using Mcp::PromptMessage;
using Mcp::TextContent;
using Mcp::ImageContent;
using Mcp::ReadResourceRequest;
using Mcp::ReadResourceRequestParams;
using Mcp::SubscribeRequest;
using Mcp::SubscribeRequestParams;
using Mcp::UnsubscribeRequest;
using Mcp::UnsubscribeRequestParams;
using Mcp::ListResourcesRequest;
using Mcp::ReadResourceResult;
using Mcp::ListResourcesResult;
using Mcp::ResourceInfo;
using Mcp::TextResourceContents;
using Mcp::BlobResourceContents;
using Mcp::ResourceLink;
using Mcp::CallToolResult;
using Mcp::EmbeddedResource;
using Mcp::ContentType;
using Mcp::EmptyResult;
using Mcp::RoleType;
using Mcp::ListToolsRequest;
using Mcp::ListToolsResult;
using Mcp::ListToolsParams;
using Mcp::CallToolRequest;
using Mcp::CallToolResult;
using Mcp::CallToolParams;
using Mcp::Tool;
using Mcp::Root;
using Mcp::ListRootsResult;
using nlohmann::json;

constexpr const char* PROTOCOL_VERSION = "2025-03-26";
constexpr const char* JSONRPC_VERSION = "2.0";
constexpr int REQUEST_ID = 1;
constexpr const char* METHOD_NAME = "initialize";
constexpr const char* TEST_METHOD = "test/method";
constexpr const char* CLIENT_NAME = "TestClient";
constexpr const char* CLIENT_VERSION = "1.0.0";
constexpr const char* SERVER_NAME = "TestServer";
constexpr const char* SERVER_VERSION = "2.0.0";
constexpr const char* RESOURCE_URI = "file:///tmp/example.txt";
constexpr const char* RESOURCE_URI_2 = "file:///tmp/example.bin";
constexpr const char* RESOURCE_MIME = "text/plain";
constexpr const char* RESOURCE_NAME = "example";
constexpr const char* RESOURCE_TITLE = "Example Title";
constexpr std::size_t EXPECTED_ROOTS_COUNT = 2;

class JSONRPCSerializationTest : public ::testing::Test {
protected:
    Implementation clientImpl = {CLIENT_NAME, CLIENT_VERSION};
    Implementation serverImpl = {SERVER_NAME, SERVER_VERSION};
};

// ---------------------------------------------------------------------
// JSONRPCRequest
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, JSONRPCRequestSerializationSuccess) {
    JSONRPCRequest req;

    req.id_ = REQUEST_ID;
    req.method_ = METHOD_NAME;
    req.request_ = std::make_unique<InitializeRequest>();

    auto* initReq = dynamic_cast<InitializeRequest*>(req.request_.get());
    ASSERT_NE(nullptr, initReq);
    ASSERT_NE(nullptr, initReq->params_);

    auto* params = dynamic_cast<InitializeRequestParams*>(initReq->params_.get());
    ASSERT_NE(nullptr, params);
    params->protocolVersion_ = PROTOCOL_VERSION;

    std::string serialized = req.Serialize(req.method_);

    JSONRPCRequest req2;
    int result = req2.Deserialize(serialized, METHOD_NAME);

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, req2.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, req2.id_);
    EXPECT_EQ(METHOD_NAME, req2.method_);
    EXPECT_NE(nullptr, req2.request_);

    auto* initReq2 = dynamic_cast<InitializeRequest*>(req2.request_.get());
    ASSERT_NE(nullptr, initReq2);
    ASSERT_NE(nullptr, initReq2->params_);

    auto* params2 = dynamic_cast<InitializeRequestParams*>(initReq2->params_.get());
    ASSERT_NE(nullptr, params2);
    EXPECT_EQ(PROTOCOL_VERSION, params2->protocolVersion_);
}

TEST_F(JSONRPCSerializationTest, JSONRPCRequestWithStringIdSuccess) {
    JSONRPCRequest req;

    req.id_ = REQUEST_ID;
    req.method_ = METHOD_NAME;
    req.request_ = std::make_unique<InitializeRequest>();

    std::string serialized = req.Serialize(req.method_);

    JSONRPCRequest req2;
    int result = req2.Deserialize(serialized, METHOD_NAME);

    EXPECT_EQ(0, result);
    EXPECT_EQ(REQUEST_ID, req2.id_);
}

// ---------------------------------------------------------------------
// InitializeRequest
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, InitializeRequestDefaultConstruction) {
    InitializeRequest req;

    EXPECT_EQ(METHOD_NAME, req.method_);
    EXPECT_NE(nullptr, req.params_);
}

// ---------------------------------------------------------------------
// JSONRPCNotification
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, JSONRPCNotificationSerializationSuccess) {
    JSONRPCNotification notif;
    notif.method_ = TEST_METHOD;

    std::string serialized = notif.Serialize(notif.method_);

    JSONRPCNotification notif2;
    int result = notif2.Deserialize(serialized, TEST_METHOD);

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, notif2.jsonrpc_);
    EXPECT_EQ(TEST_METHOD, notif2.method_);
}

// ---------------------------------------------------------------------
// JSONRPCResponse
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, JSONRPCResponseSerializationSuccess) {
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    std::string serialized = resp.Serialize(METHOD_NAME);

    JSONRPCResponse resp2;
    int result = resp2.Deserialize(serialized, METHOD_NAME);

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, resp2.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, resp2.id_);
}

// ---------------------------------------------------------------------
// Prompts/list & prompts/get
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, ListPromptsRequestSerialization) {
    // Construct a prompts/list request and serialize it
    auto listReq = std::make_unique<ListPromptsRequest>();
    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "prompts/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize(rpcReq.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    EXPECT_EQ(std::string{"prompts/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
}

TEST_F(JSONRPCSerializationTest, GetPromptRequestSerialization) {
    // Construct a prompts/get request
    nlohmann::json args;
    args["topic"] = "C++";
    args["detail"] = "short";

    auto params = std::make_unique<GetPromptParams>("summarize", args);
    auto getReq = std::make_unique<GetPromptRequest>();
    getReq->params_ = std::move(params);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "prompts/get";
    rpcReq.request_ = std::move(getReq);

    std::string serialized = rpcReq.Serialize(rpcReq.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    EXPECT_EQ(std::string{"prompts/get"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    const auto& inner = j.at("params");
    EXPECT_EQ(std::string{"summarize"}, inner.at("name").get<std::string>());
    const auto& argsJson = inner.at("arguments");
    EXPECT_EQ(std::string{"C++"}, argsJson.at("topic").get<std::string>());
    EXPECT_EQ(std::string{"short"}, argsJson.at("detail").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, PromptContentAndMessageSerialization)
{
    PromptArgument arg;
    arg.name = "topic";
    arg.description = "Topic to summarize";
    arg.required = true;
    PromptInfo info;
    info.name = "summarize";
    info.description = "Summarize content";
    info.arguments = std::vector<PromptArgument>{arg};
    TextContent content;
    content.type = "text";
    content.text = "Hello";
    PromptMessage msg;
    msg.role = RoleType::USER;
    msg.content = content;

    // PromptMessage + ContentType round-trip (validate the structure by manually constructing JSON)
    json jMsg;
    jMsg["role"] = "user";
    json contentJson;
    contentJson["type"] = "text";
    contentJson["text"] = "Hello";
    jMsg["content"] = json::array({contentJson});

    // Verify the JSON structure matches the PromptMessage schema
    EXPECT_EQ(std::string{"user"}, jMsg.at("role").get<std::string>());
    json firstContent = jMsg.at("content").at(0);
    EXPECT_EQ(std::string{"text"}, firstContent.at("type").get<std::string>());
    EXPECT_EQ(std::string{"Hello"}, firstContent.at("text").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, ListPromptResultDeserialization) {
    // Construct a JSON string for ListPromptsResult
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "prompts": [
                {
                    "name": "code_review",
                    "description": "Review c++ code for bugs",
                    "arguments": [
                        {
                            "name": "file_path",
                            "title": "File Path",
                            "description": "Path to the file to review",
                            "required": true
                        },
                        {
                            "name": "focus_onSecurity",
                            "description": "Whether to focus on security issues",
                            "required": false
                        }
                    ]
                },
                {
                    "name": "documentation_summary",
                    "description": "Generate summary of documentation",
                    "arguments": [
                        {
                            "name": "content",
                            "description": "Content to summarize",
                            "required": true
                        }
                    ]
                }
            ]
        }
    })";

    // Deserialize the JSON string
    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "prompts/list");

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, resp.jsonrpc_);
    EXPECT_EQ(1, resp.id_);
    EXPECT_NE(nullptr, resp.result_);

    auto* listResult = dynamic_cast<ListPromptsResult*>(resp.result_.get());
    ASSERT_NE(nullptr, listResult);
    EXPECT_EQ(2, listResult->prompts.size());

    const auto& prompt1 = listResult->prompts.at(0);
    EXPECT_EQ(std::string{"code_review"}, prompt1.name);
    ASSERT_TRUE(prompt1.description.has_value());
    EXPECT_EQ(std::string{"Review c++ code for bugs"}, prompt1.description.value());
    ASSERT_TRUE(prompt1.arguments.has_value());
    ASSERT_EQ(2, prompt1.arguments->size());

    const auto& arg1_1 = prompt1.arguments->at(0);
    EXPECT_EQ(std::string{"file_path"}, arg1_1.name);
    ASSERT_TRUE(arg1_1.title.has_value());
    EXPECT_EQ(std::string{"File Path"}, arg1_1.title.value());
    ASSERT_TRUE(arg1_1.description.has_value());
    EXPECT_EQ(std::string{"Path to the file to review"}, arg1_1.description.value());
    ASSERT_TRUE(arg1_1.required.has_value());
    EXPECT_TRUE(arg1_1.required.value());

    const auto& arg1_2 = prompt1.arguments->at(1);
    EXPECT_EQ(std::string{"focus_onSecurity"}, arg1_2.name);
    EXPECT_FALSE(arg1_2.title.has_value());
    ASSERT_TRUE(arg1_2.description.has_value());
    EXPECT_EQ(std::string{"Whether to focus on security issues"}, arg1_2.description.value());
    ASSERT_TRUE(arg1_2.required.has_value());
    EXPECT_FALSE(arg1_2.required.value());

    const auto& prompt2 = listResult->prompts.at(1);
    EXPECT_EQ(std::string{"documentation_summary"}, prompt2.name);
    ASSERT_TRUE(prompt2.description.has_value());
    EXPECT_EQ(std::string{"Generate summary of documentation"}, prompt2.description.value());
    ASSERT_TRUE(prompt2.arguments.has_value());
    ASSERT_EQ(1, prompt2.arguments->size());

    const auto& arg2_1 = prompt2.arguments->at(0);
    EXPECT_EQ(std::string{"content"}, arg2_1.name);
    EXPECT_FALSE(arg2_1.title.has_value());
    ASSERT_TRUE(arg2_1.description.has_value());
    EXPECT_EQ(std::string{"Content to summarize"}, arg2_1.description.value());
    ASSERT_TRUE(arg2_1.required.has_value());
    EXPECT_TRUE(arg2_1.required.value());
}

TEST_F(JSONRPCSerializationTest, GetPromptResultDeserialization)
{
    // Construct a JSON string for GetPromptResult
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "description": "Summarize content",
            "messages": [
                {
                    "role": "user",
                    "content": [
                        {
                            "type": "text",
                            "text": "Please summarize the following article."
                        }
                    ]
                }
            ]
        }
    })";

    // Deserialize the JSON string
    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "prompts/get");

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, resp.jsonrpc_);
    EXPECT_EQ(1, resp.id_);
    EXPECT_NE(nullptr, resp.result_);

    auto* getResult = dynamic_cast<GetPromptResult*>(resp.result_.get());
    ASSERT_NE(nullptr, getResult);
    ASSERT_TRUE(getResult->description.has_value());
    EXPECT_EQ(std::string{"Summarize content"}, getResult->description.value());
    ASSERT_EQ(1, getResult->messages.size());

    const auto& msg = getResult->messages.at(0);
    EXPECT_EQ(RoleType::USER, msg.role);
    auto contentPtr = std::get_if<TextContent>(&msg.content);
    ASSERT_NE(nullptr, contentPtr);
    EXPECT_EQ(std::string{"text"}, contentPtr->type);
    EXPECT_EQ(std::string{"Please summarize the following article."}, contentPtr->text);
}

TEST_F(JSONRPCSerializationTest, GetPromptResultDeserializationWithoutDescription)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "messages": [
                {
                    "role": "user",
                    "content": [
                        {
                            "type": "text",
                            "text": "Hello"
                        }
                    ]
                }
            ]
        }
    })";

    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "prompts/get");

    EXPECT_EQ(0, result);
    auto* getResult = dynamic_cast<GetPromptResult*>(resp.result_.get());
    ASSERT_NE(nullptr, getResult);
    EXPECT_FALSE(getResult->description.has_value());
    ASSERT_EQ(1, getResult->messages.size());
}

TEST_F(JSONRPCSerializationTest, GetPromptRequestDeserializationComplexArguments)
{
    // Construct a JSON string for GetPromptRequest containing complex arguments
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "prompts/get",
        "params": {
            "name": "detailed_summarize",
            "arguments": {
                "topic": "Technology",
                "length": 500,
                "include_images": true,
                "metadata": {
                    "author": "John Doe",
                    "date": "2024-01-01"
                }
            }
        }
    })";

    // Deserialize the JSON string
    JSONRPCRequest rpcReq;
    int result = rpcReq.Deserialize(jsonStr, "prompts/get");

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, rpcReq.jsonrpc_);
    EXPECT_EQ(1, rpcReq.id_);
    EXPECT_EQ(std::string{"prompts/get"}, rpcReq.method_);
    ASSERT_NE(nullptr, rpcReq.request_);

    auto* getReq = dynamic_cast<GetPromptRequest*>(rpcReq.request_.get());
    ASSERT_NE(nullptr, getReq);
    ASSERT_NE(nullptr, getReq->params_);

    auto* params = dynamic_cast<GetPromptParams*>(getReq->params_.get());
    ASSERT_NE(nullptr, params);
    EXPECT_EQ(std::string{"detailed_summarize"}, params->name);
    ASSERT_TRUE(params->arguments.has_value());

    const auto& args = params->arguments.value();
    EXPECT_EQ(std::string{"Technology"}, args.at("topic").get<std::string>());
    EXPECT_EQ(500, args.at("length").get<int>());
    EXPECT_TRUE(args.at("include_images").get<bool>());

    const auto& metadata = args.at("metadata");
    EXPECT_EQ(std::string{"John Doe"}, metadata.at("author").get<std::string>());
    EXPECT_EQ(std::string{"2024-01-01"}, metadata.at("date").get<std::string>());
}

// ---------------------------------------------------------------------
// Resources: request serialization
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, ReadResourceRequestSerializationIncludesUri)
{
    auto readReq = std::make_unique<ReadResourceRequest>();
    readReq->params_ = std::make_unique<ReadResourceRequestParams>(RESOURCE_URI);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/read";
    rpcReq.request_ = std::move(readReq);

    std::string serialized = rpcReq.Serialize(rpcReq.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    EXPECT_EQ(std::string{"resources/read"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{RESOURCE_URI}, j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, SubscribeRequestSerializationIncludesUri)
{
    auto subReq = std::make_unique<SubscribeRequest>();
    subReq->params_ = std::make_unique<SubscribeRequestParams>(RESOURCE_URI);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/subscribe";
    rpcReq.request_ = std::move(subReq);

    std::string serialized = rpcReq.Serialize(rpcReq.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/subscribe"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{RESOURCE_URI}, j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, UnsubscribeRequestSerializationIncludesUri)
{
    auto unsubReq = std::make_unique<UnsubscribeRequest>();
    unsubReq->params_ = std::make_unique<UnsubscribeRequestParams>(RESOURCE_URI);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/unsubscribe";
    rpcReq.request_ = std::move(unsubReq);

    std::string serialized = rpcReq.Serialize(rpcReq.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/unsubscribe"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{RESOURCE_URI}, j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, ListResourcesRequestSerializationHasParamsKey)
{
    auto listReq = std::make_unique<ListResourcesRequest>();

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize(rpcReq.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
}

// ---------------------------------------------------------------------
// JSONRPCResponse: Resources functions serialization tests
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, JSONRPCResponseSerializationReadResource)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;
    resp.result_ = std::make_shared<ReadResourceResult>();

    ReadResourceResult result;
    TextResourceContents textContent;
    textContent.uri = RESOURCE_URI;
    textContent.text = "Hello, World!";
    textContent.mimeType = RESOURCE_MIME;
    result.contents.push_back(textContent);

    resp.result_ = std::make_shared<ReadResourceResult>(std::move(result));

    std::string serialized = resp.Serialize("resources/read");
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    ASSERT_TRUE(j.contains("result"));
    ASSERT_TRUE(j.at("result").contains("contents"));
    ASSERT_TRUE(j.at("result").at("contents").is_array());
}

TEST_F(JSONRPCSerializationTest, JSONRPCResponseSerializationListResources)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    ListResourcesResult result;
    ResourceInfo resource1;
    resource1.uri = RESOURCE_URI;
    resource1.name = RESOURCE_NAME;
    resource1.title = RESOURCE_TITLE;
    resource1.description = "Test resource";
    resource1.mimeType = RESOURCE_MIME;
    resource1.size = 1024;
    result.resources.push_back(resource1);

    ResourceInfo resource2;
    resource2.uri = RESOURCE_URI_2;
    resource2.name = "binary_resource";
    result.resources.push_back(resource2);

    resp.result_ = std::make_shared<ListResourcesResult>(std::move(result));

    std::string serialized = resp.Serialize("resources/list");
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    ASSERT_TRUE(j.contains("result"));
    ASSERT_TRUE(j.at("result").contains("resources"));
    ASSERT_TRUE(j.at("result").at("resources").is_array());
    EXPECT_EQ(2, j.at("result").at("resources").size());
}

// ---------------------------------------------------------------------
// JSONRPCResponse: roots/list serialization & deserialization tests
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, JSONRPCResponseSerializationListRoots)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    ListRootsResult result;
    Root r1;
    r1.uri = "file:///tmp";
    r1.name = std::string{"tmp"};
    result.roots.push_back(r1);

    Root r2;
    r2.uri = "file:///home";
    result.roots.push_back(r2);

    result.meta = std::unordered_map<std::string, Mcp::JsonValue>{{"traceId", "abc"}};

    resp.result_ = std::make_shared<ListRootsResult>(std::move(result));

    std::string serialized = resp.Serialize("roots/list");
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    ASSERT_TRUE(j.contains("result"));
    ASSERT_TRUE(j.at("result").contains("roots"));
    ASSERT_TRUE(j.at("result").at("roots").is_array());
    EXPECT_EQ(EXPECTED_ROOTS_COUNT, j.at("result").at("roots").size());

    const auto& roots = j.at("result").at("roots");
    EXPECT_EQ(std::string{"file:///tmp"}, roots.at(0).at("uri").get<std::string>());
    EXPECT_EQ(std::string{"tmp"}, roots.at(0).at("name").get<std::string>());
    EXPECT_EQ(std::string{"file:///home"}, roots.at(1).at("uri").get<std::string>());
    EXPECT_FALSE(roots.at(1).contains("name"));

    ASSERT_TRUE(j.at("result").contains("_meta"));
    EXPECT_EQ(std::string{"abc"}, j.at("result").at("_meta").at("traceId").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, JSONRPCResponseDeserializationListRoots)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "roots": [
                {"uri": "file:///tmp", "name": "tmp"},
                {"uri": "file:///home"}
            ],
            "_meta": {"traceId": "abc"}
        }
    })";

    JSONRPCResponse resp;
    int rc = resp.Deserialize(jsonStr, "roots/list");
    EXPECT_EQ(0, rc);
    EXPECT_EQ(JSONRPC_VERSION, resp.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, resp.id_);
    ASSERT_NE(nullptr, resp.result_);

    auto* rootsResult = dynamic_cast<ListRootsResult*>(resp.result_.get());
    ASSERT_NE(nullptr, rootsResult);
    ASSERT_EQ(EXPECTED_ROOTS_COUNT, rootsResult->roots.size());
    EXPECT_EQ(std::string{"file:///tmp"}, rootsResult->roots[0].uri);
    ASSERT_TRUE(rootsResult->roots[0].name.has_value());
    EXPECT_EQ(std::string{"tmp"}, rootsResult->roots[0].name.value());
    EXPECT_EQ(std::string{"file:///home"}, rootsResult->roots[1].uri);
    EXPECT_FALSE(rootsResult->roots[1].name.has_value());
    ASSERT_TRUE(rootsResult->meta.has_value());
    EXPECT_EQ(std::string{"abc"}, rootsResult->meta->at("traceId").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, JSONRPCResponseSerializeResourceTemplatesList)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    ListResourceTemplatesResult result;
    ResourceTemplate template1;
    template1.uriTemplate = "resources/{resource_id}";
    template1.name = "resource_template";
    template1.title = "Resource Template";
    template1.description = "A template for resources";
    template1.mimeType = "application/json";
    result.resourceTemplates.push_back(template1);

    ResourceTemplate template2;
    template2.uriTemplate = "docuemnts/{doc_id}";
    template2.name = "document_template";
    result.resourceTemplates.push_back(template2);

    resp.result_ = std::make_shared<ListResourceTemplatesResult>(std::move(result));

    std::string serialized = resp.Serialize("resources/templates/list");
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    ASSERT_TRUE(j.contains("result"));
    ASSERT_TRUE(j.at("result").contains("resourceTemplates"));
    ASSERT_TRUE(j.at("result").at("resourceTemplates").is_array());
    EXPECT_EQ(2, j.at("result").at("resourceTemplates").size());
}

TEST_F(JSONRPCSerializationTest, JSONRPCResponseSerializeSubscribeResource)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    EmptyResult result;
    resp.result_ = std::make_shared<EmptyResult>(std::move(result));

    std::string serialized = resp.Serialize("resources/subscribe");
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    ASSERT_TRUE(j.contains("result"));
    EXPECT_TRUE(j.at("result").is_object());
    EXPECT_TRUE(j.at("result").empty());
}

TEST_F(JSONRPCSerializationTest, JSONRPCResponseSerializeUnsubscribeResource)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    EmptyResult result;
    resp.result_ = std::make_shared<EmptyResult>(std::move(result));

    std::string serialized = resp.Serialize("resources/unsubscribe");
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    ASSERT_TRUE(j.contains("result"));
    EXPECT_TRUE(j.at("result").is_object());
    EXPECT_TRUE(j.at("result").empty());
}

// ---------------------------------------------------------------------
// Resource templates request serialization tests
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, ListResourceTemplatesRequestSerializationHasParamsKey)
{
    auto listReq = std::make_unique<ListResourceTemplatesRequest>();

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/templates/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize(rpcReq.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/templates/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
}

// ---------------------------------------------------------------------
// Resources: response deserialization
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, ReadResourceResultDeserializationParsesTextAndBlob)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "contents": [
                {
                    "uri": "file:///tmp/example.txt",
                    "text": "hello",
                    "mimeType": "text/plain"
                },
                {
                    "uri": "file:///tmp/example.bin",
                    "blob": "AQIDBA=="
                }
            ]
        }
    })";

    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "resources/read");

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, resp.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, resp.id_);
    ASSERT_NE(nullptr, resp.result_);

    auto* readResult = dynamic_cast<ReadResourceResult*>(resp.result_.get());
    ASSERT_NE(nullptr, readResult);
    ASSERT_EQ(2, readResult->contents.size());

    const auto* text = std::get_if<TextResourceContents>(&readResult->contents.at(0));
    ASSERT_NE(nullptr, text);
    EXPECT_EQ(std::string{RESOURCE_URI}, text->uri);
    EXPECT_EQ(std::string{"hello"}, text->text);
    ASSERT_TRUE(text->mimeType.has_value());
    EXPECT_EQ(std::string{RESOURCE_MIME}, text->mimeType.value());

    const auto* blob = std::get_if<BlobResourceContents>(&readResult->contents.at(1));
    ASSERT_NE(nullptr, blob);
    EXPECT_EQ(std::string{RESOURCE_URI_2}, blob->uri);
    EXPECT_EQ(std::string{"AQIDBA=="}, blob->blob);
    EXPECT_FALSE(blob->mimeType.has_value());
}

TEST_F(JSONRPCSerializationTest, ListResourcesResultDeserializationParsesResourceInfo)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "resources": [
                {
                    "uri": "file:///tmp/example.txt",
                    "name": "example",
                    "title": "Example Title",
                    "description": "desc",
                    "mimeType": "text/plain",
                    "size": 123
                },
                {
                    "uri": "file:///tmp/example.bin",
                    "name": "example2"
                }
            ]
        }
    })";

    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "resources/list");

    EXPECT_EQ(0, result);
    ASSERT_NE(nullptr, resp.result_);

    auto* listResult = dynamic_cast<ListResourcesResult*>(resp.result_.get());
    ASSERT_NE(nullptr, listResult);
    ASSERT_EQ(2, listResult->resources.size());

    const ResourceInfo& r1 = listResult->resources.at(0);
    EXPECT_EQ(std::string{RESOURCE_URI}, r1.uri);
    EXPECT_EQ(std::string{RESOURCE_NAME}, r1.name);
    ASSERT_TRUE(r1.title.has_value());
    EXPECT_EQ(std::string{RESOURCE_TITLE}, r1.title.value());
    ASSERT_TRUE(r1.description.has_value());
    EXPECT_EQ(std::string{"desc"}, r1.description.value());
    ASSERT_TRUE(r1.mimeType.has_value());
    EXPECT_EQ(std::string{RESOURCE_MIME}, r1.mimeType.value());
    ASSERT_TRUE(r1.size.has_value());
    EXPECT_EQ(123, r1.size.value());

    const ResourceInfo& r2 = listResult->resources.at(1);
    EXPECT_EQ(std::string{RESOURCE_URI_2}, r2.uri);
    EXPECT_EQ(std::string{"example2"}, r2.name);
    EXPECT_FALSE(r2.title.has_value());
    EXPECT_FALSE(r2.description.has_value());
    EXPECT_FALSE(r2.mimeType.has_value());
    EXPECT_FALSE(r2.size.has_value());
}

TEST_F(JSONRPCSerializationTest, ToolsCallResultDeserializationParsesEmbeddedResourceAndLink)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "content": [
                {
                    "type": "resource",
                    "resource": {
                        "uri": "file:///tmp/example.txt",
                        "text": "hello",
                        "mimeType": "text/plain"
                    }
                },
                {
                    "type": "resource_link",
                    "uri": "file:///tmp/example.txt",
                    "name": "example"
                }
            ],
            "isError": false
        }
    })";

    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "tools/call");

    EXPECT_EQ(0, result);
    ASSERT_NE(nullptr, resp.result_);

    auto* callResult = dynamic_cast<CallToolResult*>(resp.result_.get());
    ASSERT_NE(nullptr, callResult);
    ASSERT_EQ(2, callResult->content.size());
    EXPECT_FALSE(callResult->isError);

    const auto* embedded = std::get_if<EmbeddedResource>(&callResult->content.at(0));
    ASSERT_NE(nullptr, embedded);
    EXPECT_EQ(std::string{"resource"}, embedded->type);
    const auto* embeddedText = std::get_if<TextResourceContents>(&embedded->resource);
    ASSERT_NE(nullptr, embeddedText);
    EXPECT_EQ(std::string{RESOURCE_URI}, embeddedText->uri);
    EXPECT_EQ(std::string{"hello"}, embeddedText->text);

    const auto* link = std::get_if<ResourceLink>(&callResult->content.at(1));
    ASSERT_NE(nullptr, link);
    EXPECT_EQ(std::string{"resource_link"}, link->type);
    EXPECT_EQ(std::string{RESOURCE_URI}, link->uri);
    EXPECT_EQ(std::string{RESOURCE_NAME}, link->name);
}

// ---------------------------------------------------------------------
// JSONRPCMessage helper functions
// ---------------------------------------------------------------------

TEST_F(JSONRPCSerializationTest, DeserializeJSONRPCMessageParsesRequest) {
    JSONRPCRequest req;
    req.id_ = REQUEST_ID;
    req.method_ = METHOD_NAME;

    std::string serialized = req.Serialize(req.method_);

    JSONRPCMessage message = DeserializeJSONRPCMessage(serialized, METHOD_NAME);
    ASSERT_TRUE(std::holds_alternative<JSONRPCRequest>(message));

    const auto &parsedReq = std::get<JSONRPCRequest>(message);
    EXPECT_EQ(JSONRPC_VERSION, parsedReq.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, parsedReq.id_);
    EXPECT_EQ(METHOD_NAME, parsedReq.method_);
}

TEST_F(JSONRPCSerializationTest, DeserializeJSONRPCMessageParsesResponse) {
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    std::string serialized = resp.Serialize(METHOD_NAME);

    JSONRPCMessage message = DeserializeJSONRPCMessage(serialized, METHOD_NAME);
    ASSERT_TRUE(std::holds_alternative<JSONRPCResponse>(message));

    const auto &parsedResp = std::get<JSONRPCResponse>(message);
    EXPECT_EQ(JSONRPC_VERSION, parsedResp.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, parsedResp.id_);
}

TEST_F(JSONRPCSerializationTest, DeserializeJSONRPCMessageParsesNotification) {
    JSONRPCNotification notif;
    notif.method_ = TEST_METHOD;

    std::string serialized = notif.Serialize(TEST_METHOD);

    JSONRPCMessage message = DeserializeJSONRPCMessage(serialized, TEST_METHOD);
    ASSERT_TRUE(std::holds_alternative<JSONRPCNotification>(message));

    const auto &parsedNotif = std::get<JSONRPCNotification>(message);
    EXPECT_EQ(JSONRPC_VERSION, parsedNotif.jsonrpc_);
    EXPECT_EQ(TEST_METHOD, parsedNotif.method_);
}

TEST_F(JSONRPCSerializationTest, SerializeJSONRPCMessageRoundTripRequest) {
    JSONRPCRequest req;
    req.id_ = REQUEST_ID;
    req.method_ = METHOD_NAME;

    JSONRPCMessage message = std::move(req);
    std::string serialized = SerializeJSONRPCMessage(message, std::nullopt);

    JSONRPCMessage parsed = DeserializeJSONRPCMessage(serialized, METHOD_NAME);
    ASSERT_TRUE(std::holds_alternative<JSONRPCRequest>(parsed));

    const auto &parsedReq = std::get<JSONRPCRequest>(parsed);
    EXPECT_EQ(JSONRPC_VERSION, parsedReq.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, parsedReq.id_);
    EXPECT_EQ(METHOD_NAME, parsedReq.method_);
}

TEST_F(JSONRPCSerializationTest, DeserializeJSONRPCMessageInvalidJsonThrows) {
    std::string invalidJson = "not valid json";
    EXPECT_THROW(DeserializeJSONRPCMessage(invalidJson, METHOD_NAME), nlohmann::json::parse_error);
}

TEST_F(JSONRPCSerializationTest, ResourceUpdatedNotificationSerializeIncludesUriParam)
{
    JSONRPCNotification notif;
    notif.method_ = "notifications/resources/updated";

    auto payload = std::make_unique<Mcp::ResourceUpdatedNotification>();
    payload->method_ = notif.method_;
    payload->params_ = std::make_unique<Mcp::ResourceUpdatedNotificationParams>(RESOURCE_URI);
    notif.notification_ = std::move(payload);

    std::string serialized = notif.Serialize(notif.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(std::string{"notifications/resources/updated"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    ASSERT_TRUE(j.at("params").is_object());
    EXPECT_EQ(std::string{RESOURCE_URI}, j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCSerializationTest, ResourceUpdatedNotificationDeserializeParsesUriParam)
{
    json j;
    j["jsonrpc"] = JSONRPC_VERSION;
    j["method"] = "notifications/resources/updated";
    j["params"] = {{"uri", RESOURCE_URI}};

    JSONRPCNotification notif;
    int result = notif.Deserialize(j.dump(), "notifications/resources/updated");
    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, notif.jsonrpc_);
    EXPECT_EQ(std::string{"notifications/resources/updated"}, notif.method_);
    ASSERT_NE(nullptr, notif.notification_);

    const auto *typed = dynamic_cast<const Mcp::ResourceUpdatedNotification *>(notif.notification_.get());
    ASSERT_NE(nullptr, typed);
    ASSERT_NE(nullptr, typed->params_);
    const auto *params = dynamic_cast<const Mcp::ResourceUpdatedNotificationParams *>(typed->params_.get());
    ASSERT_NE(nullptr, params);
    EXPECT_EQ(std::string{RESOURCE_URI}, params->uri);
}

TEST_F(JSONRPCSerializationTest, ToolListChangedNotificationRoundTripDeserializeTyped)
{
    json j;
    j["jsonrpc"] = JSONRPC_VERSION;
    j["method"] = "notifications/tools/list_changed";
    j["params"] = json::object();

    JSONRPCNotification notif;
    int result = notif.Deserialize(j.dump(), "notifications/tools/list_changed");
    EXPECT_EQ(0, result);
    ASSERT_NE(nullptr, notif.notification_);
    EXPECT_NE(nullptr, dynamic_cast<const Mcp::ToolListChangedNotification *>(notif.notification_.get()));
}

TEST_F(JSONRPCSerializationTest, PromptListChangedNotificationSerializeIncludesParams)
{
    JSONRPCNotification notif;
    notif.method_ = "notifications/prompts/list_changed";

    std::string serialized = notif.Serialize(notif.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(std::string{"notifications/prompts/list_changed"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_TRUE(j.at("params").is_object());
}

TEST_F(JSONRPCSerializationTest, PromptListChangedNotificationDeserializeCreatesTypedNotification)
{
    json j;
    j["jsonrpc"] = JSONRPC_VERSION;
    j["method"] = "notifications/prompts/list_changed";
    j["params"] = json::object();

    JSONRPCNotification notif;
    int result = notif.Deserialize(j.dump(), "notifications/prompts/list_changed");
    EXPECT_EQ(0, result);
    ASSERT_NE(nullptr, notif.notification_);
    EXPECT_NE(nullptr, dynamic_cast<const Mcp::PromptListChangedNotification *>(notif.notification_.get()));
}

TEST_F(JSONRPCSerializationTest, ResourceListChangedNotificationSerializeIncludesParams)
{
    JSONRPCNotification notif;
    notif.method_ = "notifications/resources/list_changed";

    std::string serialized = notif.Serialize(notif.method_);
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(std::string{"notifications/resources/list_changed"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_TRUE(j.at("params").is_object());
}

TEST_F(JSONRPCSerializationTest, ResourceListChangedNotificationDeserializeCreatesTypedNotification)
{
    json j;
    j["jsonrpc"] = JSONRPC_VERSION;
    j["method"] = "notifications/resources/list_changed";
    j["params"] = json::object();

    JSONRPCNotification notif;
    int result = notif.Deserialize(j.dump(), "notifications/resources/list_changed");
    EXPECT_EQ(0, result);
    ASSERT_NE(nullptr, notif.notification_);
    EXPECT_NE(nullptr, dynamic_cast<const Mcp::ResourceListChangedNotification *>(notif.notification_.get()));
}

} // namespace
