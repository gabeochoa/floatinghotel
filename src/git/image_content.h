#pragma once

#include <atomic>
#include <memory>
#include <stop_token>
#include <string>

namespace image_content {

inline constexpr size_t encodedLimit = 16 * 1024 * 1024;
inline constexpr size_t decodedLimit = 64 * 1024 * 1024;
inline constexpr size_t totalDecodedLimit = 128 * 1024 * 1024;
inline std::atomic<size_t> decodedBytes{0};

struct FreePixels {
    size_t bytes = 0;
    void operator()(unsigned char* pixels) const;
};

struct Decoded {
    std::unique_ptr<unsigned char, FreePixels> pixels;
    int width = 0;
    int height = 0;
    std::string error;
};

Decoded decode(const std::string& bytes, std::stop_token stop = {});
Decoded read(const std::string& repo, const std::string& path,
             const std::string& revision, std::stop_token stop = {});

}
