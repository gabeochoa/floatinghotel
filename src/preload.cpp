#include "preload.h"

#include <afterhours/src/plugins/files.h>
#include <afterhours/src/plugins/ui/theme.h>

#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>

#include "input_mapping.h"
#include "logging.h"
#include "rl.h"

#include <afterhours/src/core/key_codes.h>

using namespace afterhours;

Preload::Preload() {}

Preload& Preload::init(const char* /*title*/) {
    {
        SCOPED_TIMER("files::init");
        files::init("floatinghotel", "resources");
    }

    {
        SCOPED_TIMER("set_exit_key");
        afterhours::graphics::set_exit_key(0);
    }

    return *this;
}

Preload& Preload::make_singleton() {
    auto& sophie = EntityHelper::createEntity();
    {
        SCOPED_TIMER("Afterhours singleton setup");
        {
            SCOPED_TIMER("  input::add_singleton_components");
            // Input mappings for text input (T031)
            std::map<int, afterhours::input::ValidInputs> mapping;
            mapping[static_cast<int>(InputAction::TextBackspace)] = {
                afterhours::keys::BACKSPACE,
            };
            mapping[static_cast<int>(InputAction::TextDelete)] = {
                afterhours::keys::DELETE_KEY,
            };
            mapping[static_cast<int>(InputAction::TextHome)] = {
                afterhours::keys::HOME,
            };
            mapping[static_cast<int>(InputAction::TextEnd)] = {
                afterhours::keys::END,
            };
            mapping[static_cast<int>(InputAction::WidgetLeft)] = {
                afterhours::keys::LEFT,
            };
            mapping[static_cast<int>(InputAction::WidgetRight)] = {
                afterhours::keys::RIGHT,
            };
            mapping[static_cast<int>(InputAction::WidgetPress)] = {
                afterhours::keys::ENTER,
            };
            input::add_singleton_components(sophie, mapping);
        }
        {
            SCOPED_TIMER("  window_manager::add_singleton_components");
            window_manager::add_singleton_components(sophie, 200);
        }
        {
            SCOPED_TIMER("  ui::init_ui_plugin");
            ui::init_ui_plugin<InputAction>();
        }
    }

    // Load fonts
    std::string ui_font_path =
        files::get_resource_path("fonts", "Roboto-Regular.ttf").string();

    auto& fontMgr =
        EntityHelper::get_singleton_cmp_enforce<ui::FontManager>();

    {
        SCOPED_TIMER("Load fonts");
        {
            SCOPED_TIMER("  font: DEFAULT_FONT (Roboto)");
            fontMgr.load_font(ui::UIComponent::DEFAULT_FONT,
                              ui_font_path.c_str());
        }
        {
            SCOPED_TIMER("  font: SYMBOL_FONT (Roboto)");
            fontMgr.load_font(ui::UIComponent::SYMBOL_FONT,
                              ui_font_path.c_str());
        }
    }

    // Dark theme setup
    {
        SCOPED_TIMER("Theme setup");
        ui::imm::ThemeDefaults::get()
            .set_theme_color(ui::Theme::Usage::Primary,
                             afterhours::Color{0, 122, 204, 255})
            .set_theme_color(ui::Theme::Usage::Error,
                             afterhours::Color{220, 76, 71, 255})
            .set_theme_color(ui::Theme::Usage::Font,
                             afterhours::Color{204, 204, 204, 255})
            .set_theme_color(ui::Theme::Usage::DarkFont,
                             afterhours::Color{30, 30, 30, 255})
            .set_theme_color(ui::Theme::Usage::Background,
                             afterhours::Color{30, 30, 30, 255})
            .set_theme_color(ui::Theme::Usage::Surface,
                             afterhours::Color{37, 37, 38, 255})
            .set_theme_color(ui::Theme::Usage::Secondary,
                             afterhours::Color{58, 58, 58, 255})
            .set_theme_color(ui::Theme::Usage::Accent,
                             afterhours::Color{0, 122, 204, 255});

        ui::imm::UIStylingDefaults::get().set_grid_snapping(true);
    }

    return *this;
}

Preload::~Preload() {
    if (afterhours::graphics::is_window_ready()) {
        afterhours::graphics::close_window();
    }
}
