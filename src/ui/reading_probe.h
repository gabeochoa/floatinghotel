#pragma once

#include <filesystem>

namespace afterhours { struct SystemManager; }

namespace reading_probe {

void input_dispatched();
void rendered();
bool checkpoint_pending();
void register_handlers(afterhours::SystemManager& manager, std::filesystem::path directory);

}
