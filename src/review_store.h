#pragma once

#include <string>

#include "util/async_task.h"

namespace ecs { struct ReviewComponent; }

// Durable per-repo persistence for ballroom review state (comments, approvals,
// folds, and the "new since you last looked" baseline). Mirrors the JSON +
// afterhours::files pattern used by Settings. Keyed by repo path; missing file
// = fresh review. Callers must gate these off in test mode.
namespace review_store {

std::string copy_repository_reviews(const std::string& oldPath, const std::string& newPath);

// Path to the review JSON for a repo (under afterhours save dir / reviews/).
std::string review_path(const std::string& repoPath, const std::string& scope = "");

// Durable path for the exported review markdown (the AI-feedback prompt), so it
// survives a reboot / failed network round-trip instead of living in /tmp.
std::string markdown_path(const std::string& repoPath, const std::string& branch);

// Serialize the durable fields of `review` to disk (atomic replace).
bool save_review(const std::string& repoPath, const ecs::ReviewComponent& review);
bool persist_review(const std::string& repoPath, ecs::ReviewComponent& review);
bool switch_review_scope(const std::string& repoPath, const std::string& scope,
                         ecs::ReviewComponent& review, bool persist = true);

// Hydrate the durable fields of `review` from disk. No-op if no file exists.
// Drives ReviewStorePhase: Loading -> Ready on success/missing file,
// Loading -> LoadFailed on an unreadable or mismatched file (in-memory
// state is preserved and saves stay blocked in that phase).
void load_review(const std::string& repoPath, ecs::ReviewComponent& review);

// Recovery transition out of LoadFailed: re-run the load for the review's
// stored repo/scope. Returns true once the store is no longer LoadFailed;
// preserved in-memory work that was blocked from saving becomes Dirty
// when the file is simply gone, so persist_review can write it. A still-
// unreadable/mismatched file stays LoadFailed and keeps blocking saves.
bool retry_load(ecs::ReviewComponent& review);
bool retry_load(const std::string& repoPath, ecs::ReviewComponent& review);

// Parse the review file on a worker; apply with apply_loaded_review once
// the future lands and the scope still matches.
async_work::Task<ecs::ReviewComponent> load_review_async(std::string repoPath, std::string scope);
void apply_loaded_review(ecs::ReviewComponent& target, ecs::ReviewComponent loaded);

}  // namespace review_store
