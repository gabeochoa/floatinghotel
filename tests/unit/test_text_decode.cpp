#include "test_framework.h"

#include "../../src/util/text_decode.h"

TEST(text_decode_utf8) {
    auto result = text_decode::decode("hello\n", "auto");
    ASSERT_STREQ(result.text, "hello\n");
    ASSERT_STREQ(result.encoding, "UTF-8");
    ASSERT_FALSE(result.binary);
}

TEST(text_decode_utf16le_bom) {
    std::string bytes("\xff\xfeh\0i\0\n\0", 8);
    auto result = text_decode::decode(bytes, "auto");
    ASSERT_STREQ(result.text, "hi\n");
    ASSERT_STREQ(result.encoding, "UTF-16 LE");
    ASSERT_FALSE(result.binary);
}

TEST(text_decode_utf16be_override) {
    std::string bytes;
    bytes.push_back('\0');
    bytes.push_back('o');
    bytes.push_back('\0');
    bytes.push_back('k');
    auto result = text_decode::decode(bytes, "utf16be");
    ASSERT_STREQ(result.text, "ok");
    ASSERT_STREQ(result.encoding, "UTF-16 BE");
    ASSERT_FALSE(result.binary);
}

TEST(text_decode_utf8_override_marks_nul_binary) {
    std::string bytes("a\0b", 3);
    auto result = text_decode::decode(bytes, "utf8");
    ASSERT_TRUE(result.binary);
}

TEST(text_decode_utf8_invalid_sequences_are_labeled) {
    std::string bytes;
    bytes.push_back(static_cast<char>(0xff));
    bytes += "x";
    auto result = text_decode::decode(bytes, "auto");
    ASSERT_TRUE(result.malformed);
    ASSERT_STREQ(result.encoding, "UTF-8 (invalid sequences)");
    ASSERT_TRUE(result.text.find("\xef\xbf\xbd") != std::string::npos);
}

TEST(text_decode_utf16_replaces_unmatched_surrogates_and_odd_tail) {
    std::string bytes;
    bytes.push_back(static_cast<char>(0xff));
    bytes.push_back(static_cast<char>(0xfe));
    bytes.push_back('\0');
    bytes.push_back(static_cast<char>(0xd8));
    bytes.push_back('a');
    auto result = text_decode::decode(bytes, "auto");
    ASSERT_TRUE(result.malformed);
    ASSERT_STREQ(result.encoding, "UTF-16 LE (malformed)");
    ASSERT_TRUE(result.text.find("\xef\xbf\xbd") != std::string::npos);
}

TEST(text_decode_truncated_utf8_preserves_trailing_ascii) {
    auto result = text_decode::decode(std::string("\xe2") + "A", "utf8");
    ASSERT_EQ(result.text, std::string("\xef\xbf\xbd") + "A");
    ASSERT_TRUE(result.malformed);
}

TEST(text_decode_utf16_nul_remains_binary) {
    ASSERT_TRUE(text_decode::decode(std::string(16, '\0')).binary);
    for (const auto& encoding : {"auto", "utf16le", "utf16be"}) {
        ASSERT_TRUE(text_decode::decode(std::string("\xff\xfe\0\0", 4), encoding).binary);
        ASSERT_TRUE(text_decode::decode(std::string("\xfe\xff\0\0", 4), encoding).binary);
    }
}

int main() {
    printf("=== text decode tests ===\n");
    RUN_ALL_TESTS();
}
