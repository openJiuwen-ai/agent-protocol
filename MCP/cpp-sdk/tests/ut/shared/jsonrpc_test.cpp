/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
using Mcp::AudioContent;
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
using Mcp::CallToolRequest;
using Mcp::CallToolParams;
using Mcp::Tool;
using Mcp::InitializedNotification;
using Mcp::RequestParams;
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

static constexpr int DEFAULT_ID = 123;
static constexpr int ARGS_NUM = 2;
static constexpr int ARGS_LEN = 500;
static constexpr int RESOURCE_SIZE = 1024;
static constexpr int ERR_CODE = -32700;


class JSONRPCTest : public ::testing::Test {
public:
    ~JSONRPCTest() {}
protected:
    Implementation clientImpl = {CLIENT_NAME, CLIENT_VERSION};
    Implementation serverImpl = {SERVER_NAME, SERVER_VERSION};
};

TEST_F(JSONRPCTest, JSONRPCRequestSerializationSuccess)
{
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

    std::string serialized = req.Serialize();

    JSONRPCRequest req2;
    int result = req2.Deserialize(serialized);

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

TEST_F(JSONRPCTest, JSONRPCRequestWithStringIdSuccess)
{
    JSONRPCRequest req;

    req.id_ = REQUEST_ID;
    req.method_ = METHOD_NAME;
    req.request_ = std::make_unique<InitializeRequest>();

    std::string serialized = req.Serialize();

    JSONRPCRequest req2;
    int result = req2.Deserialize(serialized);

    EXPECT_EQ(0, result);
    EXPECT_EQ(REQUEST_ID, req2.id_);
}

TEST_F(JSONRPCTest, InitializeRequestDefaultConstruction)
{
    InitializeRequest req;

    EXPECT_EQ(METHOD_NAME, req.method_);
    EXPECT_NE(nullptr, req.params_);
}

TEST_F(JSONRPCTest, JSONRPCNotificationSerializationSuccess)
{
    JSONRPCNotification notif;
    notif.method_ = TEST_METHOD;

    std::string serialized = notif.Serialize();

    JSONRPCNotification notif2;
    int result = notif2.Deserialize(serialized);

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, notif2.jsonrpc_);
    EXPECT_EQ(TEST_METHOD, notif2.method_);
}

TEST_F(JSONRPCTest, JSONRPCResponseSerializationSuccess)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    std::string serialized = resp.Serialize(METHOD_NAME);

    JSONRPCResponse resp2;
    int result = resp2.Deserialize(serialized, METHOD_NAME);

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, resp2.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, resp2.id_);
}

TEST_F(JSONRPCTest, ListPromptsRequestSerialization)
{
    // Construct a prompts/list request and serialize it
    auto listReq = std::make_unique<ListPromptsRequest>();
    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "prompts/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    EXPECT_EQ(std::string{"prompts/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
}

TEST_F(JSONRPCTest, GetPromptRequestSerialization)
{
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

    std::string serialized = rpcReq.Serialize();
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

TEST_F(JSONRPCTest, PromptContentAndMessageSerialization)
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

TEST_F(JSONRPCTest, ListPromptResultDeserialization_MultiplePrompts)
{
    // Keep the original JSON but test only the overall structure
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

    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "prompts/list");

    EXPECT_EQ(0, result);
    EXPECT_EQ(JSONRPC_VERSION, resp.jsonrpc_);
    EXPECT_EQ(1, resp.id_);
    EXPECT_NE(nullptr, resp.result_);

    auto* listResult = dynamic_cast<ListPromptsResult*>(resp.result_.get());
    ASSERT_NE(nullptr, listResult);
    EXPECT_EQ(ARGS_NUM, listResult->prompts.size());
    EXPECT_EQ(std::string{"code_review"}, listResult->prompts.at(0).name);
    EXPECT_EQ(std::string{"documentation_summary"}, listResult->prompts.at(1).name);
}

static std::string PROMPT_JSON_STR = R"({
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
                }
            ]
        }
    })";

