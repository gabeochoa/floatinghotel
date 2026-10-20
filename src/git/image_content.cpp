#include "image_content.h"
#include "git_runner.h"
#include "../util/file_content.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <stb/stb_image.h>
#ifdef __APPLE__
#include <ImageIO/ImageIO.h>
#endif

namespace image_content {

#ifdef __APPLE__
// stb_image has no WebP, AVIF or ICO; ImageIO decodes all three natively, so
// they need no new dependency. Same limits as stb: the header's size is checked
// before one RGBA8 draw. An ICO previews its largest entry.
namespace {
struct Release { void operator()(CFTypeRef ref) const { if (ref) CFRelease(ref); } };
template <class T> using Owned = std::unique_ptr<std::remove_pointer_t<T>, Release>;

int number(CFDictionaryRef properties, CFStringRef key) {
    int value = 0;
    if (auto ref = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, key))) CFNumberGetValue(ref, kCFNumberIntType, &value);
    return value;
}

struct ImageIOSource { Owned<CGImageSourceRef> source; size_t index = 0; int width = 0, height = 0; };

ImageIOSource imageio_source(const std::string& bytes) {
    ImageIOSource out;
    Owned<CFDataRef> data(CFDataCreateWithBytesNoCopy(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
        static_cast<CFIndex>(bytes.size()), kCFAllocatorNull));
    if (!data) return out;
    out.source.reset(CGImageSourceCreateWithData(data.get(), nullptr));
    for (size_t i = 0; out.source && i < CGImageSourceGetCount(out.source.get()); ++i) {
        Owned<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(out.source.get(), i, nullptr));
        if (!properties) continue;
        const int width = number(properties.get(), kCGImagePropertyPixelWidth);
        const int height = number(properties.get(), kCGImagePropertyPixelHeight);
        if (static_cast<long>(width) * height > static_cast<long>(out.width) * out.height) out = {std::move(out.source), i, width, height};
    }
    return out;
}

unsigned char* imageio_pixels(const ImageIOSource& source) {
    Owned<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.source.get(), source.index, nullptr));
    Owned<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!image || !space) return nullptr;
    const size_t stride = static_cast<size_t>(source.width) * 4;
    // malloc: FreePixels releases through stbi_image_free, which is free().
    auto* pixels = static_cast<unsigned char*>(std::malloc(stride * static_cast<size_t>(source.height)));
    if (!pixels) return nullptr;
    Owned<CGContextRef> context(CGBitmapContextCreate(pixels, static_cast<size_t>(source.width), static_cast<size_t>(source.height), 8, stride,
        space.get(), static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big));
    if (!context) { std::free(pixels); return nullptr; }
    CGContextClearRect(context.get(), CGRectMake(0, 0, source.width, source.height));
    CGContextDrawImage(context.get(), CGRectMake(0, 0, source.width, source.height), image.get());
    // CoreGraphics only draws premultiplied; stb hands back straight alpha.
    for (size_t i = 0; i < stride * static_cast<size_t>(source.height); i += 4)
        if (const unsigned alpha = pixels[i + 3]; alpha && alpha < 255)
            for (size_t c = 0; c < 3; ++c) pixels[i + c] = static_cast<unsigned char>(std::min(255u, (pixels[i + c] * 255u + alpha / 2) / alpha));
    return pixels;
}
}
#endif

void FreePixels::operator()(unsigned char* pixels) const {
    stbi_image_free(pixels);
    decodedBytes.fetch_sub(bytes);
}

Decoded decode(const std::string& bytes, std::stop_token stop) {
    Decoded out;
    if (stop.stop_requested()) { out.error = "Image request cancelled"; return out; }
    if (bytes.size() > encodedLimit) { out.error = "Image exceeds 16 MB preview limit"; return out; }
    int channels = 0;
    auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const bool stb = stbi_info_from_memory(data, static_cast<int>(bytes.size()), &out.width, &out.height, &channels);
#ifdef __APPLE__
    ImageIOSource imageio;
    if (!stb) {
        imageio = imageio_source(bytes);
        out.width = imageio.width;
        out.height = imageio.height;
    }
#endif
    if (out.width <= 0 || out.height <= 0) {
        out.error = "Unsupported or invalid image";
        return out;
    }
    auto size = static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4;
    if (out.width > 8192 || out.height > 8192 || size > decodedLimit) {
        out.error = "Image exceeds preview memory limit";
        return out;
    }
    auto reserved = decodedBytes.load();
    do {
        if (reserved + size > totalDecodedLimit) { out.error = "Image decode memory limit reached"; return out; }
    } while (!decodedBytes.compare_exchange_weak(reserved, reserved + size));
    auto* pixels = stb ? stbi_load_from_memory(data, static_cast<int>(bytes.size()), &out.width, &out.height, &channels, 4) : nullptr;
#ifdef __APPLE__
    if (!stb) pixels = imageio_pixels(imageio);
#endif
    if (!pixels) {
        decodedBytes.fetch_sub(size);
        out.error = "Unable to decode image";
        return out;
    }
    out.pixels = decltype(out.pixels)(pixels, FreePixels{size});
    if (stop.stop_requested()) { out.pixels.reset(); out.error = "Image request cancelled"; }
    return out;
}

Decoded read(const std::string& repo, const std::string& path,
             const std::string& revision, std::stop_token stop) {
    Decoded out;
    if (stop.stop_requested()) { out.error = "Image request cancelled"; return out; }
    std::string bytes;
    if (revision.empty()) {
        auto source = file_content::read_working_file(std::filesystem::path(repo) / path, stop, encodedLimit);
        if (source.mode == "120000") { out.error = "Symbolic links are not image previews"; return out; }
        if (!source.error.empty()) {
            out.error = source.totalBytes > encodedLimit ? "Image exceeds 16 MB preview limit" : std::move(source.error);
            return out;
        }
        bytes = std::move(source.bytes);
    } else {
        auto resolved = git::git_run(repo, {"rev-parse", "--verify", "--end-of-options", (revision == "INDEX" ? "" : revision) + ":" + path}, stop);
        if (stop.stop_requested()) { out.error = "Image request cancelled"; return out; }
        if (!resolved.success()) { out.error = "Unable to read image revision"; return out; }
        auto blob = resolved.stdout_str();
        while (!blob.empty() && (blob.back() == '\n' || blob.back() == '\r')) blob.pop_back();
        auto measured = git::git_run(repo, {"cat-file", "-s", blob}, stop);
        if (stop.stop_requested()) { out.error = "Image request cancelled"; return out; }
        auto sizeText = measured.stdout_str();
        size_t size = 0;
        auto parsed = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), size);
        if (!measured.success() || parsed.ec != std::errc{}) { out.error = "Unable to measure image revision"; return out; }
        if (size > encodedLimit) { out.error = "Image exceeds 16 MB preview limit"; return out; }
        auto content = git::git_run(repo, {"cat-file", "blob", blob}, stop);
        if (stop.stop_requested()) { out.error = "Image request cancelled"; return out; }
        if (!content.success()) { out.error = "Unable to read image revision"; return out; }
        bytes = std::move(content.raw.stdout_str);
    }
    return decode(bytes, stop);
}

}
