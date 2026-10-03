#pragma once

// Working-tree source editing: a document opened from the working tree can
// switch from read-only to an editable buffer. Everything else (index,
// revisions, diffs) stays read-only. The buffer lives on the Document and
// is authoritative while editMode is on; saving writes it back to disk.

#include <filesystem>
#include <fstream>
#include <sstream>

#include "../ecs/components.h"
#include "../git/content_reader.h"
#include "../util/navigation.h"
#include "focus.h"

namespace ecs {

inline bool source_edit_eligible(const RepoComponent& repo) {
    return repo.fullFileRevision().empty() && !repo.fullFilePath().empty();
}

inline bool seed_edit_buffer(RepoComponent& repo, reading::Document& document) {
    const auto full = std::filesystem::path(repo.repoPath) / repo.fullFilePath();
    std::error_code ec;
    const auto size = std::filesystem::file_size(full, ec);
    if (ec || size > 4 * 1024 * 1024) return false;
    std::ifstream in(full, std::ios::binary);
    if (!in) return false;
    std::ostringstream stream;
    stream << in.rdbuf();
    if (in.bad()) return false;
    std::string text = stream.str();
    document.editCrlf = text.find("\r\n") != std::string::npos;
    document.editTrailingNewline = text.empty() || text.back() == '\n';
    document.editLines.clear();
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            document.editLines.push_back(std::move(line));
            line.clear();
        } else line += c;
    }
    if (!line.empty() || document.editLines.empty()) document.editLines.push_back(std::move(line));
    document.editSeeded = true;
    document.editDirty = false;
    ++document.editGeneration;
    return true;
}

inline std::string edit_buffer_text(const reading::Document& document) {
    std::string text;
    const char* eol = document.editCrlf ? "\r\n" : "\n";
    for (size_t i = 0; i < document.editLines.size(); ++i) {
        if (i) text += eol;
        text += document.editLines[i];
    }
    if (document.editTrailingNewline && !document.editLines.empty()) text += eol;
    return text;
}

inline bool save_edit_buffer(RepoComponent& repo, reading::Document& document) {
    const auto full = std::filesystem::path(repo.repoPath) / repo.fullFilePath();
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << edit_buffer_text(document);
    out.flush();
    if (!out) return false;
    document.editDirty = false;
    repo.refreshRequested = true;
    return true;
}

inline void discard_edit_buffer(reading::Document& document) {
    document.editMode = false;
    document.editSeeded = false;
    document.editDirty = false;
    document.editLines.clear();
    ++document.editGeneration;
}

namespace source_edit_detail {

inline size_t caret_byte(const std::string& line, int column) {
    return reading::byte_at_column(line, std::max(1, column));
}

// Deletes the selected range (same-file selections only). Returns the
// position the caret collapses to, or nullopt when there is no selection.
inline std::optional<reading::CodePosition> delete_selection(RepoComponent& repo, reading::Document& document) {
    const auto selection = document.selection;
    if (!selection || selection->anchor == selection->head) return std::nullopt;
    if (selection->anchor.path != repo.fullFilePath() || selection->head.path != repo.fullFilePath()) return std::nullopt;
    auto from = selection->anchor, to = selection->head;
    if (std::tie(to.line, to.column) < std::tie(from.line, from.column)) std::swap(from, to);
    auto& lines = document.editLines;
    if (from.line < 1 || to.line > static_cast<int>(lines.size())) return std::nullopt;
    const size_t fromByte = caret_byte(lines[from.line - 1], from.column);
    const size_t toByte = caret_byte(lines[to.line - 1], to.column);
    if (from.line == to.line) {
        lines[from.line - 1].erase(fromByte, toByte - fromByte);
    } else {
        lines[from.line - 1] = lines[from.line - 1].substr(0, fromByte) + lines[to.line - 1].substr(toByte);
        lines.erase(lines.begin() + from.line, lines.begin() + to.line);
    }
    navigation::set_selection(repo, {});
    return from;
}

inline void mark_changed(RepoComponent& repo, reading::Document& document, reading::CodePosition caret) {
    document.editDirty = true;
    ++document.editGeneration;
    navigation::set_caret(repo, std::move(caret));
}

} // namespace source_edit_detail

