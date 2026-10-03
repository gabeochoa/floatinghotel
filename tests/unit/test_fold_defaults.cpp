#include "test_framework.h"
#include "../../src/util/fold_defaults.h"

using namespace fold_defaults;

TEST(default_rules_fold_test_directories_only) {
    const auto rules = default_rules();
    ASSERT_TRUE(matches(rules, "tests/unit/test_x.cpp", 3));
    ASSERT_TRUE(matches(rules, "src/tests/helper.cpp", 3));
    ASSERT_FALSE(matches(rules, "src/main.cpp", 3));
    ASSERT_FALSE(matches(rules, "src/testing.cpp", 3));
    ASSERT_FALSE(matches(rules, "latest/results.txt", 3));
}

TEST(stored_patterns_replace_defaults) {
    const Rules rules{{"*.generated.*", "vendor/**"}, 0};
    ASSERT_TRUE(matches(rules, "api.generated.ts", 1));
    ASSERT_TRUE(matches(rules, "vendor/lib/x.cpp", 1));
    ASSERT_FALSE(matches(rules, "tests/unit/test_x.cpp", 1));
}

TEST(empty_pattern_list_folds_nothing_by_pattern) {
    const Rules rules{{}, 0};
    ASSERT_FALSE(matches(rules, "tests/unit/test_x.cpp", 10));
}

TEST(threshold_folds_long_files_regardless_of_path) {
    const Rules rules{{}, 500};
    ASSERT_TRUE(matches(rules, "src/main.cpp", 500));
    ASSERT_TRUE(matches(rules, "src/main.cpp", 900));
    ASSERT_FALSE(matches(rules, "src/main.cpp", 499));
}

TEST(parse_and_format_patterns_round_trip) {
    const auto parsed = parse_patterns(" tests/** ,*.generated.* ,, src/*.lock ");
    ASSERT_EQ(parsed.size(), size_t{3});
    ASSERT_STREQ(parsed[0], "tests/**");
    ASSERT_STREQ(parsed[1], "*.generated.*");
    ASSERT_STREQ(parsed[2], "src/*.lock");
    ASSERT_STREQ(format_patterns(parsed), "tests/**, *.generated.*, src/*.lock");
    ASSERT_TRUE(parse_patterns("").empty());
    ASSERT_TRUE(parse_patterns(" , ").empty());
}

int main() { RUN_ALL_TESTS(); }
