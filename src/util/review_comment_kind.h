#pragma once

#include <string>
#include <string_view>

enum class ReviewCommentKind { Comment, Question, Suggestion, Blocker, Nit };

inline std::string review_comment_kind_label(ReviewCommentKind kind) {
    switch (kind) {
        case ReviewCommentKind::Comment: return "Comment";
        case ReviewCommentKind::Question: return "Question";
        case ReviewCommentKind::Suggestion: return "Suggestion";
        case ReviewCommentKind::Blocker: return "Blocker";
        case ReviewCommentKind::Nit: return "Nit";
    }
    return "Comment";
}

inline ReviewCommentKind parse_review_comment_kind(std::string_view label) {
    for (auto kind : {ReviewCommentKind::Comment, ReviewCommentKind::Question, ReviewCommentKind::Suggestion,
            ReviewCommentKind::Blocker, ReviewCommentKind::Nit})
        if (review_comment_kind_label(kind) == label) return kind;
    return ReviewCommentKind::Comment;
}
