#include "test_framework.h"
#include "../../src/util/history_selection.h"

using reading::HistorySelection;

static const std::vector<std::string> kVisible = {"a", "b", "c", "d"};

TEST(plain_click_selects_exactly_one) {
    HistorySelection selection;
    selection.select(kVisible, "b", false, false);
    ASSERT_EQ(selection.hashes.size(), size_t{1});
    ASSERT_TRUE(selection.hashes.contains("b"));
}

TEST(plain_click_after_range_collapses_to_one) {
    HistorySelection selection;
    selection.select(kVisible, "a", false, false);
    selection.select(kVisible, "c", true, false);
    ASSERT_EQ(selection.hashes.size(), size_t{3});
    selection.select(kVisible, "d", false, false);
    ASSERT_EQ(selection.hashes.size(), size_t{1});
    ASSERT_TRUE(selection.hashes.contains("d"));
}

TEST(shift_click_extends_range_from_anchor) {
    HistorySelection selection;
    selection.select(kVisible, "b", false, false);
    selection.select(kVisible, "d", true, false);
    ASSERT_EQ(selection.hashes.size(), size_t{3});
    ASSERT_TRUE(selection.hashes.contains("b"));
    ASSERT_TRUE(selection.hashes.contains("c"));
    ASSERT_TRUE(selection.hashes.contains("d"));
}

TEST(cmd_click_toggles_and_plain_click_recovers) {
    HistorySelection selection;
    selection.select(kVisible, "a", false, false);
    selection.select(kVisible, "c", false, true);
    ASSERT_EQ(selection.hashes.size(), size_t{2});
    selection.select(kVisible, "c", false, true);
    ASSERT_EQ(selection.hashes.size(), size_t{1});
    selection.select(kVisible, "b", false, false);
    ASSERT_EQ(selection.hashes.size(), size_t{1});
    ASSERT_TRUE(selection.hashes.contains("b"));
}

int main() { RUN_ALL_TESTS(); }