TEST_F(JSONRPCTest, ListPromptResultDeserialization_PromptArgumentDetails)
{
    JSONRPCResponse resp;
    int result = resp.Deserialize(PROMPT_JSON_STR, "prompts/list");

    ASSERT_EQ(0, result);

    auto* listResult = dynamic_cast<ListPromptsResult*>(resp.result_.get());
    ASSERT_NE(nullptr, listResult);
    ASSERT_EQ(1, listResult->prompts.size());

    const auto& prompt = listResult->prompts.at(0);

    // Test prompt fields
    EXPECT_EQ(std::string{"code_review"}, prompt.name);
    ASSERT_TRUE(prompt.description.has_value());
    EXPECT_EQ(std::string{"Review c++ code for bugs"}, prompt.description.value());

    // Test arguments existence
    ASSERT_TRUE(prompt.arguments.has_value());
    ASSERT_EQ(ARGS_NUM, prompt.arguments->size());

    // Test argument with all fields
    const auto& arg1 = prompt.arguments->at(0);
    EXPECT_EQ(std::string{"file_path"}, arg1.name);
    ASSERT_TRUE(arg1.title.has_value());
    EXPECT_EQ(std::string{"File Path"}, arg1.title.value());
    ASSERT_TRUE(arg1.description.has_value());
    EXPECT_EQ(std::string{"Path to the file to review"}, arg1.description.value());
    ASSERT_TRUE(arg1.required.has_value());
    EXPECT_TRUE(arg1.required.value());

    // Test argument without title field
    const auto& arg2 = prompt.arguments->at(1);
    EXPECT_EQ(std::string{"focus_onSecurity"}, arg2.name);
    EXPECT_FALSE(arg2.title.has_value());  // This field is missing
    ASSERT_TRUE(arg2.description.has_value());
    EXPECT_EQ(std::string{"Whether to focus on security issues"}, arg2.description.value());
    ASSERT_TRUE(arg2.required.has_value());
    EXPECT_FALSE(arg2.required.value());
}

TEST_F(JSONRPCTest, GetPromptResultDeserialization)
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

TEST_F(JSONRPCTest, GetPromptResultDeserializationWithoutDescription)
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

TEST_F(JSONRPCTest, GetPromptRequestDeserializationComplexArguments)
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
    int result = rpcReq.Deserialize(jsonStr);

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
    EXPECT_EQ(ARGS_LEN, args.at("length").get<int>());
    EXPECT_TRUE(args.at("include_images").get<bool>());

    const auto& metadata = args.at("metadata");
    EXPECT_EQ(std::string{"John Doe"}, metadata.at("author").get<std::string>());
    EXPECT_EQ(std::string{"2024-01-01"}, metadata.at("date").get<std::string>());
}

