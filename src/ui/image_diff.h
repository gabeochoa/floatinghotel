#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include "../ecs/ui_imports.h"
#include "../git/git_runner.h"

namespace ui::image_diff {

struct Preview {
    afterhours::texture_manager::Texture texture{};
    std::string status;
};
struct Cache {
    std::string context;
    std::map<std::string, std::array<Preview, 2>> files;
    size_t bytes = 0;
};
inline Cache& cache() { static Cache value; return value; }

inline void clear() {
    for (auto& [path, pair] : cache().files)
        for (auto& preview : pair)
            if (preview.texture.img_id) afterhours::unload_texture(preview.texture);
    cache().files.clear();
    cache().context.clear();
    cache().bytes = 0;
}

inline void begin(const std::string& context) {
    if (cache().context == context) return;
    clear();
    cache().context = context;
}

inline Preview load(const std::string& repo, const std::string& path, const std::string& revision, bool absent) {
    Preview out;
    if (absent) { out.status = "Not present"; return out; }
    std::string bytes;
    if (revision.empty()) {
        std::ifstream input(std::filesystem::path(repo) / path, std::ios::binary | std::ios::ate);
        if (!input) { out.status = "Unable to read image"; return out; }
        if (input.tellg() > 16 * 1024 * 1024) { out.status = "Image exceeds 16 MB preview limit"; return out; }
        input.seekg(0);
        bytes.assign(std::istreambuf_iterator<char>(input), {});
    } else {
        auto result = git::git_run(repo, {"show", revision == "INDEX" ? ":" + path : revision + ":" + path});
        if (!result.success()) { out.status = "Unable to read image revision"; return out; }
        bytes = result.stdout_str();
    }
    if (bytes.size() > 16 * 1024 * 1024) { out.status = "Image exceeds 16 MB preview limit"; return out; }
    int width = 0, height = 0, channels = 0;
    auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    if (!stbi_info_from_memory(data, static_cast<int>(bytes.size()), &width, &height, &channels) || width <= 0 || height <= 0) {
        out.status = "Unsupported or invalid image";
        return out;
    }
    size_t decoded = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (width > 8192 || height > 8192 || decoded > 64 * 1024 * 1024 || cache().bytes + decoded > 128 * 1024 * 1024) {
        out.status = "Image exceeds preview memory limit";
        return out;
    }
    auto* pixels = stbi_load_from_memory(data, static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) { out.status = "Unable to decode image"; return out; }
    out.texture = afterhours::metal_texture_detail::load_texture_from_pixels(pixels, width, height);
    stbi_image_free(pixels);
    if (!out.texture.img_id) { out.status = "Unable to upload image"; return out; }
    cache().bytes += decoded;
    out.status = std::to_string(width) + " x " + std::to_string(height);
    return out;
}

inline bool render(UIContext<InputAction>& ctx, Entity& parent, int id,
                    const ecs::FileDiff& file, const std::string& repo,
                    const std::string& scope, float width) {
    std::string ext = std::filesystem::path(file.filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif" && ext != ".bmp" && ext != ".tga") return false;
    auto& files = cache().files;
    if (!files.contains(file.filePath)) {
        std::string before = scope == "wt" ? "INDEX" : scope == "index" ? "HEAD" : scope + "^";
        std::string after = scope == "wt" ? "" : scope == "index" ? "INDEX" : scope;
        if (scope.starts_with("file:")) after = scope.substr(5);
        files[file.filePath] = {
            load(repo, file.oldPath.empty() ? file.filePath : file.oldPath, before, file.isNew || file.isFullContent),
            load(repo, file.filePath, after, file.isDeleted)};
    }
    auto row = div(ctx, mk(parent, id), ComponentConfig{}
        .with_size(ComponentSize{pixels(width), h720(300)})
        .with_flex_direction(FlexDirection::Row).with_debug_name("image_diff"));
    for (size_t side = 0; side < 2; ++side) {
        const auto& preview = files.at(file.filePath)[side];
        auto column = div(ctx, mk(row.ent(), static_cast<int>(side)), ComponentConfig{}
            .with_size(ComponentSize{percent(0.5f), percent(1.f)})
            .with_flex_direction(FlexDirection::Column).with_align_items(AlignItems::Center)
            .with_debug_name(side == 0 ? "image_before" : "image_after"));
        div(ctx, mk(column.ent(), 0), ComponentConfig{}
            .with_label(std::string(side == 0 ? "Before: " : "After: ") + preview.status)
            .with_size(ComponentSize{percent(1.f), h720(28)})
            .with_font_size(FontSize::Small).with_debug_name("image_caption"));
        if (preview.texture.img_id) {
            float maxHeight = resolve_to_pixels(h720(260), static_cast<float>(afterhours::graphics::get_screen_height()));
            float scale = std::min({1.f, (width * 0.5f - 16.f) / preview.texture.width, maxHeight / preview.texture.height});
            afterhours::ui::imm::image(ctx, mk(column.ent(), 1), ComponentConfig{}
                .with_size(ComponentSize{pixels(preview.texture.width * scale), pixels(preview.texture.height * scale)})
                .with_texture(preview.texture, afterhours::texture_manager::HasTexture::Alignment::Center)
                .with_debug_name(side == 0 ? "before_image_pixels" : "after_image_pixels"));
        }
    }
    return true;
}

}
