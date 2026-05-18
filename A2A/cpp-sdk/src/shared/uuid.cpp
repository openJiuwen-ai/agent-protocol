/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include <array>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <cstdint>

#include "uuid.h"

namespace A2A {

namespace {
    constexpr size_t UUID_BYTE_SIZE = 16;
    constexpr size_t UUID_VER_INDEX = 6;
    constexpr size_t UUID_VAR_INDEX = 8;
    constexpr unsigned char UUID_VERSION_MASK = 0x0F;
    constexpr unsigned char UUID_VERSION_BITS = 0x40; // version 4
    constexpr unsigned char UUID_VARIANT_MASK = 0x3F;
    constexpr unsigned char UUID_VARIANT_BITS = 0x80; // variant 10xxxxxx
    constexpr int UUID_HEX_WIDTH = 2;
    constexpr std::array<size_t, 4> UUID_HYPHEN_INDICES = {3, 5, 7, 9};
}

// Generate 16 random bytes using std facilities, set RFC 4122 version/variant bits,
// and format as lowercase hex with hyphens: 8-4-4-4-12
std::string GenerateUuid()
{
    std::array<unsigned char, UUID_BYTE_SIZE> bytes{};

    // Use non-deterministic seed if available, fallback to mt19937
    try {
        std::random_device rd;
        for (auto& b : bytes) {
            b = static_cast<unsigned char>(rd());
        }
    } catch (...) {
        // Extremely rare; fallback to PRNG
        std::mt19937_64 gen{static_cast<std::mt19937_64::result_type>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count())};
        std::uniform_int_distribution<int> dist(0, UINT8_MAX);
        for (auto& b : bytes) {
            b = static_cast<unsigned char>(dist(gen));
        }
    }

    // Set version (4) in byte 6 (0-indexed) and variant in byte 8
    bytes[UUID_VER_INDEX] = static_cast<unsigned char>((bytes[UUID_VER_INDEX] & UUID_VERSION_MASK) | UUID_VERSION_BITS);
    bytes[UUID_VAR_INDEX] = static_cast<unsigned char>((bytes[UUID_VAR_INDEX] & UUID_VARIANT_MASK) | UUID_VARIANT_BITS);

    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        oss << std::setw(UUID_HEX_WIDTH) << static_cast<int>(bytes[i]);
        for (const size_t hyphenIdx : UUID_HYPHEN_INDICES) {
            if (i == hyphenIdx) {
                oss << '-';
                break;
            }
        }
    }
    return oss.str();
}

} // namespace A2A
