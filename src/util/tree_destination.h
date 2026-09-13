#pragma once

#include "reading_workspace.h"

namespace file_tree {

enum class Scope { Review, Working, Index };

inline std::string destination_path(const reading::ReadingWorkspace& workspace, Scope scope) {
    const auto& location = workspace.location();
    if (const auto* source = std::get_if<reading::SourceLocation>(&location)) {
        if (scope == Scope::Working) return std::holds_alternative<reading::WorkingTree>(source->destination.revision) ? source->destination.path : "";
        if (scope == Scope::Index) return std::holds_alternative<reading::Index>(source->destination.revision) ? source->destination.path : "";
        if (const auto* origin = workspace.retained_review(); origin && origin->files) {
            for (const auto& file : *origin->files) if (file.path == source->destination.path) return file.path;
            for (const auto& file : *origin->files) if (file.oldPath == source->destination.path) return file.path;
        }
        return source->destination.path;
    }
    const auto& review = std::get<reading::ReviewLocation>(location);
    if (scope == Scope::Review) return review.file;
    const auto* working = std::get_if<reading::WorkingChanges>(&review.destination);
    return working && working->staged == (scope == Scope::Index) ? review.file : "";
}

}
