#pragma once

#include <string>
#include <string_view>

enum class ReviewVerdict { InProgress, Approved, ChangesRequested, Commented };

struct ReviewDecision {
    ReviewVerdict verdict = ReviewVerdict::InProgress;
    std::string signature;
};

inline std::string review_verdict_label(ReviewVerdict verdict) {
    switch (verdict) {
        case ReviewVerdict::InProgress: return "In progress";
        case ReviewVerdict::Approved: return "Approved";
        case ReviewVerdict::ChangesRequested: return "Changes requested";
        case ReviewVerdict::Commented: return "Commented";
    }
    return "In progress";
}

inline ReviewVerdict parse_review_verdict(std::string_view label) {
    for (auto verdict : {ReviewVerdict::InProgress, ReviewVerdict::Approved, ReviewVerdict::ChangesRequested, ReviewVerdict::Commented})
        if (review_verdict_label(verdict) == label) return verdict;
    return ReviewVerdict::InProgress;
}
