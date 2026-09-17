/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */


#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <regex>
#include <set>
#include <thread>
#include <vector>

#include "uuid.h"

namespace A2A::Shared::Test {

using namespace testing;

// UUID格式的正则表达式：8-4-4-4-12 十六进制格式
const std::string UUID_PATTERN = "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$";
const std::regex uuid_regex(UUID_PATTERN);

class UuidTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 每个测试用例前的设置
    }

    void TearDown() override
    {
        // 每个测试用例后的清理
    }

    // 验证UUID是否符合RFC 4122版本4规范
    bool ValidateUuidVersion4(const std::string& uuid)
    {
        if (!std::regex_match(uuid, uuid_regex)) {
            return false;
        }

        // 检查版本位（第15个字符应该是4）
        // UUID格式: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
        // 其中y应该是8,9,A或B
        if (uuid[14] != '4') {
            return false;
        }

        // 检查变体位（第19个字符应该是8,9,A或B）
        char variant = uuid[19];
        if (variant != '8' && variant != '9' && variant != 'a' && variant != 'b') {
            return false;
        }

        return true;
    }
};

// 测试基本UUID生成
TEST_F(UuidTest, GenerateUuid_Basic)
{
    std::string uuid = GenerateUuid();

    // 验证UUID不为空
    EXPECT_FALSE(uuid.empty());

    // 验证UUID长度（36个字符：32个十六进制数字 + 4个连字符）
    EXPECT_EQ(uuid.length(), 36);

    // 验证UUID格式
    EXPECT_TRUE(std::regex_match(uuid, uuid_regex));
}

// 测试UUID的唯一性
TEST_F(UuidTest, GenerateUuid_Uniqueness)
{
    constexpr int NUM_UUIDS = 1000;
    std::set<std::string> uuids;

    for (int i = 0; i < NUM_UUIDS; ++i) {
        std::string uuid = GenerateUuid();

        // 验证没有重复
        EXPECT_TRUE(uuids.find(uuid) == uuids.end()) <<
            "Duplicate UUID generated: " << uuid;
        uuids.insert(uuid);
    }

    EXPECT_EQ(uuids.size(), NUM_UUIDS);
}

// 测试UUID版本4规范
TEST_F(UuidTest, GenerateUuid_Version4Compliance)
{
    constexpr int NUM_TESTS = 100;

    for (int i = 0; i < NUM_TESTS; ++i) {
        std::string uuid = GenerateUuid();
        EXPECT_TRUE(ValidateUuidVersion4(uuid)) <<
            "UUID does not comply with RFC 4122 version 4: " << uuid;
    }
}

// 测试UUID格式的各个部分
TEST_F(UuidTest, GenerateUuid_FormatParts)
{
    std::string uuid = GenerateUuid();

    // 分割UUID的各个部分
    std::vector<std::string> parts;
    std::stringstream ss(uuid);
    std::string part;

    while (std::getline(ss, part, '-')) {
        parts.push_back(part);
    }

    // 验证有5个部分
    ASSERT_EQ(parts.size(), 5);

    // 验证各部分长度
    EXPECT_EQ(parts[0].length(), 8);  // 第一部分：8个字符
    EXPECT_EQ(parts[1].length(), 4);  // 第二部分：4个字符
    EXPECT_EQ(parts[2].length(), 4);  // 第三部分：4个字符
    EXPECT_EQ(parts[3].length(), 4);  // 第四部分：4个字符
    EXPECT_EQ(parts[4].length(), 12); // 第五部分：12个字符

    // 验证各部分都是有效的十六进制
    for (const auto& p : parts) {
        for (char c : p) {
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) <<
                "Invalid hex character in UUID: " << c;
        }
    }
}

// 测试UUID的版本位设置
TEST_F(UuidTest, GenerateUuid_VersionBit)
{
    std::string uuid = GenerateUuid();

    // 版本位在第15个字符（0-based索引14）应该是'4'
    EXPECT_EQ(uuid[14], '4');

    // 验证版本位所在的字节（第7-8个十六进制数字）
    std::string versionByteHex = uuid.substr(14, 2);
    int versionByte = std::stoi(versionByteHex, nullptr, 16);

    // 版本4要求该字节的高4位应该是0100（即4）
    EXPECT_EQ((versionByte & 0xF0) >> 4, 4);
}

