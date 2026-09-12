#include "image_content.h"
#include "git_runner.h"
#include "../util/file_content.h"
#include <charconv>
#include <filesystem>
#include <stb/stb_image.h>

namespace image_content {

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
    if (!stbi_info_from_memory(data, static_cast<int>(bytes.size()), &out.width, &out.height, &channels) || out.width <= 0 || out.height <= 0) {
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
    auto* pixels = stbi_load_from_memory(data, static_cast<int>(bytes.size()), &out.width, &out.height, &channels, 4);
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