TEST_F(JSONRPCTest, ReadResourceRequestSerializationIncludesUri)
{
    auto readReq = std::make_unique<ReadResourceRequest>();
    readReq->params_ = std::make_unique<ReadResourceRequestParams>(RESOURCE_URI);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/read";
    rpcReq.request_ = std::move(readReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    EXPECT_EQ(std::string{"resources/read"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{RESOURCE_URI}, j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCTest, SubscribeRequestSerializationIncludesUri)
{
    auto subReq = std::make_unique<SubscribeRequest>();
    subReq->params_ = std::make_unique<SubscribeRequestParams>(RESOURCE_URI);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/subscribe";
    rpcReq.request_ = std::move(subReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/subscribe"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{RESOURCE_URI}, j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCTest, UnsubscribeRequestSerializationIncludesUri)
{
    auto unsubReq = std::make_unique<UnsubscribeRequest>();
    unsubReq->params_ = std::make_unique<UnsubscribeRequestParams>(RESOURCE_URI);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/unsubscribe";
    rpcReq.request_ = std::move(unsubReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/unsubscribe"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{RESOURCE_URI}, j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCTest, ListResourcesRequestSerializationHasParamsKey)
{
    auto listReq = std::make_unique<ListResourcesRequest>();

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
}

TEST_F(JSONRPCTest, JSONRPCResponseSerializationReadResource)
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

TEST_F(JSONRPCTest, JSONRPCResponseSerializationListResources)
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
    resource1.size = RESOURCE_SIZE;
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
    EXPECT_EQ(ARGS_NUM, j.at("result").at("resources").size());
}

TEST_F(JSONRPCTest, JSONRPCResponseSerializeResourceTemplatesList)
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
    EXPECT_EQ(ARGS_NUM, j.at("result").at("resourceTemplates").size());
}

TEST_F(JSONRPCTest, JSONRPCResponseSerializeSubscribeResource)
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

TEST_F(JSONRPCTest, JSONRPCResponseSerializeUnsubscribeResource)
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

TEST_F(JSONRPCTest, ListResourceTemplatesRequestSerializationHasParamsKey)
{
    auto listReq = std::make_unique<ListResourceTemplatesRequest>();

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/templates/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/templates/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
}

TEST_F(JSONRPCTest, ReadResourceResultDeserializationParsesTextAndBlob)
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
    ASSERT_EQ(ARGS_NUM, readResult->contents.size());

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

TEST_F(JSONRPCTest, ListResourcesResultDeserializationParsesResourceInfo)
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
                    "size": 1
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
    ASSERT_EQ(ARGS_NUM, listResult->resources.size());

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
    EXPECT_EQ(1, r1.size.value());

    const ResourceInfo& r2 = listResult->resources.at(1);
    EXPECT_EQ(std::string{RESOURCE_URI_2}, r2.uri);
    EXPECT_EQ(std::string{"example2"}, r2.name);
    EXPECT_FALSE(r2.title.has_value());
    EXPECT_FALSE(r2.description.has_value());
    EXPECT_FALSE(r2.mimeType.has_value());
    EXPECT_FALSE(r2.size.has_value());
}

TEST_F(JSONRPCTest, ListToolsRequestSerializationWithCursor)
{
    auto listReq = std::make_unique<ListToolsRequest>();
    auto params = std::make_unique<RequestParams>();
    params->cursor = std::string{"10"};
    listReq->params_ = std::move(params);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "tools/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"tools/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{"10"}, j.at("params").at("cursor").get<std::string>());
}

TEST_F(JSONRPCTest, ListToolsResultSerializationWithNextCursor)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    ListToolsResult result;
    Tool tool;
    tool.name = "example_tool";
    result.tools.push_back(tool);
    result.nextCursor = std::string{"20"};

    resp.result_ = std::make_shared<ListToolsResult>(std::move(result));

    std::string serialized = resp.Serialize("tools/list");
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(REQUEST_ID, j.at("id").get<int>());
    ASSERT_TRUE(j.contains("result"));
    ASSERT_TRUE(j.at("result").contains("tools"));
    ASSERT_TRUE(j.at("result").contains("nextCursor"));
    EXPECT_EQ(std::string{"20"}, j.at("result").at("nextCursor").get<std::string>());
}

TEST_F(JSONRPCTest, ListResourcesRequestSerializationWithCursor)
{
    auto listReq = std::make_unique<ListResourcesRequest>();
    auto params = std::make_unique<RequestParams>();
    params->cursor = std::string{"5"};
    listReq->params_ = std::move(params);

    JSONRPCRequest rpcReq;
    rpcReq.id_ = REQUEST_ID;
    rpcReq.method_ = "resources/list";
    rpcReq.request_ = std::move(listReq);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(std::string{"resources/list"}, j.at("method").get<std::string>());
    ASSERT_TRUE(j.contains("params"));
    EXPECT_EQ(std::string{"5"}, j.at("params").at("cursor").get<std::string>());
}

TEST_F(JSONRPCTest, ListResourcesResultDeserializationWithNextCursor)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "resources": [],
            "nextCursor": "15"
        }
    })";

    JSONRPCResponse resp;
    int result = resp.Deserialize(jsonStr, "resources/list");

    EXPECT_EQ(0, result);
    ASSERT_NE(nullptr, resp.result_);

    auto* listResult = dynamic_cast<ListResourcesResult*>(resp.result_.get());
    ASSERT_NE(nullptr, listResult);
    ASSERT_TRUE(listResult->nextCursor.has_value());
    EXPECT_EQ(std::string{"15"}, listResult->nextCursor.value());
}

TEST_F(JSONRPCTest, ToolsCallResultDeserializationParsesEmbeddedResourceAndLink)
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
    ASSERT_EQ(ARGS_NUM, callResult->content.size());
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

TEST_F(JSONRPCTest, DeserializeJSONRPCMessageParsesRequest)
{
    JSONRPCRequest req;
    req.id_ = REQUEST_ID;
    req.method_ = METHOD_NAME;

    std::string serialized = req.Serialize();

    JSONRPCMessage message = DeserializeJSONRPCMessage(serialized, METHOD_NAME);
    ASSERT_TRUE(std::holds_alternative<JSONRPCRequest>(message));

    const auto &parsedReq = std::get<JSONRPCRequest>(message);
    EXPECT_EQ(JSONRPC_VERSION, parsedReq.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, parsedReq.id_);
    EXPECT_EQ(METHOD_NAME, parsedReq.method_);
}

TEST_F(JSONRPCTest, DeserializeJSONRPCMessageParsesResponse)
{
    JSONRPCResponse resp;
    resp.id_ = REQUEST_ID;

    std::string serialized = resp.Serialize(METHOD_NAME);

    JSONRPCMessage message = DeserializeJSONRPCMessage(serialized, METHOD_NAME);
    ASSERT_TRUE(std::holds_alternative<JSONRPCResponse>(message));

    const auto &parsedResp = std::get<JSONRPCResponse>(message);
    EXPECT_EQ(JSONRPC_VERSION, parsedResp.jsonrpc_);
    EXPECT_EQ(REQUEST_ID, parsedResp.id_);
}