// 测试UUID的变体位设置
TEST_F(UuidTest, GenerateUuid_VariantBit)
{
    std::string uuid = GenerateUuid();

    // 变体位在第19个字符（0-based索引19）应该是8,9,A或B
    char variantChar = uuid[19];
    EXPECT_TRUE(variantChar == '8' || variantChar == '9' ||
                variantChar == 'a' || variantChar == 'b') <<
        "Invalid variant character: " << variantChar;

    // 验证变体位所在的字节（第17-18个十六进制数字）
    std::string variantByteHex = uuid.substr(19, 2);
    int variantByte = std::stoi(variantByteHex, nullptr, 16);

    // RFC 4122变体10xxxxxx要求该字节的高2位应该是10
    EXPECT_EQ((variantByte & 0xC0) >> 6, 2); // 二进制10 = 2
}

// 测试连字符位置
TEST_F(UuidTest, GenerateUuid_HyphenPositions)
{
    std::string uuid = GenerateUuid();

    // 验证连字符在正确的位置
    EXPECT_EQ(uuid[8], '-');
    EXPECT_EQ(uuid[13], '-');
    EXPECT_EQ(uuid[18], '-');
    EXPECT_EQ(uuid[23], '-');

    // 验证其他地方没有连字符
    for (size_t i = 0; i < uuid.length(); ++i) {
        if (i != 8 && i != 13 && i != 18 && i != 23) {
            EXPECT_NE(uuid[i], '-') << "Unexpected hyphen at position " << i;
        }
    }
}

