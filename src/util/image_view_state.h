#pragma once

#include <algorithm>
#include <string>

namespace image_view_state {

struct Placement {
    float scale;
    float x;
    float y;
};

inline Placement place(float width, float height, float availableWidth, float availableHeight, float zoom) {
    float scale = std::min({1.f, availableWidth / std::max(1.f, width), availableHeight / std::max(1.f, height)}) * zoom;
    return {scale, (availableWidth - width * scale) * 0.5f, (availableHeight - height * scale) * 0.5f};
}

struct Crop {
    float x;
    float width;
};

inline Crop wipe_crop(float imageWidth, float canvasWidth, bool after) {
    float split = std::min(imageWidth, canvasWidth * 0.5f);
    return after ? Crop{split, imageWidth - split} : Crop{0.f, split};
}

enum class Mode {
    SideBySide,
    Overlay,
    Wipe,
};

inline std::string label(Mode mode) {
    switch (mode) {
        case Mode::SideBySide: return "Side by side";
        case Mode::Overlay: return "Overlay";
        case Mode::Wipe: return "Wipe";
        default: return "Side by side";
    }
}

inline float zoom_in(float zoom) {
    return std::min(4.f, zoom * 1.25f);
}

inline float zoom_out(float zoom) {
    return std::max(0.25f, zoom / 1.25f);
}

inline std::string zoom_label(float zoom) {
    return std::to_string(static_cast<int>(zoom * 100.f + 0.5f)) + "%";
}

}
