/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <gtest/gtest.h>
#include <atomic>
#include <future>
#include <thread>
#include "resource_manager.h"

static constexpr int CONTENT_SIZE = 2;
static constexpr int LOOPNUM = 3;
static constexpr int RESOURCE_SIZE = 2048;

namespace Mcp {

// Helper functions to create test data
ResourceInfo CreateResourceInfo(const std::string& uri, const std::string& name)
{
    ResourceInfo info;
    constexpr int defaultSize = 100;
    info.uri = uri;
    info.name = name;
    info.mimeType = "text/plain";
    info.size = defaultSize;
    return info;
}

ResourceTemplate CreateResourceTemplate(const std::string& uriTemplate, const std::string& name)
{
    ResourceTemplate template_;
    template_.uriTemplate = uriTemplate;
    template_.name = name;
    template_.mimeType = "application/json";
    return template_;
}

ReadResourceResult CreateTestReadResourceResult(const std::string& uri)
{
    ReadResourceResult result;
    TextResourceContents contents;
    contents.uri = uri;
    contents.text = "Resource content for " + uri;
    result.contents.push_back(contents);
    return result;
}

// Test fixture for ResourceManager
class ResourceManagerTest : public ::testing::Test {
public:
    ~ResourceManagerTest() {}
protected:
    void SetUp() override
    {
        // Create test resources
        resource1 = CreateResourceInfo("resource://test/file1.txt", "File 1");
        resource2 = CreateResourceInfo("resource://test/file2.txt", "File 2");
        resource3 = CreateResourceInfo("resource://test/file3.txt", "File 3");

        // Create test templates
        template1 = CreateResourceTemplate("resource://test/{id}.txt", "Dynamic Resource");
        template2 = CreateResourceTemplate("resource://data/{type}/{id}", "Data Resource");
    }

    ResourceInfo resource1;
    ResourceInfo resource2;
    ResourceInfo resource3;

    ResourceTemplate template1;
    ResourceTemplate template2;

