#include "test_framework.h"

#include "../../src/util/hex_view.h"

TEST(hex_view_formats_address_hex_and_ascii) {
    std::string bytes("A\0z", 3);
    auto preview = hex_view::make(bytes, 0, 16);
    ASSERT_EQ(preview.lines.size(), static_cast<size_t>(1));
    ASSERT_TRUE(preview.lines[0].find("00000000") != std::string::npos);
    ASSERT_TRUE(preview.lines[0].find("41 00 7a") != std::string::npos);
    ASSERT_TRUE(preview.lines[0].find("|A.z|") != std::string::npos);
    ASSERT_FALSE(preview.truncated);
}

TEST(hex_view_is_bounded) {
    std::string bytes(5000, '\xff');
    auto preview = hex_view::make(bytes);
    ASSERT_EQ(preview.shown, static_cast<size_t>(4096));
    ASSERT_EQ(preview.loaded, static_cast<size_t>(5000));
    ASSERT_TRUE(preview.truncated);
    ASSERT_TRUE(hex_view::summary(preview).find("4096 of 5000 loaded bytes") != std::string::npos);
}

TEST(hex_view_uses_absolute_page_addresses_and_handles_unlimited_requests) {
    auto preview = hex_view::make("abc", 1, static_cast<size_t>(-1), 0x10000);
    ASSERT_EQ(preview.shown, static_cast<size_t>(2));
    ASSERT_TRUE(preview.lines.front().starts_with("00010001"));
    ASSERT_TRUE(hex_view::make("abc", 10).lines.empty());
}

TEST(hex_preview_visible_rows_are_bounded_at_both_ends) {
    ASSERT_EQ(hex_view::visible_rows(256, 0.f, 600.f, 20.f), (std::pair<size_t, size_t>{0, 32}));
    ASSERT_EQ(hex_view::visible_rows(256, 5000.f, 600.f, 20.f), (std::pair<size_t, size_t>{248, 256}));
    ASSERT_EQ(hex_view::visible_rows(0, 100.f, 600.f, 20.f), (std::pair<size_t, size_t>{0, 0}));
}

int main() {
    printf("=== hex view tests ===\n");
    RUN_ALL_TESTS();
}
