#include "test_framework.h"
#include "../../src/ui/bindings.h"

TEST(binding_table_has_unique_nonempty_chords_and_descriptions) {
    for (size_t i = 0; i < std::size(ui::bindings::table); ++i) {
        const auto& binding = ui::bindings::table[i];
        ASSERT_TRUE(binding.chord != nullptr && binding.chord[0] != '\0');
        ASSERT_TRUE(binding.description != nullptr && binding.description[0] != '\0');
        for (size_t j = i + 1; j < std::size(ui::bindings::table); ++j)
            ASSERT_TRUE(std::string_view(binding.chord) != ui::bindings::table[j].chord);
    }
}

TEST(binding_lookup_finds_dispatchable_and_display_only_rows) {
    const auto* palette = ui::bindings::find("Cmd+K");
    ASSERT_TRUE(palette != nullptr);
    ASSERT_TRUE(palette->key != 0 && palette->command);
    const auto* range = ui::bindings::find("Cmd+1 ... Cmd+9");
    ASSERT_TRUE(range != nullptr);
    ASSERT_EQ(range->key, 0);  // display-only: dispatched by the digit loop
    ASSERT_TRUE(ui::bindings::find("Cmd+Nope") == nullptr);
}

int main() { RUN_ALL_TESTS(); }