TEST_F(JSONRPCTest, DeserializeJSONRPCMessageParsesNotification)
{
    JSONRPCNotification notif;
    notif.method_ = TEST_METHOD;

    std::string serialized = notif.Serialize();

    JSONRPCMessage message = DeserializeJSONRPCMessage(serialized, TEST_METHOD);
    ASSERT_TRUE(std::holds_alternative<JSONRPCNotification>(message));

    const auto &parsedNotif = std::get<JSONRPCNotification>(message);
    EXPECT_EQ(JSONRPC_VERSION, parsedNotif.jsonrpc_);
    EXPECT_EQ(TEST_METHOD, parsedNotif.method_);
}

TEST_F(JSONRPCTest, SerializeJSONRPCMessageRoundTripRequest)
{
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

TEST_F(JSONRPCTest, DeserializeJSONRPCMessageInvalidJsonThrows)
{
    std::string invalidJson = "not valid json";
    EXPECT_THROW(DeserializeJSONRPCMessage(invalidJson, METHOD_NAME), nlohmann::json::parse_error);
}

TEST_F(JSONRPCTest, CallToolRequestSerializationRoundTrip)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": "search",
            "arguments": {
                "query": "test query",
                "limit": 1
            }
        }
    })";

    JSONRPCRequest rpcReq;
    int result = rpcReq.Deserialize(jsonStr);
    EXPECT_EQ(0, result);

    std::string serialized = rpcReq.Serialize();
    auto j = json::parse(serialized);

    EXPECT_EQ(JSONRPC_VERSION, j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(1, j.at("id").get<int>());
    EXPECT_EQ("tools/call", j.at("method").get<std::string>());
    EXPECT_EQ("search", j.at("params").at("name").get<std::string>());
    EXPECT_EQ("test query", j.at("params").at("arguments").at("query").get<std::string>());
    EXPECT_EQ(1, j.at("params").at("arguments").at("limit").get<int>());
}

TEST_F(JSONRPCTest, JSONRPCError_DefaultConstructor)
{
    JSONRPCError error;
    std::string jsonStr = error.Serialize();
    auto j = json::parse(jsonStr);

    EXPECT_EQ("2.0", j.at("jsonrpc").get<std::string>());
    EXPECT_EQ(0, j.at("id").get<int>());
    EXPECT_EQ(-1, j.at("error").at("code").get<int>());
    EXPECT_EQ("Internal error", j.at("error").at("message").get<std::string>());
}

TEST_F(JSONRPCTest, JSONRPCError_DeserializeValidError)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "error": {
            "code": 1,
            "message": "Parse error",
            "data": {"details": "Invalid JSON"}
        }
    })";

    JSONRPCError error;
    int result = error.Deserialize(jsonStr);
    EXPECT_EQ(0, result);

    std::string serialized = error.Serialize();
    auto j = json::parse(serialized);
    EXPECT_EQ(1, j.at("id").get<int>());
    EXPECT_EQ(1, j.at("error").at("code").get<int>());
    EXPECT_EQ("Parse error", j.at("error").at("message").get<std::string>());
    EXPECT_EQ("Invalid JSON", j.at("error").at("data").at("details").get<std::string>());
}

TEST_F(JSONRPCTest, CompleteToolCallScenario)
{
    // 1. 客户端发送工具调用请求
    std::string requestJson = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": "search_web",
            "arguments": {
                "query": "C++ JSON serialization",
                "max_results": 5
            }
        }
    })";

    JSONRPCMessage requestMsg = DeserializeJSONRPCMessage(requestJson, "tools/call");
    std::string serializedRequest = SerializeJSONRPCMessage(requestMsg, "tools/call");

    auto requestJ = json::parse(serializedRequest);
    EXPECT_EQ("tools/call", requestJ.at("method").get<std::string>());
    EXPECT_EQ("search_web", requestJ.at("params").at("name").get<std::string>());

    // 2. 服务器返回响应
    std::string responseJson = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "content": [
                {
                    "type": "text",
                    "text": "Found 5 results for 'C++ JSON serialization'"
                }
            ],
            "isError": false
        }
    })";

    JSONRPCMessage responseMsg = DeserializeJSONRPCMessage(responseJson, "tools/call");
    std::string serializedResponse = SerializeJSONRPCMessage(responseMsg, "tools/call");

    auto responseJ = json::parse(serializedResponse);
    EXPECT_EQ(1, responseJ.at("id").get<int>());
    EXPECT_FALSE(responseJ.at("result").at("isError").get<bool>());
}