    // Simple read function for testing
    ReadResourceFunc createSimpleReadFunc(const std::string& expectedContent)
    {
        return [expectedContent](const ServerContext& /* ctx */, const std::string& uri) {
            ReadResourceResult result;
            TextResourceContents contents;
            contents.uri = uri;
            contents.text = expectedContent;
            result.contents.push_back(contents);
            return result;
        };
    }
};

// Test constructor
TEST_F(ResourceManagerTest, Constructor_Default)
{
    ResourceManager manager;
    SUCCEED(); // Should not throw
}

TEST_F(ResourceManagerTest, Constructor_WithOverwrite)
{
    ResourceManager manager(true);
    ResourceManager managerNoOverwrite(false);
    SUCCEED(); // Should not throw
}

// Test AddResource
TEST_F(ResourceManagerTest, AddResource_Success)
{
    ResourceManager manager(true);

    auto readFunc = createSimpleReadFunc("Test content");

    EXPECT_NO_THROW(manager.AddResource(resource1, readFunc));
}

TEST_F(ResourceManagerTest, AddResource_EmptyUriThrows)
{
    ResourceManager manager(true);

    ResourceInfo emptyUriResource;
    emptyUriResource.uri = "";
    emptyUriResource.name = "Test";

    auto readFunc = createSimpleReadFunc("Test");

    EXPECT_THROW(manager.AddResource(emptyUriResource, readFunc), std::invalid_argument);
}

TEST_F(ResourceManagerTest, AddResource_EmptyNameThrows)
{
    ResourceManager manager(true);

    ResourceInfo emptyNameResource;
    emptyNameResource.uri = "resource://test/file.txt";
    emptyNameResource.name = "";

    auto readFunc = createSimpleReadFunc("Test");

    EXPECT_THROW(manager.AddResource(emptyNameResource, readFunc), std::invalid_argument);
}

TEST_F(ResourceManagerTest, AddResource_NullReadFuncThrows)
{
    ResourceManager manager(true);

    EXPECT_THROW(manager.AddResource(resource1, nullptr), std::invalid_argument);
}

TEST_F(ResourceManagerTest, AddResource_OverwriteExisting)
{
    ResourceManager manager(true);

    auto readFunc1 = createSimpleReadFunc("First content");
    auto readFunc2 = createSimpleReadFunc("Second content");

    // First add
    EXPECT_NO_THROW(manager.AddResource(resource1, readFunc1));

    // Overwrite with different function
    EXPECT_NO_THROW(manager.AddResource(resource1, readFunc2));

    // Verify the second function is used
    ReadResourceResult result = manager.ReadResource(resource1.uri);
    ASSERT_EQ(result.contents.size(), 1);
    const auto& contents = std::get<TextResourceContents>(result.contents[0]);
    EXPECT_EQ(contents.text, "Second content");
}

TEST_F(ResourceManagerTest, AddResource_NoOverwriteThrowsWhenExists)
{
    ResourceManager manager(false);

    auto readFunc = createSimpleReadFunc("Test");

    EXPECT_NO_THROW(manager.AddResource(resource1, readFunc));

    // Try to add again - should throw
    EXPECT_THROW(manager.AddResource(resource1, readFunc), std::runtime_error);
}

// Test RemoveResource
TEST_F(ResourceManagerTest, RemoveResource_Success)
{
    ResourceManager manager(true);

    auto readFunc = createSimpleReadFunc("Test");

    manager.AddResource(resource1, readFunc);
    manager.AddResource(resource2, readFunc);

    // Remove resource1
    EXPECT_NO_THROW(manager.RemoveResource(resource1.uri));

    // Verify resource1 is removed, resource2 still exists
    ListResourcesResult listResult = manager.ListResources();
    EXPECT_EQ(listResult.resources.size(), 1);
    EXPECT_EQ(listResult.resources[0].uri, resource2.uri);
}

TEST_F(ResourceManagerTest, RemoveResource_EmptyUriThrows)
{
    ResourceManager manager(true);

    EXPECT_THROW(manager.RemoveResource(""), std::invalid_argument);
}

TEST_F(ResourceManagerTest, RemoveResource_NonExistentThrows)
{
    ResourceManager manager(true);

    auto readFunc = createSimpleReadFunc("Test");
    manager.AddResource(resource1, readFunc);

    EXPECT_THROW(manager.RemoveResource("nonexistent://resource"), std::runtime_error);
}

// Test ListResources
TEST_F(ResourceManagerTest, ListResources_Empty)
{
    ResourceManager manager(true);

    ListResourcesResult result = manager.ListResources();
    EXPECT_TRUE(result.resources.empty());
}

TEST_F(ResourceManagerTest, ListResources_MultipleResources)
{
    ResourceManager manager(true);

    auto readFunc = createSimpleReadFunc("Test");

    manager.AddResource(resource1, readFunc);
    manager.AddResource(resource2, readFunc);
    manager.AddResource(resource3, readFunc);

    ListResourcesResult result = manager.ListResources();
    EXPECT_EQ(result.resources.size(), LOOPNUM);
}

TEST_F(ResourceManagerTest, ListResources_ResourceInfoPreserved)
{
    ResourceManager manager(true);

    ResourceInfo detailedResource;
    detailedResource.uri = "resource://detailed/file.txt";
    detailedResource.name = "Detailed Resource";
    detailedResource.title = "Resource Title";
    detailedResource.description = "A detailed resource description";
    detailedResource.mimeType = "text/markdown";
    detailedResource.size = RESOURCE_SIZE;

    Icon icon;
    icon.src = "icon.png";
    icon.mimeType = "image/png";
    detailedResource.icons = std::vector<Icon>{icon};

    Annotations annotations;
    annotations.priority = 1.0;
    detailedResource.annotations = annotations;

    auto readFunc = createSimpleReadFunc("Detailed content");
    manager.AddResource(detailedResource, readFunc);

    ListResourcesResult result = manager.ListResources();
    ASSERT_EQ(result.resources.size(), 1);

    const auto& retrievedResource = result.resources[0];
    EXPECT_EQ(retrievedResource.uri, detailedResource.uri);
    EXPECT_EQ(retrievedResource.name, detailedResource.name);
    EXPECT_EQ(retrievedResource.title.value(), detailedResource.title.value());
    EXPECT_EQ(retrievedResource.description.value(), detailedResource.description.value());
    EXPECT_EQ(retrievedResource.mimeType.value(), detailedResource.mimeType.value());
    EXPECT_EQ(retrievedResource.size.value(), detailedResource.size.value());
    ASSERT_TRUE(retrievedResource.icons.has_value());
    EXPECT_EQ(retrievedResource.icons->size(), 1);
    EXPECT_EQ(retrievedResource.icons->at(0).src, "icon.png");
    ASSERT_TRUE(retrievedResource.annotations.has_value());
    EXPECT_EQ(retrievedResource.annotations->priority.value(), 1.0);
}

// Test ReadResource
TEST_F(ResourceManagerTest, ReadResource_Success)
{
    ResourceManager manager(true);

    std::string expectedContent = "This is resource content";
    auto readFunc = [expectedContent](const ServerContext&, const std::string& uri) {
        ReadResourceResult result;
        TextResourceContents contents;
        contents.uri = uri;
        contents.text = expectedContent;
        result.contents.push_back(contents);
        return result;
    };

    manager.AddResource(resource1, readFunc);

    ReadResourceResult result = manager.ReadResource(resource1.uri);
    ASSERT_EQ(result.contents.size(), 1);

    const auto& contents = std::get<TextResourceContents>(result.contents[0]);
    EXPECT_EQ(contents.uri, resource1.uri);
    EXPECT_EQ(contents.text, expectedContent);
}

TEST_F(ResourceManagerTest, ReadResource_BlobResource)
{
    ResourceManager manager(true);

    auto readFunc = [](const ServerContext&, const std::string& uri) {
        ReadResourceResult result;
        BlobResourceContents contents;
        contents.uri = uri;
        contents.blob = "SGVsbG8gV29ybGQh"; // Base64 for "Hello World!"
        contents.mimeType = "image/png";
        result.contents.push_back(contents);
        return result;
    };

    manager.AddResource(resource1, readFunc);

    ReadResourceResult result = manager.ReadResource(resource1.uri);
    ASSERT_EQ(result.contents.size(), 1);

    const auto& contents = std::get<BlobResourceContents>(result.contents[0]);
    EXPECT_EQ(contents.uri, resource1.uri);
    EXPECT_EQ(contents.blob, "SGVsbG8gV29ybGQh");
    EXPECT_EQ(contents.mimeType.value(), "image/png");
}

TEST_F(ResourceManagerTest, ReadResource_MultipleContents)
{
    ResourceManager manager(true);

    auto readFunc = [](const ServerContext&, const std::string& uri) {
        ReadResourceResult result;

        // Add text content
        TextResourceContents textContents;
        textContents.uri = uri;
        textContents.text = "Text content";
        result.contents.push_back(textContents);

        // Add blob content
        BlobResourceContents blobContents;
        blobContents.uri = uri + "#attachment";
        blobContents.blob = "QXR0YWNobWVudA=="; // Base64 for "Attachment"
        blobContents.mimeType = "application/octet-stream";
        result.contents.push_back(blobContents);

        return result;
    };

    manager.AddResource(resource1, readFunc);

    ReadResourceResult result = manager.ReadResource(resource1.uri);
    EXPECT_EQ(result.contents.size(), CONTENT_SIZE);

    // Check first content is text
    EXPECT_TRUE(std::holds_alternative<TextResourceContents>(result.contents[0]));
    // Check second content is blob
    EXPECT_TRUE(std::holds_alternative<BlobResourceContents>(result.contents[1]));
}

TEST_F(ResourceManagerTest, ReadResource_NonExistentThrows)
{
    ResourceManager manager(true);

    EXPECT_THROW(manager.ReadResource("nonexistent://resource"), std::runtime_error);
}

TEST_F(ResourceManagerTest, ReadResource_HandlerReceivesCorrectUri)
{
    ResourceManager manager(true);

    std::string receivedUri;
    auto readFunc = [&receivedUri](const ServerContext&, const std::string& uri) {
        receivedUri = uri;
        return CreateTestReadResourceResult(uri);
    };

    manager.AddResource(resource1, readFunc);

    manager.ReadResource(resource1.uri);
    EXPECT_EQ(receivedUri, resource1.uri);
}

// Test AddResourceTemplate
TEST_F(ResourceManagerTest, AddResourceTemplate_Success)
{
    ResourceManager manager(true);

    EXPECT_NO_THROW(manager.AddResourceTemplate(template1));
}

TEST_F(ResourceManagerTest, AddResourceTemplate_EmptyUriTemplateThrows)
{
    ResourceManager manager(true);

    ResourceTemplate emptyUriTemplate;
    emptyUriTemplate.uriTemplate = "";
    emptyUriTemplate.name = "Test";

    EXPECT_THROW(manager.AddResourceTemplate(emptyUriTemplate), std::invalid_argument);
}

TEST_F(ResourceManagerTest, AddResourceTemplate_EmptyNameThrows)
{
    ResourceManager manager(true);

    ResourceTemplate emptyNameTemplate;
    emptyNameTemplate.uriTemplate = "template://{id}";
    emptyNameTemplate.name = "";

    EXPECT_THROW(manager.AddResourceTemplate(emptyNameTemplate), std::invalid_argument);
}

TEST_F(ResourceManagerTest, AddResourceTemplate_OverwriteExisting)
{
    ResourceManager manager(true);

    ResourceTemplate templateA;
    templateA.uriTemplate = "template://test";
    templateA.name = "First Template";
    templateA.title = "First";

    ResourceTemplate templateB;
    templateB.uriTemplate = "template://test";
    templateB.name = "Second Template";
    templateB.title = "Second";

    EXPECT_NO_THROW(manager.AddResourceTemplate(templateA));
    EXPECT_NO_THROW(manager.AddResourceTemplate(templateB));

    ListResourceTemplatesResult result = manager.ListResourceTemplates();
    ASSERT_EQ(result.resourceTemplates.size(), 1);
    EXPECT_EQ(result.resourceTemplates[0].name, "Second Template");
    EXPECT_EQ(result.resourceTemplates[0].title.value(), "Second");
}

TEST_F(ResourceManagerTest, AddResourceTemplate_NoOverwriteThrowsWhenExists)
{
    ResourceManager manager(false);

    EXPECT_NO_THROW(manager.AddResourceTemplate(template1));

    // Try to add again - should throw
    EXPECT_THROW(manager.AddResourceTemplate(template1), std::runtime_error);
}

// Test RemoveResourceTemplate
TEST_F(ResourceManagerTest, RemoveResourceTemplate_Success)
{
    ResourceManager manager(true);

    manager.AddResourceTemplate(template1);
    manager.AddResourceTemplate(template2);

    // Remove template1
    EXPECT_NO_THROW(manager.RemoveResourceTemplate(template1.uriTemplate));

    // Verify template1 is removed, template2 still exists
    ListResourceTemplatesResult result = manager.ListResourceTemplates();
    EXPECT_EQ(result.resourceTemplates.size(), 1);
    EXPECT_EQ(result.resourceTemplates[0].uriTemplate, template2.uriTemplate);
}

TEST_F(ResourceManagerTest, RemoveResourceTemplate_EmptyUriTemplateThrows)
{
    ResourceManager manager(true);

    EXPECT_THROW(manager.RemoveResourceTemplate(""), std::invalid_argument);
}

TEST_F(ResourceManagerTest, RemoveResourceTemplate_NonExistentThrows)
{
    ResourceManager manager(true);

    manager.AddResourceTemplate(template1);

    EXPECT_THROW(manager.RemoveResourceTemplate("nonexistent://template"), std::runtime_error);
}

// Test ListResourceTemplates
TEST_F(ResourceManagerTest, ListResourceTemplates_Empty)
{
    ResourceManager manager(true);

    ListResourceTemplatesResult result = manager.ListResourceTemplates();
    EXPECT_TRUE(result.resourceTemplates.empty());
}

TEST_F(ResourceManagerTest, ListResourceTemplates_MultipleTemplates)
{
    ResourceManager manager(true);

    manager.AddResourceTemplate(template1);
    manager.AddResourceTemplate(template2);

    ListResourceTemplatesResult result = manager.ListResourceTemplates();
    EXPECT_EQ(result.resourceTemplates.size(), CONTENT_SIZE);
}

TEST_F(ResourceManagerTest, ListResourceTemplates_TemplateInfoPreserved)
{
    ResourceManager manager(true);

    ResourceTemplate detailedTemplate;
    detailedTemplate.uriTemplate = "resource://{namespace}/{id}/data";
    detailedTemplate.name = "Detailed Template";
    detailedTemplate.title = "Template Title";
    detailedTemplate.description = "A detailed template description";
    detailedTemplate.mimeType = "application/json";

    Icon icon;
    icon.src = "template-icon.png";
    icon.mimeType = "image/svg+xml";
    detailedTemplate.icons = std::vector<Icon>{icon};

    Annotations annotations;
    annotations.priority = 1.0;
    detailedTemplate.annotations = annotations;

    manager.AddResourceTemplate(detailedTemplate);

    ListResourceTemplatesResult result = manager.ListResourceTemplates();
    ASSERT_EQ(result.resourceTemplates.size(), 1);

    const auto& retrievedTemplate = result.resourceTemplates[0];
    EXPECT_EQ(retrievedTemplate.uriTemplate, detailedTemplate.uriTemplate);
    EXPECT_EQ(retrievedTemplate.name, detailedTemplate.name);
    EXPECT_EQ(retrievedTemplate.title.value(), detailedTemplate.title.value());
    EXPECT_EQ(retrievedTemplate.description.value(), detailedTemplate.description.value());
    EXPECT_EQ(retrievedTemplate.mimeType.value(), detailedTemplate.mimeType.value());
    ASSERT_TRUE(retrievedTemplate.icons.has_value());
    EXPECT_EQ(retrievedTemplate.icons->size(), 1);
    EXPECT_EQ(retrievedTemplate.icons->at(0).src, "template-icon.png");
    ASSERT_TRUE(retrievedTemplate.annotations.has_value());
    EXPECT_EQ(retrievedTemplate.annotations->priority.value(), 1.0);
}

// Test SubscribeResource
TEST_F(ResourceManagerTest, SubscribeResource_Success)
{
    ResourceManager manager(true);

    auto readFunc = createSimpleReadFunc("Test");
    manager.AddResource(resource1, readFunc);

    // Should not throw for existing resource
    EXPECT_NO_THROW(manager.SubscribeResource(resource1.uri));
}

TEST_F(ResourceManagerTest, SubscribeResource_NonExistentThrows)
{
    ResourceManager manager(true);

    EXPECT_THROW(manager.SubscribeResource("nonexistent://resource"), std::runtime_error);
}

// Test UnsubscribeResource
TEST_F(ResourceManagerTest, UnsubscribeResource_Success)
{
    ResourceManager manager(true);

    auto readFunc = createSimpleReadFunc("Test");
    manager.AddResource(resource1, readFunc);
    // Should not throw for existing resource
    EXPECT_NO_THROW(manager.UnsubscribeResource(resource1.uri));
}

TEST_F(ResourceManagerTest, UnsubscribeResource_NonExistentThrows)
{
    ResourceManager manager(true);

    EXPECT_THROW(manager.UnsubscribeResource("nonexistent://resource"), std::runtime_error);
}

// Test concurrency
TEST_F(ResourceManagerTest, Concurrency_AddAndListResources)
{
    ResourceManager manager(true);

    constexpr int kNumThreads = 5;
    constexpr int kResourcesPerThread = 10;
    std::atomic<int> addedCount{0};

    auto addThreadFunc = [&manager, &addedCount](int threadId) {
        for (int i = 0; i < kResourcesPerThread; i++) {
            ResourceInfo resource;
            resource.uri = "resource://thread" + std::to_string(threadId) + "/file" + std::to_string(i) + ".txt";
            resource.name = "Resource from thread " + std::to_string(threadId);

            auto readFunc = [](const ServerContext&, const std::string& uri) {
                return CreateTestReadResourceResult(uri);
            };

            try {
                manager.AddResource(resource, readFunc);
                addedCount++;
            } catch (const std::exception& e) {
                // Might get duplicate if overwrite=false, but we're using overwrite=true
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; i++) {
        threads.emplace_back(addThreadFunc, i);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all adds completed without deadlock
    ListResourcesResult result = manager.ListResources();
    EXPECT_EQ(result.resources.size(), addedCount);
}

// Test large number of resources
TEST_F(ResourceManagerTest, LargeNumberOfResources)
{
    ResourceManager manager(true);

    auto readFunc = createSimpleReadFunc("Content");

    constexpr int kNumResources = 100;
    for (int i = 0; i < kNumResources; i++) {
        ResourceInfo resource;
        resource.uri = "resource://batch/resource" + std::to_string(i) + ".txt";
        resource.name = "Resource " + std::to_string(i);

        EXPECT_NO_THROW(manager.AddResource(resource, readFunc));
    }

    std::optional<std::string> cursor;
    std::size_t total = 0;
    do {
        ListResourcesResult page = manager.ListResources(cursor);
        total += page.resources.size();
        cursor = page.nextCursor;
    } while (cursor.has_value());

    EXPECT_EQ(total, static_cast<std::size_t>(kNumResources));

    // Verify we can read resources from the middle
    EXPECT_NO_THROW(manager.ReadResource("resource://batch/resource50.txt"));
    EXPECT_NO_THROW(manager.ReadResource("resource://batch/resource99.txt"));
}

// Test resource and template independence
TEST_F(ResourceManagerTest, ResourceAndTemplateIndependence)
{
    ResourceManager manager(true);

    // Add resources
    auto readFunc = createSimpleReadFunc("Resource content");
    manager.AddResource(resource1, readFunc);
    manager.AddResource(resource2, readFunc);

    // Add templates
    manager.AddResourceTemplate(template1);
    manager.AddResourceTemplate(template2);

    // Verify resources and templates are separate
    ListResourcesResult resourcesResult = manager.ListResources();
    ListResourceTemplatesResult templatesResult = manager.ListResourceTemplates();

    EXPECT_EQ(resourcesResult.resources.size(), CONTENT_SIZE);
    EXPECT_EQ(templatesResult.resourceTemplates.size(), CONTENT_SIZE);

    // Remove a resource should not affect templates
    manager.RemoveResource(resource1.uri);

    resourcesResult = manager.ListResources();
    templatesResult = manager.ListResourceTemplates();

    EXPECT_EQ(resourcesResult.resources.size(), 1);
    EXPECT_EQ(templatesResult.resourceTemplates.size(), CONTENT_SIZE);

    // Remove a template should not affect resources
    manager.RemoveResourceTemplate(template1.uriTemplate);

    resourcesResult = manager.ListResources();
    templatesResult = manager.ListResourceTemplates();

    EXPECT_EQ(resourcesResult.resources.size(), 1);
    EXPECT_EQ(templatesResult.resourceTemplates.size(), 1);
}

} // namespace Mcp