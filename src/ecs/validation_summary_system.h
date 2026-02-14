#pragma once

#include <map>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/system.h"
#include "../../vendor/afterhours/src/logging.h"
#include "../../vendor/afterhours/src/plugins/ui/validation_config.h"

namespace ecs {

// Collects all ValidationViolation components each frame, deduplicates by
// message, and prints a summary after layout settles (not every frame).
// Also writes a JSON report file if a path is configured.
struct ValidationSummarySystem
    : afterhours::System<afterhours::ui::ValidationViolation> {

    // Wait this many frames before printing the first summary
    // (lets layout stabilize so we don't report transient violations)
    int settle_frames = 5;

    // If set, write the final report to this file path
    std::string report_path;

    // --- internal state ---
    int frame_count = 0;
    bool summary_printed = false;
    std::map<std::string, int> violation_counts;
    std::map<std::string, std::string> violation_categories;
    std::map<std::string, float> violation_severities;

    void once(const float) override {
        frame_count++;
        violation_counts.clear();
        violation_categories.clear();
        violation_severities.clear();
    }

    void for_each_with(afterhours::Entity&,
                       afterhours::ui::ValidationViolation& v,
                       float) override {
        violation_counts[v.message]++;
        violation_categories[v.message] = v.category;
        if (v.severity > violation_severities[v.message]) {
            violation_severities[v.message] = v.severity;
        }

        // After settling, print summary once on the first frame we see data
        if (frame_count >= settle_frames && !summary_printed) {
            // Defer to after all entities are processed -- use a flag
            // and check in the next once() call. For now, we accumulate.
        }
    }

    // Called via a separate system registered right after this one
    void print_if_ready() {
        if (summary_printed) return;
        if (frame_count < settle_frames) return;

        summary_printed = true;
        print_summary();
        if (!report_path.empty()) {
            write_report();
        }
    }

    void print_summary() const {
        if (violation_counts.empty()) {
            log_info("[Validation] No UI violations detected");
            return;
        }

        // Group by category
        std::map<std::string, std::vector<std::string>> by_category;
        for (auto& [msg, cat] : violation_categories) {
            by_category[cat].push_back(msg);
        }

        log_warn("=== UI Validation Summary ({} unique violations) ===",
                 violation_counts.size());

        for (auto& [category, messages] : by_category) {
            log_warn("  [{}] ({} issues)", category, messages.size());
            for (auto& msg : messages) {
                float sev = violation_severities.at(msg);
                const char* sev_label = sev >= 0.8f ? "HIGH"
                                      : sev >= 0.5f ? "MED"
                                      : "LOW";
                log_warn("    {}: {}", sev_label, msg);
            }
        }

        log_warn("=== End Validation Summary ===");
    }

    void write_report() const {
        FILE* f = fopen(report_path.c_str(), "w");
        if (!f) {
            log_warn("Could not write validation report to {}",
                     report_path);
            return;
        }

        fprintf(f, "{\n  \"total_unique\": %zu,\n  \"violations\": [\n",
                violation_counts.size());

        size_t i = 0;
        for (auto& [msg, count] : violation_counts) {
            auto& cat = violation_categories.at(msg);
            float sev = violation_severities.at(msg);
            fprintf(f,
                    "    {\"category\": \"%s\", \"severity\": %.1f, "
                    "\"count\": %d, \"message\": \"%s\"}%s\n",
                    cat.c_str(), sev, count, msg.c_str(),
                    (i + 1 < violation_counts.size()) ? "," : "");
            i++;
        }

        fprintf(f, "  ]\n}\n");
        fclose(f);
        log_info("[Validation] Report written to {}", report_path);
    }
};

// Triggers the summary print after ValidationSummarySystem has collected all
// violations for the frame. Register this immediately after ValidationSummarySystem.
struct ValidationSummaryTrigger : afterhours::System<> {
    ValidationSummarySystem* summary = nullptr;

    void once(const float) override {
        if (summary) summary->print_if_ready();
    }
};

}  // namespace ecs