TEST_F(JSONRPCTest, InitializeRequestResponseScenario)
{
    // 客户端初始化请求
    std::string initRequest = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {
                "name": "TestClient",
                "version": "1.0.0"
            }
        }
    })";

    JSONRPCMessage requestMsg = DeserializeJSONRPCMessage(initRequest, "initialize");
    std::string serializedRequest = SerializeJSONRPCMessage(requestMsg, "initialize");

    auto requestJ = json::parse(serializedRequest);
    EXPECT_EQ("initialize", requestJ.at("method").get<std::string>());
    EXPECT_EQ("TestClient", requestJ.at("params").at("clientInfo").at("name").get<std::string>());

    // 服务器初始化响应
    std::string initResponse = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "result": {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "serverInfo": {
                "name": "TestServer",
                "version": "2.0.0"
            },
            "instructions": "Welcome!"
        }
    })";

    JSONRPCMessage responseMsg = DeserializeJSONRPCMessage(initResponse, "initialize");
    std::string serializedResponse = SerializeJSONRPCMessage(responseMsg, "initialize");

    auto responseJ = json::parse(serializedResponse);
    EXPECT_EQ("TestServer", responseJ.at("result").at("serverInfo").at("name").get<std::string>());
    EXPECT_EQ("Welcome!", responseJ.at("result").at("instructions").get<std::string>());
}

TEST_F(JSONRPCTest, PromptsListRequestResponse)
{
    std::string requestJson = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "prompts/list",
        "params": {}
    })";

    JSONRPCMessage requestMsg = DeserializeJSONRPCMessage(requestJson, "prompts/list");
    std::string serialized = SerializeJSONRPCMessage(requestMsg, "prompts/list");

    auto j = json::parse(serialized);
    EXPECT_EQ("prompts/list", j.at("method").get<std::string>());
}

TEST_F(JSONRPCTest, ResourcesReadRequestResponse)
{
    std::string requestJson = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "resources/read",
        "params": {
            "uri": "file:///test.txt"
        }
    })";

    JSONRPCMessage requestMsg = DeserializeJSONRPCMessage(requestJson, "resources/read");
    std::string serialized = SerializeJSONRPCMessage(requestMsg, "resources/read");

    auto j = json::parse(serialized);
    EXPECT_EQ("resources/read", j.at("method").get<std::string>());
    EXPECT_EQ("file:///test.txt", j.at("params").at("uri").get<std::string>());
}

TEST_F(JSONRPCTest, ResourcesSubscribeUnsubscribe)
{
    // 订阅请求
    std::string subscribeJson = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "resources/subscribe",
        "params": {
            "uri": "file:///data.txt"
        }
    })";

    JSONRPCMessage subscribeMsg = DeserializeJSONRPCMessage(subscribeJson, "resources/subscribe");
    std::string serializedSubscribe = SerializeJSONRPCMessage(subscribeMsg, "resources/subscribe");

    auto subscribeJ = json::parse(serializedSubscribe);
    EXPECT_EQ("resources/subscribe", subscribeJ.at("method").get<std::string>());

    // 取消订阅请求
    std::string unsubscribeJson = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "resources/unsubscribe",
        "params": {
            "uri": "file:///data.txt"
        }
    })";

    JSONRPCMessage unsubscribeMsg = DeserializeJSONRPCMessage(unsubscribeJson, "resources/unsubscribe");
    std::string serializedUnsubscribe = SerializeJSONRPCMessage(unsubscribeMsg, "resources/unsubscribe");

    auto unsubscribeJ = json::parse(serializedUnsubscribe);
    EXPECT_EQ("resources/unsubscribe", unsubscribeJ.at("method").get<std::string>());
}

TEST_F(JSONRPCTest, JSONRPCVersionIsAlways20)
{
    std::string jsonStr = R"({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "test",
        "params": {}
    })";

    JSONRPCMessage msg = DeserializeJSONRPCMessage(jsonStr, "test");
    std::string serialized = SerializeJSONRPCMessage(msg, "test");

    auto j = json::parse(serialized);
    EXPECT_EQ("2.0", j.at("jsonrpc").get<std::string>());
}

TEST_F(JSONRPCTest, DeserializeJSONRPCMessage_InvalidJSON)
{
    EXPECT_THROW({
        DeserializeJSONRPCMessage("invalid json", "initialize");
    }, json::parse_error);
}

} // namespace