// Consumes this frame's text-editing input for the active document. Called
// after the diff view has applied caret motions, so the caret read here is
// current. No-ops unless the code region owns the keyboard.
inline void apply_edit_input(UIContext<InputAction>& ctx, RepoComponent& repo, LayoutComponent& layout) {
    auto* document = navigation::active_edit_document(repo);
    if (!document || !document->editMode || !document->editSeeded) return;
    if (!ui::reader_shortcuts(ctx, repo, layout)) return;
    using namespace source_edit_detail;
    const std::string path = repo.fullFilePath();
    auto caret = document->caret.value_or(reading::CodePosition{path, reading::DiffSide::After, 1, 1});
    if (caret.path != path) caret = reading::CodePosition{path, reading::DiffSide::After, 1, 1};
    caret.line = std::clamp(caret.line, 1, std::max(1, static_cast<int>(document->editLines.size())));
    auto& lines = document->editLines;
    if (lines.empty()) lines.push_back("");

    // Save is Cmd+S: real Command only. Control belongs to vim
    // motions, so Ctrl+S must not save.
    const bool super = afterhours::input::is_key_down(afterhours::keys::LEFT_SUPER) ||
        afterhours::input::is_key_down(afterhours::keys::RIGHT_SUPER);
    const bool command = super ||
        afterhours::input::is_key_down(afterhours::keys::LEFT_CONTROL) ||
        afterhours::input::is_key_down(afterhours::keys::RIGHT_CONTROL);
    if (super && afterhours::input::is_key_pressed(afterhours::keys::S)) {
        save_edit_buffer(repo, *document);
        return;
    }
    if (command) return; // other shortcuts belong to the app, not the buffer

    auto prepare = [&]() -> bool {
        if (auto collapsed = delete_selection(repo, *document)) {
            caret = *collapsed;
            caret.line = std::clamp(caret.line, 1, std::max(1, static_cast<int>(document->editLines.size())));
            return true;
        }
        return false;
    };

    bool changed = false;
    if (afterhours::input::is_key_pressed(afterhours::keys::BACKSPACE) ||
        afterhours::input::is_key_pressed_repeat(afterhours::keys::BACKSPACE)) {
        if (!prepare()) {
            std::string& line = lines[caret.line - 1];
            const size_t byte = caret_byte(line, caret.column);
            if (byte > 0) {
                const size_t prev = reading::byte_at_column(line, std::max(1, reading::column_at_byte(line, byte) - 1));
                line.erase(prev, byte - prev);
                caret.column = reading::column_at_byte(line, prev);
                changed = true;
            } else if (caret.line > 1) {
                std::string& above = lines[caret.line - 2];
                caret.column = reading::column_at_byte(above, above.size());
                above += line;
                lines.erase(lines.begin() + caret.line - 1);
                --caret.line;
                changed = true;
            }
        } else changed = true;
    }
    if (afterhours::input::is_key_pressed(afterhours::keys::DELETE_KEY) ||
        afterhours::input::is_key_pressed_repeat(afterhours::keys::DELETE_KEY)) {
        if (!prepare()) {
            std::string& line = lines[caret.line - 1];
            const size_t byte = caret_byte(line, caret.column);
            if (byte < line.size()) {
                const size_t next = reading::byte_at_column(line, reading::column_at_byte(line, byte) + 1);
                line.erase(byte, next - byte);
                changed = true;
            } else if (caret.line < static_cast<int>(lines.size())) {
                line += lines[caret.line];
                lines.erase(lines.begin() + caret.line);
                changed = true;
            }
        } else changed = true;
    }
    if (afterhours::input::is_key_pressed(afterhours::keys::ENTER) ||
        afterhours::input::is_key_pressed_repeat(afterhours::keys::ENTER)) {
        prepare();
        std::string& line = lines[caret.line - 1];
        const size_t byte = caret_byte(line, caret.column);
        std::string tail = line.substr(byte);
        line.erase(byte);
        lines.insert(lines.begin() + caret.line, std::move(tail));
        ++caret.line;
        caret.column = 1;
        changed = true;
    }
    if (afterhours::input::is_key_pressed(afterhours::keys::TAB)) {
        prepare();
        std::string& line = lines[caret.line - 1];
        const size_t byte = caret_byte(line, caret.column);
        line.insert(byte, "    ");
        caret.column += 4;
        changed = true;
    }
    for (int cp = afterhours::input::get_char_pressed(); cp != 0; cp = afterhours::input::get_char_pressed()) {
        if (cp < 32 || cp == 127) continue;
        prepare();
        std::string& line = lines[caret.line - 1];
        const size_t byte = caret_byte(line, caret.column);
        char utf8[4];
        int count = 0;
        if (cp < 0x80) { utf8[count++] = static_cast<char>(cp); }
        else if (cp < 0x800) {
            utf8[count++] = static_cast<char>(0xc0 | (cp >> 6));
            utf8[count++] = static_cast<char>(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            utf8[count++] = static_cast<char>(0xe0 | (cp >> 12));
            utf8[count++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            utf8[count++] = static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            utf8[count++] = static_cast<char>(0xf0 | (cp >> 18));
            utf8[count++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            utf8[count++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            utf8[count++] = static_cast<char>(0x80 | (cp & 0x3f));
        }
        line.insert(byte, utf8, static_cast<size_t>(count));
        ++caret.column;
        changed = true;
    }
    if (changed) mark_changed(repo, *document, caret);
}

} // namespace ecs
