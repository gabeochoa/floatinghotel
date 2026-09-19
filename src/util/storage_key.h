#pragma once

#include <array>
#include <string>
#include <string_view>
#include <CommonCrypto/CommonDigest.h>

namespace storage {

inline std::string key(std::string_view value) {
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    CC_SHA256(value.data(), static_cast<CC_LONG>(value.size()), digest.data());
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "v1-";
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

}
