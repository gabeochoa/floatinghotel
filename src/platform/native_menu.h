#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace native_menu {

enum class CommandId : std::uint32_t {};

struct Invocation {
    CommandId command{};
    std::string owner{};
    bool operator==(const Invocation&) const = default;
};

struct Item {
    CommandId command{};
    std::string title;
    std::string shortcut;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
    std::string owner{};
    bool operator==(const Item&) const = default;
};

struct Menu {
    std::string title;
    std::vector<Item> items;
    bool operator==(const Menu&) const = default;
};

#if defined(__APPLE__)
bool install(const std::string& app_title, CommandId quit_command,
             const std::vector<Menu>& menus);
void refresh(const std::vector<Menu>& menus);
std::vector<Invocation> drain_commands();
void shutdown();
bool is_installed();
void prepare_windowless();
bool activate_for_test(const std::string& title);
#else
inline bool install(const std::string&, CommandId, const std::vector<Menu>&) { return false; }
inline void refresh(const std::vector<Menu>&) {}
inline std::vector<Invocation> drain_commands() { return {}; }
inline void shutdown() {}
inline bool is_installed() { return false; }
inline void prepare_windowless() {}
inline bool activate_for_test(const std::string&) { return false; }
#endif

}
