#pragma once

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include "../ecs/ui_imports.h"
#include "../git/git_runner.h"
#include "../util/diff_revisions.h"
#include "../util/image_view_state.h"

namespace ui::image_diff {

struct Preview {
    afterhours::texture_manager::Texture texture{};
    std::string status;
};
struct Cache {
    std::string context;
    std::map<std::string, std::array<Preview, 2>> files;
    size_t bytes = 0;
    image_view_state::Mode mode = image_view_state::Mode::SideBySide;
    float zoom = 1.f;
};
inline Cache& cache() { static Cache value; return value; }

inline void clear() {
    for (auto& [path, pair] : cache().files)
        for (auto& preview : pair)
            if (preview.texture.img_id) afterhours::unload_texture(preview.texture);
    cache().files.clear();
    cache().context.clear();
    cache().bytes = 0;
    cache().mode = image_view_state::Mode::SideBySide;
    cache().zoom = 1.f;
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

inline float scale_for(const afterhours::texture_manager::Texture& texture, float width, float height, float zoom) {
    if (!texture.img_id || texture.width <= 0 || texture.height <= 0 || width <= 0.f || height <= 0.f) return 1.f;
    return std::min({1.f, width / texture.width, height / texture.height}) * zoom;
}

inline void render_one(UIContext<InputAction>& ctx, Entity& parent, int id,
                       const Preview& preview, const char* caption, float width, float height, float zoom,
                       const char* debugName) {
    auto column = div(ctx, mk(parent, id), ComponentConfig{}
        .with_size(ComponentSize{pixels(width), pixels(height)})
        .with_flex_direction(FlexDirection::Column).with_align_items(AlignItems::Center)
        .with_overflow(Overflow::Hidden)
        .with_debug_name(debugName));
    div(ctx, mk(column.ent(), 0), ComponentConfig{}
        .with_label(std::string(caption) + ": " + preview.status)
        .with_size(ComponentSize{percent(1.f), h720(28)})
        .with_font_size(FontSize::Small).with_debug_name("image_caption"));
    if (preview.texture.img_id) {
        auto canvas = div(ctx, mk(column.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(height - 34.f)})
            .with_overflow(Overflow::Hidden));
        float scale = scale_for(preview.texture, width - 16.f, height - 36.f, zoom);
        afterhours::ui::imm::image(ctx, mk(canvas.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{pixels(preview.texture.width * scale), pixels(preview.texture.height * scale)})
            .with_texture(preview.texture, afterhours::texture_manager::HasTexture::Alignment::Center)
            .with_absolute_position((width - preview.texture.width * scale) * 0.5f,
                (height - 34.f - preview.texture.height * scale) * 0.5f)
            .with_debug_name(std::string(debugName) + "_pixels"));
    }
}

inline bool render(UIContext<InputAction>& ctx, Entity& parent, int id,
                    const ecs::FileDiff& file, const std::string& repo,
                    const std::string& scope, float width) {
    std::string ext = std::filesystem::path(file.filePath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".gif" && ext != ".bmp" && ext != ".tga") return false;
    auto& files = cache().files;
    if (!files.contains(file.filePath)) {
        auto [before, after] = diff_revisions(scope);
        files[file.filePath] = {
            load(repo, file.oldPath.empty() ? file.filePath : file.oldPath, before, file.isNew || file.isFullContent),
            load(repo, file.filePath, after, file.isDeleted)};
    }
    auto root = div(ctx, mk(parent, id), ComponentConfig{}
        .with_size(ComponentSize{pixels(width), h720(300)})
        .with_flex_direction(FlexDirection::Column).with_debug_name("image_diff"));
    auto& c = cache();
    auto controls = div(ctx, mk(root.ent(), 0), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), h720(34)})
        .with_flex_direction(FlexDirection::Row).with_gap(pixels(6))
        .with_debug_name("image_controls"));
    if (button(ctx, mk(controls.ent(), 0), preset::Button("Side")
            .with_size(ComponentSize{pixels(70), pixels(28)}).with_debug_name("image_mode_side")))
        c.mode = image_view_state::Mode::SideBySide;
    if (button(ctx, mk(controls.ent(), 1), preset::Button("Overlay")
            .with_size(ComponentSize{pixels(85), pixels(28)}).with_debug_name("image_mode_overlay")))
        c.mode = image_view_state::Mode::Overlay;
    if (button(ctx, mk(controls.ent(), 2), preset::Button("Wipe")
            .with_size(ComponentSize{pixels(70), pixels(28)}).with_debug_name("image_mode_wipe")))
        c.mode = image_view_state::Mode::Wipe;
    if (button(ctx, mk(controls.ent(), 3), preset::Button("-")
            .with_size(ComponentSize{pixels(36), pixels(28)}).with_debug_name("image_zoom_out")))
        c.zoom = image_view_state::zoom_out(c.zoom);
    div(ctx, mk(controls.ent(), 4), ComponentConfig{}
        .with_label(image_view_state::label(c.mode) + " · " + image_view_state::zoom_label(c.zoom))
        .with_size(ComponentSize{pixels(170), pixels(28)})
        .with_font_size(FontSize::Small).with_debug_name("image_mode_label"));
    if (button(ctx, mk(controls.ent(), 5), preset::Button("+")
            .with_size(ComponentSize{pixels(36), pixels(28)}).with_debug_name("image_zoom_in")))
        c.zoom = image_view_state::zoom_in(c.zoom);
    const auto& pair = files.at(file.filePath);
    float bodyHeight = resolve_to_pixels(h720(266), static_cast<float>(afterhours::graphics::get_screen_height()));
    if (c.mode == image_view_state::Mode::SideBySide) {
        auto row = div(ctx, mk(root.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(bodyHeight)})
            .with_flex_direction(FlexDirection::Row).with_debug_name("image_side_by_side"));
        render_one(ctx, row.ent(), 0, pair[0], "Before", width * 0.5f, bodyHeight, c.zoom, "image_before");
        render_one(ctx, row.ent(), 1, pair[1], "After", width * 0.5f, bodyHeight, c.zoom, "image_after");
    } else {
        bool overlay = c.mode == image_view_state::Mode::Overlay;
        auto panel = div(ctx, mk(root.ent(), 2), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(bodyHeight)})
            .with_flex_direction(FlexDirection::Column)
            .with_overflow(Overflow::Hidden)
            .with_debug_name(overlay ? "image_overlay" : "image_wipe"));
        div(ctx, mk(panel.ent(), 0), ComponentConfig{}
            .with_label(overlay ? "Overlay 50% · Before: " + pair[0].status + " · After: " + pair[1].status : "Wipe 50% · Before left / After right")
            .with_size(ComponentSize{percent(1.f), h720(28)})
            .with_font_size(FontSize::Small).with_debug_name(overlay ? "image_caption" : "image_wipe_label"));
        auto canvas = div(ctx, mk(panel.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(bodyHeight - 34.f)})
            .with_overflow(Overflow::Hidden).with_debug_name("image_compare_canvas"));
        float canvasWidth = static_cast<float>(std::max(pair[0].texture.width, pair[1].texture.width));
        float canvasHeight = static_cast<float>(std::max(pair[0].texture.height, pair[1].texture.height));
        auto placement = image_view_state::place(canvasWidth, canvasHeight, width - 16.f, bodyHeight - 36.f, c.zoom);
        for (size_t side = 0; side < 2; ++side) {
            const auto& preview = pair[side];
            if (!preview.texture.img_id) continue;
            auto crop = overlay ? image_view_state::Crop{0.f, static_cast<float>(preview.texture.width)} :
                image_view_state::wipe_crop(static_cast<float>(preview.texture.width), canvasWidth, side == 1);
            if (crop.width <= 0.f) continue;
            afterhours::texture_manager::Rectangle source{crop.x, 0.f, crop.width, static_cast<float>(preview.texture.height)};
            afterhours::ui::imm::sprite(ctx, mk(canvas.ent(), static_cast<int>(side)), preview.texture, source, ComponentConfig{}
                .with_size(ComponentSize{pixels(crop.width * placement.scale), pixels(preview.texture.height * placement.scale)})
                .with_absolute_position(8.f + placement.x + crop.x * placement.scale, 1.f + placement.y)
                .with_opacity(overlay && side == 1 && pair[0].texture.img_id ? 0.5f : 1.f)
                .with_debug_name(overlay ? (side == 0 ? "before_overlay_pixels" : "after_overlay_pixels") :
                    (side == 0 ? "before_wipe_pixels" : "after_wipe_pixels")));
        }
    }
    return true;
}

}
