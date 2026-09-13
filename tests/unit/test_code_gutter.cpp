#include "test_framework.h"
#include "../../src/util/code_gutter.h"

TEST(full_content_has_one_number_and_no_diff_sign_column) {
    ASSERT_EQ(code_gutter::prefix("12", "12", ' ', true), "     12  ");
    ASSERT_EQ(code_gutter::prefix("123456", "123456", ' ', true), "  123456  ");
}

TEST(diff_prefix_preserves_both_coordinates_and_signs) {
    ASSERT_EQ(code_gutter::prefix("12", "13", ' ', false), "   12    13    ");
    ASSERT_EQ(code_gutter::prefix("", "1", '+', false), "          1  + ");
    ASSERT_EQ(code_gutter::prefix("123456", "", '-', false).size(), 16u);
}

int main() { RUN_ALL_TESTS(); }
