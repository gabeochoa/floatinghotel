#pragma once

#include <string>

namespace ecs { struct ReviewComponent; }

// Durable per-repo persistence for ballroom review state (comments, approvals,
// folds, and the "new since you last looked" baseline). Mirrors the JSON +
// afterhours::files pattern used by Settings. Keyed by repo path; missing file
// = fresh review. Callers must gate these off in test mode.
namespace review_store {

// Path to the review JSON for a repo (under afterhours save dir / reviews/).
std::string review_path(const std::string& repoPath);

// Durable path for the exported review markdown (the AI-feedback prompt), so it
// survives a reboot / failed network round-trip instead of living in /tmp.
std::string markdown_path(const std::string& repoPath, const std::string& branch);

// Serialize the durable fields of `review` to disk (atomic replace).
void save_review(const std::string& repoPath, const ecs::ReviewComponent& review);

// Hydrate the durable fields of `review` from disk. No-op if no file exists.
void load_review(const std::string& repoPath, ecs::ReviewComponent& review);

}  // namespace review_store
