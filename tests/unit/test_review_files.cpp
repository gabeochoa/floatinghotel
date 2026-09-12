#include "test_framework.h"
#include "../../src/util/review_files.h"

TEST(path_filters_are_independent_and_default_to_visible) {
    review_files::Filter filter;
    ASSERT_TRUE(review_files::matches(filter, "vendor/lib.cpp"));
    filter.hideVendor = true;
    ASSERT_FALSE(review_files::matches(filter, "src/vendor/lib.cpp"));
    ASSERT_TRUE(review_files::matches(filter, "src/vendor_adapter.cpp"));
    ASSERT_TRUE(review_files::matches(filter, "generated/api.cpp"));
    filter.hideGenerated = true;
    ASSERT_FALSE(review_files::matches(filter, "generated/output.txt"));
    ASSERT_FALSE(review_files::matches(filter, "generated/api.cpp"));
    ASSERT_FALSE(review_files::matches(filter, "src/api.generated.ts"));
    ASSERT_TRUE(review_files::matches(filter, "Cargo.lock"));
    filter.hideLockfiles = true;
    ASSERT_FALSE(review_files::matches(filter, "Cargo.lock"));
    ASSERT_FALSE(review_files::matches(filter, "packages/app/package-lock.json"));
    ASSERT_TRUE(review_files::matches(filter, "src/lock_manager.cpp"));
}

int main() { RUN_ALL_TESTS(); }
