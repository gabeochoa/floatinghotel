#include "test_framework.h"
#include "../../src/util/reading_session.h"
#include "../../src/util/reading_anchor.h"

TEST(session_round_trip_preserves_typed_destinations_origins_and_anchors) {
    const std::string oid(40, 'a'), parent(40, 'b');
    reading::ReadingSession session;
    for (const auto& location : std::vector<reading::Location>{reading::review("wt"), reading::review("index"),
        reading::review("parent:" + parent + ":" + oid, "renamed.cpp"), reading::review("compare:" + parent + ":" + oid),
        reading::source("日本語.cpp", oid, 22, reading::review(oid, "日本語.cpp")), reading::source("index.cpp", "INDEX")})
        session.documents.push_back({location, "Subject", 42, reading::ReadingAnchor{"日本語.cpp", reading::anchor_revision(location), reading::DiffSide::Before, 22, 7, .25f, '-'}});
    session.active = 4;
    const auto restored = reading::decode_session(reading::encode_session(session));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->active, size_t{4});
    ASSERT_EQ(restored->documents.size(), session.documents.size());
    for (size_t i = 0; i < session.documents.size(); ++i) {
        ASSERT_TRUE(restored->documents[i].location == session.documents[i].location);
        ASSERT_TRUE(restored->documents[i].anchor == session.documents[i].anchor);
        ASSERT_EQ(restored->documents[i].subject, std::string("Subject"));
        ASSERT_EQ(restored->documents[i].lastActivated, std::uint64_t{42});
    }
}

TEST(session_parser_rejects_invalid_revisions_and_retains_other_documents) {
    reading::ReadingSession session{{{reading::source("a.cpp")}, {reading::source("b.cpp", std::string(40, 'c'))}}, 1};
    auto json = reading::encode_session(session);
    json["documents"][0]["location"]["source"]["revision"] = {{"kind", "object"}, {"value", "HEAD"}};
    auto decoded = reading::decode_session(json);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->documents.size(), size_t{1});
    ASSERT_EQ(decoded->active, size_t{0});
    ASSERT_TRUE(std::holds_alternative<reading::ObjectId>(std::get<reading::SourceLocation>(decoded->documents[0].location).destination.revision));
    json["version"] = 999;
    ASSERT_FALSE(reading::decode_session(json).has_value());
    ASSERT_FALSE(reading::decode_session(nlohmann::json::array()).has_value());
}

TEST(session_parser_drops_duplicate_destinations_and_mismatched_anchor_revisions) {
    reading::SavedDocument document{reading::source("a.cpp"), "", 0, reading::ReadingAnchor{"a.cpp", "wrong", reading::DiffSide::After, 10, 1, .2f, ' '}};
    const auto restored = reading::decode_session(reading::encode_session({{document, document}, 0}));
    ASSERT_TRUE(restored.has_value());
    ASSERT_EQ(restored->documents.size(), size_t{1});
    ASSERT_FALSE(restored->documents.front().anchor.has_value());
}

TEST(reading_columns_count_decoded_characters_across_wrapped_unicode) {
    const std::string text = "a日本z";
    ASSERT_EQ(reading::column_at_byte(text, 0), 1);
    ASSERT_EQ(reading::column_at_byte(text, 4), 3);
    ASSERT_EQ(reading::column_at_byte(text, text.size()), 5);
    ASSERT_EQ(reading::byte_at_column(text, 3), size_t{4});
    ASSERT_EQ(reading::byte_at_column(text, 99), text.size());
}

int main() { RUN_ALL_TESTS(); }