// 测试多线程环境下的UUID生成
TEST_F(UuidTest, GenerateUuid_MultiThreaded)
{
    constexpr int THREAD_COUNT = 10;
    constexpr int UUIDS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::vector<std::set<std::string>> threadUuids(THREAD_COUNT);

    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&threadUuids, i]() {
            for (int j = 0; j < UUIDS_PER_THREAD; ++j) {
                threadUuids[i].insert(GenerateUuid());
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证每个线程内部没有重复
    for (int i = 0; i < THREAD_COUNT; ++i) {
        EXPECT_EQ(threadUuids[i].size(), UUIDS_PER_THREAD) <<
            "Thread " << i << " generated duplicate UUIDs";
    }

    // 验证不同线程之间也没有重复
    std::set<std::string> allUuids;
    for (const auto& threadSet : threadUuids) {
        for (const auto& uuid : threadSet) {
            EXPECT_TRUE(allUuids.find(uuid) == allUuids.end()) <<
                "Duplicate UUID across threads: " << uuid;
            allUuids.insert(uuid);
        }
    }

    EXPECT_EQ(allUuids.size(), THREAD_COUNT * UUIDS_PER_THREAD);
}

// 测试随机数生成器的回退机制
TEST_F(UuidTest, GenerateUuid_FallbackRandomness)
{
    // 这个测试验证即使在random_device失败的情况下也能生成有效的UUID
    // 由于我们无法直接模拟random_device失败，这里只是验证函数正常工作

    std::string uuid = GenerateUuid();
    EXPECT_TRUE(ValidateUuidVersion4(uuid));
}

// 测试UUID的字节分布（更详细的统计测试）
TEST_F(UuidTest, GenerateUuid_ByteDistribution)
{
    constexpr int NUM_TESTS = 10000;
    std::array<int, 256> byteCounts = {0};

    for (int i = 0; i < NUM_TESTS; ++i) {
        std::string uuid = GenerateUuid();

        // 移除连字符并转换为字节
        std::string hexStr;
        for (char c : uuid) {
            if (c != '-') {
                hexStr += c;
            }
        }

        // 每两个十六进制字符组成一个字节
        for (size_t j = 0; j < hexStr.length(); j += 2) {
            std::string byteStr = hexStr.substr(j, 2);
            int byte = std::stoi(byteStr, nullptr, 16);
            byteCounts[byte]++;
        }
    }

    // 验证每个字节值出现的频率大致相等
    int totalBytes = NUM_TESTS * 16; // 每个UUID 16字节
    double expectedCount = static_cast<double>(totalBytes) / 256;
    double tolerance = expectedCount * 0.3; // 30%的容差

    for (int i = 0; i < 256; ++i) {
        // 跳过版本和变体位固定位置的检查（这些位置的值受RFC约束）
        bool isVersionByte = (i >= 0x40 && i <= 0x4F); // 版本4的高4位固定
        bool isVariantByte = ((i & 0xC0) == 0x80);     // 变体10xxxxxx

        if (!isVersionByte && !isVariantByte) {
            EXPECT_NEAR(byteCounts[i], expectedCount, tolerance) <<
                "Byte value 0x" << std::hex << i << std::dec <<
                " appears " << byteCounts[i] << " times, expected ~" <<
                expectedCount;
        }
    }
}

// 测试UUID的排序和比较
TEST_F(UuidTest, GenerateUuid_Comparison)
{
    std::string uuid1 = GenerateUuid();
    std::string uuid2 = GenerateUuid();

    // UUID应该可以正常比较
    EXPECT_NE(uuid1, uuid2);

    // 验证字符串比较的预期行为
    std::vector<std::string> uuids = {uuid1, uuid2};
    std::sort(uuids.begin(), uuids.end());

    // 排序后应该保持两个元素
    EXPECT_EQ(uuids.size(), 2);
    EXPECT_TRUE(uuids[0] == uuid1 || uuids[0] == uuid2);
}

// 测试UUID的生成速度（性能测试）
TEST_F(UuidTest, GenerateUuid_Performance)
{
    constexpr int NUM_TESTS = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_TESTS; ++i) {
        GenerateUuid();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 记录性能数据，但不作为硬性要求
    std::cout << "Generated " << NUM_TESTS << " UUIDs in " <<
                duration.count() << " ms" << std::endl;
    std::cout << "Average: " << duration.count() / static_cast<double>(NUM_TESTS) <<
                " ms per UUID" << std::endl;
}

// 测试UUID的十六进制字符大小写
TEST_F(UuidTest, GenerateUuid_Lowercase)
{
    std::string uuid = GenerateUuid();

    // 验证所有十六进制字符都是小写
    for (char c : uuid) {
        if (c != '-') {
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) <<
                "Found uppercase character in UUID: " << c;
        }
    }
}

// 测试边界情况：连续快速生成
TEST_F(UuidTest, GenerateUuid_RapidGeneration)
{
    constexpr int NUM_TESTS = 10000;
    std::set<std::string> uuids;

    for (int i = 0; i < NUM_TESTS; ++i) {
        uuids.insert(GenerateUuid());
    }

    // 即使快速连续生成，也不应该有重复
    EXPECT_EQ(uuids.size(), NUM_TESTS);
}

// 测试UUID的熵（信息熵）
TEST_F(UuidTest, GenerateUuid_Entropy)
{
    constexpr int NUM_TESTS = 1000;
    std::array<int, 16> nibbleCounts = {0};

    for (int i = 0; i < NUM_TESTS; ++i) {
        std::string uuid = GenerateUuid();

        // 统计每个半字节（4位）的出现次数
        for (char c : uuid) {
            if (c == '-') continue;

            int value;
            if (c >= '0' && c <= '9') {
                value = c - '0';
            } else {
                value = c - 'a' + 10;
            }
            nibbleCounts[value]++;
        }
    }

    // 计算信息熵
    int totalNibbles = NUM_TESTS * 32; // 每个UUID 32个十六进制字符
    double entropy = 0.0;

    for (int count : nibbleCounts) {
        if (count > 0) {
            double p = static_cast<double>(count) / totalNibbles;
            entropy -= p * log2(p);
        }
    }

    // 对于随机UUID，期望熵接近4（16种可能，均匀分布）
    std::cout << "Entropy of generated UUIDs: " << entropy << " bits per nibble" << std::endl;
    EXPECT_GT(entropy, 3.8); // 应该接近4
}

}