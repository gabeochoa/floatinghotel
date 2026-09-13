#pragma once

#include "../util/reading_anchor.h"
#include "geometry.h"

#include "review_comment_kind.h"

#include "../ecs/ui_imports.h"
#include "../git/git_commands.h"
#include "../settings.h"
#include "code_highlight.h"
#include "token_cache.h"
#include "diff_metrics.h"
#include "context_menu.h"
#include "image_diff.h"
#include "reading_position.h"
#include "zoom.h"
#include "text_area.h"
#include "chrome_icons.h"
#include "file_tree_style.h"
#include "../util/review_selection.h"
#include "../util/code_gutter.h"
#include "../util/lfs_pointer.h"
#include <afterhours/src/core/text_cache.h>
#include <afterhours/src/plugins/clipboard.h>
#include <afterhours/src/plugins/toast.h>
#include <afterhours/src/plugins/ui/text_input/text_input.h>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace ui {

inline void begin_diff_comment(ecs::ReviewComponent& review, const std::string& key,
    ecs::ReviewComponent::Comment location, const ecs::DiffHunk& hunk) {
    auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
    ecs::begin_comment(review, key, ecs::comment_with_context(std::move(location), hunk,
        repo ? repo->headCommitHash : ""));
}

inline std::vector<afterhours::ui::TextSpan> highlighted_code(
    const std::string& prefix, const std::string& content, const std::string& path,
    bool visibleWhitespace = false, bool hasNewline = true,
    const std::string* original = nullptr, size_t offset = 0, bool finalFragment = true, const std::string& ending = "") {
    std::vector<afterhours::ui::TextSpan> spans{{prefix, theme::TEXT_SECONDARY}};
    const auto& source = original ? *original : content;
    auto tokens = code_highlight::token_cache().get(code_highlight::display_text(source, visibleWhitespace), path);
    size_t begin = code_highlight::display_text(std::string_view(source).substr(0, offset), visibleWhitespace).size();
    size_t end = begin + code_highlight::display_text(content, visibleWhitespace).size();
    size_t position = 0;
    for (const auto& token : *tokens) {
        auto color = theme::TEXT_PRIMARY;
        switch (token.kind) {
            case code_highlight::Kind::Plain: break;
            case code_highlight::Kind::Keyword: color = {194, 168, 217, 255}; break;
            case code_highlight::Kind::String: color = {183, 205, 159, 255}; break;
            case code_highlight::Kind::Number: color = {215, 185, 145, 255}; break;
            case code_highlight::Kind::Comment: color = {117, 129, 142, 255}; break;
        }
        auto first = std::clamp(begin, position, position + token.text.size()) - position;
        auto last = std::clamp(end, position, position + token.text.size()) - position;
        if (last > first) spans.push_back({token.text.substr(first, last - first), color});
        position += token.text.size();
    }
    if (visibleWhitespace && finalFragment) spans.push_back({ending.empty() ? code_highlight::display_text(source.ends_with('\r') ? "\r" : "", true, true, hasNewline) : ending, theme::TEXT_SECONDARY});
    return spans;
}

// ============================================================================
// Diff text selection (drag to select code, copy with file:line for AI review)
// ============================================================================
namespace diff_sel {

struct Pos {
    afterhours::EntityID ent = 0;
    int col = 0;
    bool operator==(const Pos& o) const { return ent == o.ent && col == o.col; }
};

// One selectable line, rebuilt each frame. rect/contentX0 come from the prior
// frame's layout (this system runs before autolayout), which is stable while
// the diff is just being viewed.
struct Rec {
    afterhours::EntityID ent = 0;
    std::string content;   // the code text (no gutter/sign)
    std::string filePath;
    int lineNo = 0;        // display line number, for the copy location header
    Rectangle rect{};
    float contentX0 = 0.f; // screen x where content[0] starts
    int side = 0;
    char sign = ' ';
    int oldLine = 0;
    int newLine = 0;
    size_t sourceOffset = 0;
    int logicalColumn = 1;
};

struct State {
    bool dragging = false;
    bool hasSel = false;
    std::string context;
    Pos anchor, head;
    std::vector<Rec> lastLines; // prior frame (used for hit-test + copy)
    std::vector<Rec> curLines;  // being built this frame
    std::unordered_map<afterhours::EntityID, std::pair<int, int>> hl; // ent -> [a,b)
};

inline State& state() {
    static State s;
    return s;
}

// Clear any active text selection. Used when resetting to a fresh repo (e.g.
// make_test_repo) so a stale selection can't bleed into the next context.
inline void reset() {
    State& s = state();
    s.dragging = false;
    s.hasSel = false;
    s.anchor = {};
    s.head = {};
    s.lastLines.clear();
    s.curLines.clear();
    s.hl.clear();
}

struct Session {
    bool visibleWhitespace = false;
    std::optional<ecs::DiffMatch> findMatch;
    std::string findQuery;
    bool findNavigate = false;
    bool enabled = false;
    afterhours::ui::TextMeasureCache* tmc = nullptr;
    float fontSize = 0.f;
    bool reviewActions = false;
    // Embedded (commit-detail) diffs are read-only: no keyboard review cursor.
    bool embedded = false;
    std::string repoPath;
    std::string reviewScope = "wt";  // "wt" (working tree) or a commit SHA
    ecs::ReviewComponent* review = nullptr;
    int hunkOrdinal = 0;  // running index of visible hunks (for the cursor)
};

inline float mw(const Session& s, const std::string& t) {
    return s.tmc ? s.tmc->measure_width(t, "mono", s.fontSize) : 0.f;
}

inline float content_x_offset(const Session& s, const std::string& gutter) {
    constexpr float afterhoursTextInsetPx = 5.f;
    return afterhoursTextInsetPx + mw(s, std::string(gutter.size(), ' '));
}

inline float code_mw(const Session& s, const std::string& raw) {
    return mw(s, code_highlight::display_text(raw, s.visibleWhitespace));
}

inline std::string ending_label(const Session& session, const std::string& text, bool newline, float available) {
    if (!session.visibleWhitespace) return "";
    auto label = code_highlight::display_text(text.ends_with('\r') ? "\r" : "", true, true, newline);
    if (mw(session, label) > available) label = newline ? text.ends_with('\r') ? " CRLF" : " LF" : " EOF";
    if (mw(session, label) > available) label = "*";
    return label;
}

inline std::vector<size_t> wrapped_rows(const Session& session, const std::string& text, float available, bool newline) {
    auto breaks = diff_metrics().wraps(text, available, session.fontSize, session.visibleWhitespace,
        [&](std::string_view glyph) { return code_mw(session, std::string(glyph)); });
    if (session.visibleWhitespace && !text.empty() &&
        code_mw(session, text.substr(breaks[breaks.size() - 2])) + mw(session, ending_label(session, text, newline, available)) > available)
        breaks.push_back(text.size());
    return breaks;
}

inline bool found_line(const Session* s, const std::string& file, int line, char sign) {
    return s && s->findMatch && s->findMatch->file == file &&
           s->findMatch->line == line && s->findMatch->sign == sign;
}

inline void render_changed_range(UIContext<InputAction>& ctx, Entity& entity,
                                  const Session& session, const std::string& content,
                                  float prefix, code_highlight::Range range, bool deletion) {
    if (range.second <= range.first || range.second > content.size()) return;
    float x = prefix + code_mw(session, content.substr(0, range.first));
    float width = code_mw(session, content.substr(range.first, range.second - range.first));
    div(ctx, mk(entity, 90003), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{pixels(width / zoom::get()), percent(1.f)})
        .with_absolute_position(x / zoom::get(), 0.f)
        .with_custom_background(deletion ? afterhours::Color{240, 100, 100, 90}
                                         : afterhours::Color{90, 230, 140, 80})
        .with_roundness(0.f)
        .with_debug_name("intraline_change"));
}

inline void render_find_match(UIContext<InputAction>& ctx, Entity& lineEntity,
                              Session& s, const std::string& content, float prefix, size_t sourceOffset = 0) {
    auto range = code_wrap::intersect({s.findMatch->column, s.findMatch->column + s.findQuery.size()}, sourceOffset, sourceOffset + content.size());
    if (range.first == range.second) return;
    size_t at = range.first;
    float x = prefix + code_mw(s, content.substr(0, at));
    float width = code_mw(s, content.substr(at, range.second - range.first));
    div(ctx, mk(lineEntity, 90002), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{pixels(width / zoom::get()), percent(1.f)})
        .with_absolute_position(x / zoom::get(), 0.f)
        .with_custom_background(afterhours::Color{230, 180, 30, 100})
        .with_roundness(0.f)
        .with_debug_name("diff_find_match"));
}

// Resolve anchor/head into an ordered span (i1,c1) <= (i2,c2) as indices into
// `lines`. Returns false if either endpoint's line is no longer present.
inline bool ordered_span(const std::vector<Rec>& lines, Pos anchor, Pos head,
                         int& i1, int& c1, int& i2, int& c2) {
    int ai = -1, hi = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (lines[i].ent == anchor.ent) ai = i;
        if (lines[i].ent == head.ent) hi = i;
    }
    if (ai < 0 || hi < 0 || lines[ai].side != lines[hi].side ||
        lines[ai].filePath != lines[hi].filePath) return false;
    i1 = ai; c1 = anchor.col; i2 = hi; c2 = head.col;
    if (i1 > i2 || (i1 == i2 && c1 > c2)) { std::swap(i1, i2); std::swap(c1, c2); }
    return true;
}

inline void recompute_highlight(State& st) {
    st.hl.clear();
    if (!st.hasSel) return;
    int i1, c1, i2, c2;
    if (!ordered_span(st.lastLines, st.anchor, st.head, i1, c1, i2, c2)) return;
    for (int k = i1; k <= i2; ++k) {
        if (st.lastLines[k].side != st.lastLines[i1].side ||
            st.lastLines[k].filePath != st.lastLines[i1].filePath) continue;
        int a = (k == i1) ? c1 : 0;
        int b = (k == i2) ? c2 : (int)st.lastLines[k].content.size();
        if (k == i1 && k == i2 && a == b) continue;
        st.hl[st.lastLines[k].ent] = {a, b};
    }
}

// Build the clipboard text for the current selection. Optionally prepends a
// "path:Lstart[-Lend]" location so it's ready to paste into an AI review chat.
inline std::string build_copy_text(State& st, bool withLocation) {
    int i1, c1, i2, c2;
    if (!ordered_span(st.lastLines, st.anchor, st.head, i1, c1, i2, c2)) return "";
    std::string out;
    if (withLocation) {
        const Rec& r = st.lastLines[i1];
        int endNo = st.lastLines[i2].lineNo;
        out += r.filePath + ":L" + std::to_string(r.lineNo);
        if (endNo != r.lineNo) out += "-" + std::to_string(endNo);
        out += "\n";
    }
    bool emitted = false;
    int previous = i1;
    for (int k = i1; k <= i2; ++k) {
        if (st.lastLines[k].side != st.lastLines[i1].side ||
            st.lastLines[k].filePath != st.lastLines[i1].filePath) continue;
        const std::string& c = st.lastLines[k].content;
        int a = std::min((k == i1) ? c1 : 0, (int)c.size());
        int b = std::min((k == i2) ? c2 : (int)c.size(), (int)c.size());
        if (emitted && (st.lastLines[k].sourceOffset == 0 ||
            st.lastLines[k].lineNo != st.lastLines[previous].lineNo || st.lastLines[k].sign != st.lastLines[previous].sign)) out += "\n";
        out += c.substr(a, b - a);
        emitted = true;
        previous = k;
    }
    return out;
}

// Update the selection from this frame's mouse against the prior frame's lines.
inline void handle_mouse(UIContext<InputAction>& ctx, const Session& sess) {
    State& st = state();
    if (st.lastLines.empty()) { recompute_highlight(st); return; }
    const auto& mouse = ctx.mouse;
    float mx = mouse.pos.x, my = mouse.pos.y;

    auto colAt = [&](const Rec& r) -> int {
        float rel = mx - r.contentX0;
        if (rel <= 0) return 0;
        float best = std::fabs(rel);
        int bc = 0;
        for (size_t column : code_wrap::character_ends(r.content)) {
            float distance = std::fabs(code_mw(sess, r.content.substr(0, column)) - rel);
            if (distance < best) { best = distance; bc = static_cast<int>(column); }
        }
        return bc;
    };
    auto lineUnder = [&]() -> int {
        for (int i = 0; i < (int)st.lastLines.size(); ++i) {
            const Rectangle& rc = st.lastLines[i].rect;
            if (mx >= rc.x && mx <= rc.x + rc.width &&
                my >= rc.y && my <= rc.y + rc.height) return i;
        }
        return -1;
    };
    auto nearestLine = [&]() -> int {
        int nn = -1; float bd = 1e30f;
        auto anchor = std::find_if(st.lastLines.begin(), st.lastLines.end(),
            [&](const Rec& r) { return r.ent == st.anchor.ent; });
        for (int i = 0; i < (int)st.lastLines.size(); ++i) {
            if (anchor != st.lastLines.end() &&
                (st.lastLines[i].side != anchor->side || st.lastLines[i].filePath != anchor->filePath)) continue;
            const Rectangle& rc = st.lastLines[i].rect;
            float d = std::fabs(my - (rc.y + rc.height / 2.f));
            if (d < bd) { bd = d; nn = i; }
        }
        return nn;
    };

    if (mouse.just_pressed) {
        int li = lineUnder();
        if (li >= 0) {
            // Press on a line: start a new selection. (A press+release with no
            // drag collapses anchor==head on release, which clears it.)
            st.anchor = st.head = {st.lastLines[li].ent, colAt(st.lastLines[li])};
            st.dragging = true; st.hasSel = false;
        }
        // Press off a line (header, Copy button, sidebar): leave any existing
        // selection intact so the Copy button stays clickable.
    }
    if (mouse.left_down && st.dragging) {
        int li = nearestLine();
        if (li >= 0) {
            st.head = {st.lastLines[li].ent, colAt(st.lastLines[li])};
            if (!(st.head == st.anchor)) st.hasSel = true;
        }
    }
    if (mouse.just_released) {
        st.dragging = false;
        if (st.head == st.anchor) st.hasSel = false;
    }
    recompute_highlight(st);
}

inline std::optional<reading::ReadingAnchor> source_point(const ecs::FileDiff& file, const Rectangle& viewport,
                                                         const std::string& revision) {
    const auto& selection = state();
    auto anchor = [&](const Rec& row, int column) {
        const auto side = row.sign == '-' || row.side == 1 ? reading::DiffSide::Before : reading::DiffSide::After;
        const auto text = reading::diff_text_at(file, row.lineNo, side);
        return reading::ReadingAnchor{file.filePath, revision, side, row.lineNo,
            reading::column_at_byte(text, row.sourceOffset + static_cast<size_t>(std::max(0, column))), 0.f, row.sign};
    };
    for (const auto& row : selection.lastLines)
        if (row.ent == selection.anchor.ent && row.filePath == file.filePath) return anchor(row, selection.anchor.col);
    for (const auto& row : selection.lastLines)
        if (row.filePath == file.filePath && (row.sign == '+' || row.sign == '-') &&
            row.rect.y + row.rect.height > viewport.y && row.rect.y < viewport.y + viewport.height)
            return anchor(row, 0);
    return {};
}

} // namespace diff_sel

namespace diff_detail {

// Diff colors — all defined in theme.h, aliased here for brevity
inline const auto& DIFF_ADD_BG    = theme::DIFF_ADD_BG;
inline const auto& DIFF_DEL_BG    = theme::DIFF_DEL_BG;
inline const auto& HUNK_HEADER_BG = theme::DIFF_HUNK_BG;

inline float code_line_height() {
    return Settings::get().get_code_font_size() + 8.f;
}
inline float hunk_header_height() { return std::max(24.f, Settings::get().get_code_font_size() + 8.f); }
constexpr float COMMENT_COMPOSE_H = 104.0f;

// A human-visible reason for a failed git op. Some failures (e.g. a lost
// index.lock race) leave stderr empty, which rendered as a bare "Approve
// failed:" with no cause; fall back to stdout, then the exit code.
inline std::string git_err(const git::GitResult& r) {
    if (!r.stderr_str().empty()) return r.stderr_str();
    if (!r.stdout_str().empty()) return r.stdout_str();
    return "git exit " + std::to_string(r.exit_code());
}

// ID ranges for diff elements to avoid collision with other systems.
// MainContentSystem uses 3000-3999. We use 4000-59999.
constexpr int BASE_ID = 4000;

inline std::string hunk_to_text(const ecs::DiffHunk& hunk) {
    std::string text = hunk.header + "\n";
    for (auto& line : hunk.lines) {
        text += line + "\n";
        if (hunk.noNewline.contains(static_cast<size_t>(&line - hunk.lines.data())))
            text += "\\ No newline at end of file\n";
    }
    return text;
}

inline std::string file_diff_to_text(const ecs::FileDiff& diff) {
    std::string text = "--- a/" + (diff.oldPath.empty() ? diff.filePath : diff.oldPath) + "\n";
    text += "+++ b/" + diff.filePath + "\n";
    for (auto& hunk : diff.hunks) {
        text += hunk_to_text(hunk);
    }
    return text;
}

inline std::string file_header_label(const ecs::FileDiff& fileDiff) {
    if (fileDiff.isFullContent) return "Source";
    std::string label = fileDiff.filePath;
    if (fileDiff.isRenamed && !fileDiff.oldPath.empty())
        label = fileDiff.oldPath + " -> " + fileDiff.filePath;
    std::string stats;
    if (fileDiff.additions > 0) stats += "+" + std::to_string(fileDiff.additions);
    if (fileDiff.deletions > 0) {
        if (!stats.empty()) stats += " ";
        stats += "-" + std::to_string(fileDiff.deletions);
    }
    if (!stats.empty()) label += "  " + stats;
    if (fileDiff.isNew) label += "  (new file)";
    else if (fileDiff.isDeleted) label += "  (deleted)";
    else if (fileDiff.isBinary) label += "  (binary)";
    return label;
}

inline std::vector<afterhours::ui::TextSpan> hunk_caption(const std::string& header) {
    auto rangeEnd = header.find("@@", 2);
    if (rangeEnd == std::string::npos) return {{header, theme::DIFF_HUNK_HEADER}};
    return {{header.substr(0, rangeEnd + 2), theme::DIFF_HUNK_HEADER},
            {header.substr(rangeEnd + 2), theme::TEXT_TERTIARY}};
}

// ----------------------------------------------------------------------------
// Row virtualization (inline diff only)
// ----------------------------------------------------------------------------
// A large diff builds one div per line EVERY frame — afterhours culls the draw
// but the build + autolayout + text-measure is O(total lines), the freeze
// cliff. We track a running content-Y as rows are emitted and only build the
// rows whose span intersects the visible scroll window (+1 screen overscan);
// skipped runs collapse into a single spacer div of equal height so the
// scroll container's content_size (and thus scrollbar) stays exact.
struct DiffViewport {
    afterhours::ui::HasScrollView* scroll = nullptr;
    bool active = false;
    float screenH = 720.f;
    float contentWidth = 0.f;   // for spacer width; <=0 -> percent(1.0)
    float top = 0.f, bottom = 1e30f; // visible content-Y window (px, w/ overscan)
    float curY = 0.f;           // running content-Y of the next row (px)
    float pending = 0.f;        // height of skipped rows not yet flushed (px)
    ecs::ReadingLayout* layout = nullptr;
    std::optional<reading::ReadingAnchor> restoreAnchor;
    struct AnchorCandidate { int distance; int line; float y; };
    std::optional<AnchorCandidate> nearestAnchor;
    bool restoredAnchor = false;

    void restore_at(float y) {
        const float height = scroll->viewport_or_zero().y;
        const float target = std::max(0.f, y - restoreAnchor->viewportFraction * height);
        scroll->scroll_offset.y = scroll->scroll_target.y = scroll->last_eased_offset.y = target;
        scroll->anchor_child = -1;
        top = target - height;
        bottom = target + height * 2.f;
        restoredAnchor = true;
    }

    void observe_line(const std::string& path, int line, reading::DiffSide side, char,
                      const std::string& text, size_t begin, size_t end) {
        if (!scroll || scroll->viewport_or_zero().y <= 0.f) return;
        if (restoreAnchor && !restoredAnchor && path == restoreAnchor->path && side == restoreAnchor->side) {
            const int distance = std::abs(line - restoreAnchor->line);
            if (!nearestAnchor || distance < nearestAnchor->distance) nearestAnchor = AnchorCandidate{distance, line, curY};
            const size_t byte = reading::byte_at_column(text, restoreAnchor->column);
            if (line == restoreAnchor->line && byte >= begin && (byte < end || end == text.size())) restore_at(curY);
        }

    }


    float px(float raw720) const {
        return raw720 * zoom::get();
    }

    // Is a row of height raw720 at curY within the visible window?
    bool visible(float raw720) const {
        if (!active) return true;
        float h = px(raw720);
        return (curY + h >= top) && (curY <= bottom);
    }
    void built(float raw720) { if (active) curY += px(raw720); }
    void reveal() {
        if (!active || !scroll) return;
        float height = scroll->viewport_or_zero().y;
        float target = std::clamp(curY - px(36.f), 0.f,
                                 std::max(0.f, scroll->content_size.y - height));
        scroll->scroll_offset.y = scroll->scroll_target.y = scroll->last_eased_offset.y = target;
        top = target - height;
        bottom = target + height * 2.f;
    }
    void skipped(float raw720) {
        if (!active) return;
        float h = px(raw720);
        pending += h;
        curY += h;
    }
    // Emit one spacer div for the accumulated skipped height, if any. Called
    // before building any real row so the row lands at the right column offset.
    void flush(UIContext<InputAction>& ctx, Entity& parent, int& nextId) {
        if (!active || pending <= 0.5f) return;
        auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);
        div(ctx, mk(parent, nextId++),
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{w, pixels(pending / zoom::get())})
                .with_custom_background(theme::PANEL_BG)
                .with_roundness(0.0f)
                .with_debug_name("diff_virt_spacer"));
        pending = 0.f;
    }
};

} // namespace diff_detail

// Render a single diff line as a composed label.
// Format: "  OldLn  NewLn  content"
inline void render_diff_line(UIContext<InputAction>& ctx,
                              Entity& parent,
                              int id,
                              const std::string& line,
                              int& oldLine,
                              int& newLine,
                              float contentWidth = 0,
                              const std::string& filePath = "",
                              diff_sel::Session* sel = nullptr,
                              code_highlight::Range changed = {},
                              bool hasNewline = true, bool moved = false, bool fullContent = false,
                              size_t sourceOffset = 0, bool finalFragment = true, const std::string* original = nullptr) {
    afterhours::Color bgColor, textColor;
    std::string oldNum, newNum;
    std::string content;
    char sign;

    // Determine line type from prefix character
    char prefix = line.empty() ? ' ' : line[0];
    content = line.size() > 1 ? line.substr(1) : "";

    // Only the background carries add/del color; text stays one color so the
    // code reads consistently.
    textColor = theme::TEXT_PRIMARY;
    if (prefix == '+') {
        bgColor   = diff_detail::DIFF_ADD_BG;
        newNum    = std::to_string(newLine++);
        sign      = '+';
    } else if (prefix == '-') {
        bgColor   = diff_detail::DIFF_DEL_BG;
        oldNum    = std::to_string(oldLine++);
        sign      = '-';
    } else {
        bgColor   = theme::PANEL_BG;
        oldNum    = std::to_string(oldLine++);
        newNum    = std::to_string(newLine++);
        sign      = ' ';
    }

    if (moved) bgColor = afterhours::Color{35, 55, 85, 255};

    // Format: "OldLn NewLn  <sign> content"
    // The dedicated sign column makes add/del/context scannable without
    // relying on background color alone.
    std::string gutter = code_gutter::prefix(oldNum, newNum, sign, fullContent);
    if (sourceOffset) gutter.assign(gutter.size(), ' ');
    std::string label = gutter + content;

    float available = std::max(1.f, contentWidth * zoom::get() - diff_sel::content_x_offset(*sel, gutter) - 12.f);
    auto ending = diff_sel::ending_label(*sel, original ? *original : content, hasNewline, available);
    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);
    auto lineDiv = div(ctx, mk(parent, id),
        ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{w, pixels(diff_detail::code_line_height())})
            .with_custom_background(bgColor)
            .with_border_left(prefix == '+' ? theme::DIFF_ADD_TEXT : prefix == '-' ? theme::DIFF_DEL_TEXT : bgColor, pixels(2))
            .with_custom_text_color(textColor)
            .with_styled_label(highlighted_code(label.substr(0, label.size() - content.size()), content, filePath,
                                                sel && sel->visibleWhitespace, hasNewline, original, sourceOffset, finalFragment, ending))
            .with_font("mono", pixels(Settings::get().get_code_font_size()))
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = pixels(0), .right = pixels(0),
                .bottom = pixels(0), .left = pixels(0)})
            .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_roundness(0.0f)
            .with_debug_name("diff_line"));
    if (moved) set_tooltip(lineDiv.ent(), "Moved unchanged code");
    if (sel->visibleWhitespace && finalFragment) set_tooltip(lineDiv.ent(), !hasNewline ? "No newline at end of file" :
        (original ? *original : content).ends_with('\r') ? "Line ending: CRLF" : "Line ending: LF");

    if (sel && sel->enabled) {
        // Register this line (using the prior frame's resolved rect) so the next
        // frame can hit-test drags and the copy action can extract text.
        Rectangle r = afterhours::ui::detail::apply_scroll_offset(
            lineDiv.ent(), lineDiv.ent().get<afterhours::ui::UIComponent>().rect());
        float prefixW = diff_sel::content_x_offset(*sel, gutter);
        float cx0 = r.x + prefixW;
        diff_sel::render_changed_range(ctx, lineDiv.ent(), *sel, content,
                                       prefixW, changed, prefix == '-');
        int lno = !newNum.empty() ? std::stoi(newNum)
                                  : (!oldNum.empty() ? std::stoi(oldNum) : 0);
        diff_sel::state().curLines.push_back(
            {lineDiv.ent().id, content, filePath, lno, r, cx0, 0, prefix,
             oldNum.empty() ? 0 : std::stoi(oldNum), newNum.empty() ? 0 : std::stoi(newNum), sourceOffset,
             reading::column_at_byte(original ? *original : content, sourceOffset)});
        if (diff_sel::found_line(sel, filePath, lno, prefix))
            diff_sel::render_find_match(ctx, lineDiv.ent(), *sel, content, prefixW, sourceOffset);

        // Draw the selection highlight for the covered column range, if any.
        auto it = diff_sel::state().hl.find(lineDiv.ent().id);
        if (it != diff_sel::state().hl.end()) {
            int n = static_cast<int>(content.size());
            int a = std::min(it->second.first, n);
            int b = std::min(it->second.second, n);
            float x0 = prefixW + diff_sel::code_mw(*sel, content.substr(0, a));
            float x1 = prefixW + diff_sel::code_mw(*sel, content.substr(0, b));
            if (x1 > x0) {
                // Translucent selection overlay. afterhours now alpha-blends div
                // backgrounds, so a low-alpha box drawn OVER the already-rendered
                // line tints the selected span while the text shows through — no
                // need to re-draw the text on an opaque box anymore.
                div(ctx, mk(lineDiv.ent(), 90001),
                    ComponentConfig{}.with_skip_grid_snap()
                        .with_size(ComponentSize{pixels((x1 - x0) / zoom::get()),
                                                 pixels(diff_detail::code_line_height())})
                        .with_absolute_position(x0 / zoom::get(), 0.f)
                        .with_custom_background(afterhours::Color{58, 130, 210, 90})
                        .with_roundness(0.0f)
                        .with_debug_name("diff_sel_hl"));
            }
        }
    }
}

// Render a single hunk with its header and all diff lines.
inline void render_sbs_hunk(UIContext<InputAction>&, Entity&, const ecs::FileDiff&,
                            const ecs::DiffHunk&, int&, float,
                            diff_detail::DiffViewport*, diff_sel::Session*, bool);

inline void render_hunk(UIContext<InputAction>& ctx,
                         Entity& parent,
                         const ecs::FileDiff& fileDiff,
                         const ecs::DiffHunk& hunk,
                         int& nextId,
                         float contentWidth = 0,
                         diff_sel::Session* sel = nullptr,
                         diff_detail::DiffViewport* vp = nullptr,
                         bool sideBySide = false,
                         float lineWidth = 0.f) {

    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);

    // Review state for this hunk (working-tree diff only).
    bool reviewOn = sel && sel->reviewActions && sel->review;
    std::string hkey;
    bool isCursor = false;
    if (reviewOn) {
        hkey = sel->reviewScope + "\n" +
               ecs::ReviewComponent::hunk_key(fileDiff.filePath, hunk);
        if (sel->review->approvedHunks.count(hkey) && !sel->review->showApproved)
            return;
        // Keyboard chunk cursor + pending vim actions (a=approve, c=comment).
        int ord = sel->hunkOrdinal++;
        // Read-only embedded (commit-detail) diff has no review cursor, so the
        // first hunk must not pick up the cursor highlight.
        isCursor = !sel->embedded && (ord == sel->review->cursor);
        if (isCursor && sel->review->cursorMoved && vp && vp->scroll) {
            float viewportHeight = vp->scroll->viewport_or_zero().y;
            float target = std::clamp(vp->curY - vp->px(24.f), 0.f,
                                     std::max(0.f, vp->scroll->content_size.y - viewportHeight));
            vp->scroll->scroll_offset.y = target;
            vp->scroll->scroll_target.y = target;
            vp->scroll->last_eased_offset.y = target;
            vp->top = target - viewportHeight;
            vp->bottom = target + viewportHeight * 2.f;
            sel->review->cursorMoved = false;
        }
        if (isCursor && sel->review->cursorApprove) {
            sel->review->cursorApprove = false;
            sel->review->approvedHunks.insert(hkey);
            sel->review->dirty = true;
            afterhours::toast::send_info(ctx, "Approved for review; index unchanged", 1.5f);
            if (!sel->review->showApproved) return;
        }
        if (isCursor && sel->review->cursorComment) {
            sel->review->cursorComment = false;
            int line = hunk.newCount == 0 ? hunk.oldStart : hunk.newStart;
            begin_diff_comment(*sel->review, hkey,
                {sel->reviewScope, fileDiff.filePath, line, "", line, hunk.newCount == 0}, hunk);
        }
    }

    // Hunk header row: label + copy button. Culled like the lines: a diff
    // with thousands of hunks used to build every header (six entities each)
    // no matter what was on screen, which dwarfed the culled line rows.
    int hunkHeaderId = nextId++;
    const bool headerVisible =
        !vp || !vp->active || vp->visible(diff_detail::hunk_header_height());
    if (!headerVisible) {
        vp->skipped(diff_detail::hunk_header_height());
    } else {
    if (vp) vp->flush(ctx, parent, nextId);
    auto hunkRow = div(ctx, mk(parent, hunkHeaderId),
        ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{w, pixels(diff_detail::hunk_header_height())})
            .with_flex_direction(FlexDirection::Row)
            .with_justify_content(JustifyContent::SpaceBetween)
            .with_align_items(AlignItems::Center)
            .with_custom_background(isCursor ? afterhours::Color{38, 79, 140, 255}
                                             : diff_detail::HUNK_HEADER_BG)
            .with_roundness(0.0f)
            .with_debug_name("hunk_header_row"));
    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>())
        bind_focus(hunkRow.ent(), *repo, reading::focus::Region::Code, hkey);
    hunkRow.ent().addComponentIfMissing<HasClickListener>([](Entity&){});
    if (vp) vp->built(diff_detail::hunk_header_height());
    if (ctx.is_right_click(hunkRow.ent().id)) {
        remember_focus_origin(ctx, hunkRow.ent());
        std::vector<ContextMenuItem> items;
        auto* owner = ecs::find_singleton_entity<ecs::RepoComponent, ecs::ActiveTab>();
        auto currentTab = [ownerId = owner ? std::optional(owner->id) : std::nullopt]() -> Entity* {
            auto* active = ecs::find_singleton_entity<ecs::RepoComponent, ecs::ActiveTab>();
            return ownerId && active && active->id == *ownerId && !active->cleanup ? active : nullptr;
        };
        if (sel && !sel->repoPath.empty() && !fileDiff.isFullContent) {
            items.push_back(ContextMenuItem::item("Show surrounding lines", [currentTab] {
                if (auto* tab = currentTab()) {
                    auto* repo = &tab->get<ecs::RepoComponent>();
                    repo->diffContext = std::min(10000, repo->diffContext + 20);
                    repo->refreshRequested = true;
                    repo->cachedFilePath.clear();
                    if (tab->has<ecs::CommitDetailCache>()) tab->get<ecs::CommitDetailCache>().cachedCommitHash.clear();
                }
            }));
        }
        items.push_back(ContextMenuItem::item("Copy hunk", [hunk] {
            afterhours::clipboard::set_text(diff_detail::hunk_to_text(hunk));
        }));
        if (reviewOn) {
            auto currentReview = [currentTab, session = sel->review->storageScope, repoPath = sel->repoPath]() -> ecs::ReviewComponent* {
                auto* tab = currentTab();
                if (!tab || !tab->has<ecs::ReviewComponent>() || tab->get<ecs::RepoComponent>().repoPath != repoPath) return nullptr;
                auto& review = tab->get<ecs::ReviewComponent>();
                return review.storageScope == session ? &review : nullptr;
            };
            auto key = hkey;
            bool approved = sel->review->approvedHunks.contains(key);
            items.push_back(ContextMenuItem::item(approved ? "Unapprove hunk" : "Approve hunk", [currentReview, key, approved] {
                auto* reviewPtr = currentReview();
                if (!reviewPtr) return;
                if (approved) reviewPtr->approvedHunks.erase(key);
                else reviewPtr->approvedHunks.insert(key);
                reviewPtr->dirty = true;
            }));
            if (sel->reviewScope == "wt" && owner && !owner->get<ecs::RepoComponent>().reviewWorkspace) {
                auto fd = fileDiff;
                auto repoPath = sel->repoPath;
                items.push_back(ContextMenuItem::item("Stage hunk", [currentTab, currentReview, repoPath, fd, hunk] {
                    auto* tab = currentTab();
                    if (!tab || !currentReview() || tab->get<ecs::RepoComponent>().reviewWorkspace) return;
                    auto res = fd.isSubmodule ? git::stage_file(repoPath, fd.filePath)
                                              : git::stage_hunk(repoPath, fd, hunk);
                    if (res.success()) {
                        tab->get<ecs::RepoComponent>().refreshRequested = true;
                    }
                }));
            }
            int line = hunk.newCount == 0 ? hunk.oldStart : hunk.newStart;
            auto scope = sel->reviewScope;
            auto path = fileDiff.filePath;
            bool oldSide = hunk.newCount == 0;
            items.push_back(ContextMenuItem::item("Comment on hunk", [currentReview, key, scope, path, line, oldSide, hunk] {
                if (auto* review = currentReview()) begin_diff_comment(*review, key, {scope, path, line, "", line, oldSide}, hunk);
            }));
        }
        show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
    }

    // The label takes what the action cluster (Copy/Comment/Approve) leaves.
    // This used to subtract a hardcoded reserve, because percent(1.0) took the
    // whole row and shoved the buttons off-screen.
    auto captionClip = div(ctx, mk(hunkRow.ent(), 20), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{expand(), percent(1.f)}).with_overflow(Overflow::Hidden)
        .with_debug_name("hunk_caption_clip"));
    div(ctx, mk(captionClip.ent(), 0),
        ComponentConfig{}.with_skip_grid_snap()
            .with_styled_label(diff_detail::hunk_caption(hunk.header))
            .with_size(ComponentSize{percent(1.f), percent(1.0f)})
            .with_custom_text_color(theme::DIFF_HUNK_HEADER)
            .with_font("mono", pixels(14))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = pixels(4), .right = pixels(0),
                .bottom = pixels(4), .left = pixels(12)})
            .with_debug_name("hunk_header_label"));

    // Buttons live in a right-aligned group so they cluster together instead of
    // being spread apart by the row's SpaceBetween.
    auto hunkBtns = div(ctx, mk(hunkRow.ent(), 9),
        ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{children(), percent(1.0f)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_gap(pixels(6))
            .with_margin(Margin{.right = pixels(8)})
            .with_transparent_bg()
            .with_roundness(0.0f)
            .with_debug_name("hunk_header_btns"));

    if (sel && !sel->repoPath.empty() && !fileDiff.isFullContent) {
        auto context = button(ctx, mk(hunkBtns.ent(), 4), preset::Button("Show surrounding lines")
            .with_size(ComponentSize{children(), pixels(18)})
            .with_font_size(pixels(12))
            .with_custom_background(theme::BUTTON_SECONDARY)
            .with_debug_name("expand_diff_context"));
        set_tooltip(context.ent(), "Show 20 more unchanged lines before and after each change.");
        if (context) {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) {
                repo->diffContext = std::min(10000, repo->diffContext + 20);
                repo->refreshRequested = true;
                repo->cachedFilePath.clear();
            }
            if (auto* cache = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>())
                cache->cachedCommitHash.clear();
        }
    }

    // Copy button only where drag-select-to-copy isn't available (i.e. the
    // embedded commit-detail diff). In the working-tree diff, select-to-copy
    // (with file:line) replaces it.
    if (!(sel && sel->enabled)) {
        std::string hunkText = diff_detail::hunk_to_text(hunk);
        auto copyBtn = button(ctx, mk(hunkBtns.ent(), 1),
            preset::Button("Copy")
                .with_size(ComponentSize{children(), pixels(18)})
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = pixels(8)})
                .with_custom_background(afterhours::Color{78, 78, 86, 255})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(pixels(12))
                .with_debug_name("copy_hunk_btn"));
        if (copyBtn) {
            afterhours::clipboard::set_text(hunkText);
            afterhours::toast::send_info(ctx, "Copied hunk to clipboard", 1.5f);
        }
    }

    if (reviewOn) {
        {
            bool approved = sel->review->approvedHunks.contains(hkey);
            auto approveBtn = button(ctx, mk(hunkBtns.ent(), 2),
                preset::Button(approved ? "Unapprove" : "Approve")
                    .with_size(ComponentSize{children(), pixels(18)})
                    .with_padding(Padding{
                        .top = pixels(2), .right = pixels(8),
                        .bottom = pixels(2), .left = pixels(8)})
                    .with_custom_background(theme::BUTTON_SECONDARY)
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(pixels(12))
                    .with_debug_name("approve_hunk_btn"));
            if (approveBtn) {
                if (approved) sel->review->approvedHunks.erase(hkey);
                else sel->review->approvedHunks.insert(hkey);
                sel->review->dirty = true;
                afterhours::toast::send_info(ctx, approved ? "Approval removed" : "Approved for review; index unchanged", 1.5f);
            }
        }
        auto* hunkRepo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        if (sel->reviewScope == "wt" && (!hunkRepo || !hunkRepo->reviewWorkspace)) {
            if (button(ctx, mk(hunkBtns.ent(), 5), preset::Button("Stage")
                    .with_size(ComponentSize{children(), pixels(18)})
                    .with_font_size(pixels(12)).with_custom_background(theme::BUTTON_SECONDARY)
                    .with_debug_name("stage_hunk_btn"))) {
                auto res = fileDiff.isSubmodule
                    ? git::stage_file(sel->repoPath, fileDiff.filePath)
                    : git::stage_hunk(sel->repoPath, fileDiff, hunk);
                if (res.success()) {
                    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) repo->refreshRequested = true;
                    afterhours::toast::send_info(ctx, "Hunk staged; review approval unchanged", 1.5f);
                } else afterhours::toast::send_info(ctx, "Stage failed: " + diff_detail::git_err(res), 2.5f);
            }
        }
        auto commentBtn = button(ctx, mk(hunkBtns.ent(), 3),
            preset::Button("Comment")
                .with_size(ComponentSize{children(), pixels(18)})
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = pixels(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(pixels(12))
                .with_debug_name("comment_hunk_btn"));
        if (commentBtn) {
            int line = hunk.newCount == 0 ? hunk.oldStart : hunk.newStart;
            begin_diff_comment(*sel->review, hkey,
                {sel->reviewScope, fileDiff.filePath, line, "", line, hunk.newCount == 0}, hunk);
        }
    }
    } // headerVisible

    // Inline compose row for this hunk.
    if (reviewOn && sel->review->composingKey == hkey) {
        if (vp) { vp->flush(ctx, parent, nextId); vp->built(diff_detail::COMMENT_COMPOSE_H); }
        auto composeRow = div(ctx, mk(parent, nextId++),
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{w, pixels(diff_detail::COMMENT_COMPOSE_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_custom_background(afterhours::Color{35, 35, 39, 255})
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = pixels(12)})
                .with_debug_name("comment_compose_row"));
        float editorH = diff_detail::COMMENT_COMPOSE_H - 8.f;
        std::string addLabel = "Add L" + std::to_string(sel->review->composingLine) +
            (sel->review->composingEndLine > sel->review->composingLine ? "-" + std::to_string(sel->review->composingEndLine) : "") +
            (sel->review->composingOldSide ? " (old)" : "");
        float addWidth = static_cast<float>(afterhours::graphics::measure_text(addLabel.c_str(),
            static_cast<int>(12.f * zoom::get()))) / zoom::get() + 24.f;
        auto previousDraft = sel->review->composingText;
        ui::text_area(
            ctx, mk(composeRow.ent(), 0), sel->review->composingText,
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{pixels(std::max(80.f, contentWidth - addWidth - 134.f)), pixels(editorH)})
                .with_custom_background(theme::INPUT_BG)
                .with_font("mono", pixels(14.f))
                .with_line_height(pixels(22.f))
                .with_word_wrap(true)
                .with_overflow(afterhours::ui::Overflow::Hidden)
                .with_corner_radius(4.0f)
                .with_debug_name("comment_input"));
        if (previousDraft != sel->review->composingText) sel->review->dirty = true;
        render_comment_kind(ctx, composeRow.ent(), 2, *sel->review, false);
        auto addBtn = button(ctx, mk(composeRow.ent(), 1),
            preset::Button(addLabel)
                .with_size(ComponentSize{pixels(addWidth), pixels(18)})
                .with_font_size(pixels(12))
                .with_debug_name("comment_add_btn"));
        if (addBtn)
            ecs::commit_pending_comment(*sel->review);
    }

    // Folded (commented) hunks collapse — show a marker instead of the lines.
    if (reviewOn && sel->review->foldedHunks.count(hkey)) {
        if (vp) { vp->flush(ctx, parent, nextId); vp->built(20.0f); }
        auto expand = button(ctx, mk(parent, nextId++),
            ComponentConfig{}.with_skip_grid_snap()
                .with_label("\xe2\x9c\x8e commented \xc2\xb7 click to expand")
                .with_size(ComponentSize{w, pixels(20)})
                .with_custom_text_color(theme::STATUS_MODIFIED)
                .with_font_size(pixels(12))
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = pixels(52)})
                .with_debug_name("hunk_folded_marker"));
        if (vp && vp->layout) vp->layout->rows.push_back({expand.ent().id, fileDiff.filePath,
            hunk.oldStart, hunk.oldStart + hunk.oldCount - 1, hunk.newStart, hunk.newStart + hunk.newCount - 1,
            1, std::numeric_limits<int>::max(), true});
        if (expand) {
            sel->review->foldedHunks.erase(hkey);
            sel->review->dirty = true;
        }
        return;
    }

    if (sideBySide) {
        render_sbs_hunk(ctx, parent, fileDiff, hunk, nextId,
                        lineWidth > 0 ? lineWidth : contentWidth, vp, sel, false);
        return;
    }

    int oldLine = hunk.oldStart;
    int newLine = hunk.newStart;

    auto changedRanges = code_highlight::hunk_ranges(hunk.lines);
    for (size_t index = 0; index < hunk.lines.size(); ++index) {
        const auto& line = hunk.lines[index];
        char sign = line.empty() ? ' ' : line.front();
        std::string content = line.empty() ? "" : line.substr(1);
        auto gutter = code_gutter::prefix(sign == '+' ? "" : std::to_string(oldLine),
            sign == '-' ? "" : std::to_string(newLine), sign, fileDiff.isFullContent);
        float width = lineWidth > 0 ? lineWidth : contentWidth;
        float available = std::max(1.f, width * zoom::get() - diff_sel::content_x_offset(*sel, gutter) - 12.f);
        auto breaks = diff_sel::wrapped_rows(*sel, content, available, !hunk.noNewline.contains(index));
        for (size_t part = 0; part + 1 < breaks.size(); ++part) {
            int lineId = nextId++;
            auto begin = breaks[part], end = breaks[part + 1];
            if (vp) {
                if (sign != '-') vp->observe_line(fileDiff.filePath, newLine, reading::DiffSide::After, sign, content, begin, end);
                if (sign != '+') vp->observe_line(fileDiff.filePath, oldLine, reading::DiffSide::Before, sign, content, begin, end);
            }
            if (sel->findNavigate && vp &&
                diff_sel::found_line(sel, fileDiff.filePath, sign == '-' ? oldLine : newLine, sign) &&
                sel->findMatch->column >= begin && (sel->findMatch->column < end || part + 2 == breaks.size()))
                vp->reveal();
            if (!vp || vp->visible(diff_detail::code_line_height())) {
                if (vp) vp->flush(ctx, parent, nextId);
                int oldNumber = oldLine, newNumber = newLine;
                render_diff_line(ctx, parent, lineId, std::string(1, sign) + content.substr(begin, end - begin),
                    oldNumber, newNumber, width, fileDiff.filePath, sel,
                    code_wrap::intersect(changedRanges[index], begin, end), !hunk.noNewline.contains(index),
                    hunk.movedLines.contains(index), fileDiff.isFullContent, begin, part + 2 == breaks.size(), &content);
                if (vp) vp->built(diff_detail::code_line_height());
            } else vp->skipped(diff_detail::code_line_height());
        }
        if (sign != '+') ++oldLine;
        if (sign != '-') ++newLine;
    }
}

namespace diff_detail {

enum class SbsKind { Context, Add, Del, Empty };

// Render one side (left or right) of a side-by-side row as a single baked
// label ("<gutter>  <sign> <content>"). We bake gutter+content into one label
// to sidestep the afterhours Row-flex expand() bug (see docs/afterhours-gaps.md).
inline void render_sbs_cell(UIContext<InputAction>& ctx, Entity& row, int id,
                            const std::string& num, const std::string& content,
                            SbsKind kind, bool leftBorder,
                            const std::string& filePath, diff_sel::Session* sel,
                            code_highlight::Range changed = {},
                            bool hasNewline = true, bool moved = false, size_t sourceOffset = 0, bool finalFragment = true, const std::string* original = nullptr, float available = 0.f) {
    afterhours::Color bg, fg;
    char sign = ' ';
    // Only the background carries add/del color; text stays one color.
    switch (kind) {
        case SbsKind::Add:
            bg = DIFF_ADD_BG; fg = theme::TEXT_PRIMARY; sign = '+'; break;
        case SbsKind::Del:
            bg = DIFF_DEL_BG; fg = theme::TEXT_PRIMARY; sign = '-'; break;
        case SbsKind::Empty:
            // Slightly darker than the panel to read as "no line here".
            bg = afterhours::Color{26, 26, 26, 255};
            fg = theme::TEXT_SECONDARY; break;
        default:
            bg = theme::PANEL_BG; fg = theme::TEXT_PRIMARY; break;
    }

    if (moved) bg = afterhours::Color{35, 55, 85, 255};
    std::string gutter = code_gutter::pad(num) + "  " + sign + " ";
    if (sourceOffset) gutter.assign(gutter.size(), ' ');
    std::string label = gutter + content;

    auto ending = diff_sel::ending_label(*sel, original ? *original : content, hasNewline, available);
    auto cfg = ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{percent(0.5f), pixels(code_line_height())})
        .with_custom_background(bg)
        .with_custom_text_color(fg)
        .with_styled_label(highlighted_code(label.substr(0, label.size() - content.size()), content, filePath,
                                            sel && sel->visibleWhitespace && kind != SbsKind::Empty, hasNewline, original, sourceOffset, finalFragment, ending))
        .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
        .with_font("mono", pixels(Settings::get().get_code_font_size()))
        .with_alignment(TextAlignment::Left)
        .with_padding(Padding{
            .top = pixels(0), .right = pixels(0),
            .bottom = pixels(0), .left = pixels(0)})
        .with_roundness(0.0f)
        .with_debug_name("sbs_cell");
    if (leftBorder) cfg = cfg.with_border_right(theme::BORDER);
    auto cell = div(ctx, mk(row, id), cfg);
    if (moved) set_tooltip(cell.ent(), "Moved unchanged code");
    if (sel->visibleWhitespace && finalFragment) set_tooltip(cell.ent(), !hasNewline ? "No newline at end of file" :
        (original ? *original : content).ends_with('\r') ? "Line ending: CRLF" : "Line ending: LF");
    if (sel && sel->enabled && kind != SbsKind::Empty) {
        auto rect = afterhours::ui::detail::apply_scroll_offset(
            cell.ent(), cell.ent().get<afterhours::ui::UIComponent>().rect());
        float prefix = diff_sel::content_x_offset(*sel, label.substr(0, label.size() - content.size()));
        diff_sel::render_changed_range(ctx, cell.ent(), *sel, content, prefix, changed, kind == SbsKind::Del);
        diff_sel::state().curLines.push_back(
            {cell.ent().id, content, filePath, num.empty() ? 0 : std::stoi(num),
             rect, rect.x + prefix, leftBorder ? 1 : 2, sign,
             leftBorder && !num.empty() ? std::stoi(num) : 0,
             !leftBorder && !num.empty() ? std::stoi(num) : 0, sourceOffset,
             reading::column_at_byte(original ? *original : content, sourceOffset)});
        if (diff_sel::found_line(sel, filePath, num.empty() ? 0 : std::stoi(num), sign) &&
            (kind != SbsKind::Context || !leftBorder))
            diff_sel::render_find_match(ctx, cell.ent(), *sel, content, prefix, sourceOffset);
        auto it = diff_sel::state().hl.find(cell.ent().id);
        if (it != diff_sel::state().hl.end()) {
            auto a = std::min(static_cast<size_t>(it->second.first), content.size());
            auto b = std::min(static_cast<size_t>(it->second.second), content.size());
            float x0 = prefix + diff_sel::code_mw(*sel, content.substr(0, a));
            float x1 = prefix + diff_sel::code_mw(*sel, content.substr(0, b));
            div(ctx, mk(cell.ent(), 90001), ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{pixels(std::max(0.f, x1 - x0) / zoom::get()), pixels(code_line_height())})
                .with_absolute_position(x0 / zoom::get(), 0.f)
                .with_custom_background(afterhours::Color{58, 130, 210, 90})
                .with_roundness(0.f)
                .with_debug_name("diff_sel_hl"));
        }
    }
}

} // namespace diff_detail

// Render a single hunk in side-by-side mode: deletions on the left, additions
// on the right, context on both. Runs of -/+ are paired row-by-row; the shorter
// side is padded with empty cells.
inline void render_sbs_hunk(UIContext<InputAction>& ctx,
                            Entity& parent,
                            const ecs::FileDiff& fileDiff,
                            const ecs::DiffHunk& hunk,
                            int& nextId,
                            float contentWidth = 0,
                            diff_detail::DiffViewport* vp = nullptr,
                            diff_sel::Session* sel = nullptr,
                            bool showHeader = true) {
    (void)fileDiff;
    using diff_detail::SbsKind;

    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);
    const bool culling = vp && vp->active;

    if (showHeader) {
    int hunkHeaderId = nextId++;
    if (culling && !vp->visible(diff_detail::hunk_header_height())) {
        vp->skipped(diff_detail::hunk_header_height());
    } else {
    if (culling) vp->flush(ctx, parent, nextId);
    auto hunkRow = div(ctx, mk(parent, hunkHeaderId),
        ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{w, pixels(diff_detail::hunk_header_height())})
            .with_flex_direction(FlexDirection::Row)
            .with_justify_content(JustifyContent::SpaceBetween)
            .with_align_items(AlignItems::Center)
            .with_custom_background(diff_detail::HUNK_HEADER_BG)
            .with_roundness(0.0f)
            .with_debug_name("sbs_hunk_header_row"));
    if (culling) vp->built(diff_detail::hunk_header_height());
    auto captionClip = div(ctx, mk(hunkRow.ent(), 20), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{expand(), percent(1.f)}).with_overflow(Overflow::Hidden)
        .with_debug_name("hunk_caption_clip"));
    div(ctx, mk(captionClip.ent(), 0),
        ComponentConfig{}.with_skip_grid_snap()
            .with_styled_label(diff_detail::hunk_caption(hunk.header))
            .with_size(ComponentSize{percent(1.f), percent(1.0f)})
            .with_custom_text_color(theme::DIFF_HUNK_HEADER)
            .with_font("mono", pixels(14))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = pixels(4), .right = pixels(0),
                .bottom = pixels(4), .left = pixels(12)})
            .with_debug_name("sbs_hunk_header_label"));
    {
        std::string hunkText = diff_detail::hunk_to_text(hunk);
        auto copyBtn = button(ctx, mk(hunkRow.ent(), 1),
            preset::Button("Copy")
                .with_size(ComponentSize{children(), pixels(18)})
                .with_margin(Margin{.right = pixels(8)})
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = pixels(8)})
                .with_custom_background(afterhours::Color{60, 60, 65, 255})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(12))
                .with_debug_name("copy_sbs_hunk_btn"));
        if (copyBtn) {
            afterhours::clipboard::set_text(hunkText);
            afterhours::toast::send_info(ctx, "Copied hunk to clipboard", 1.5f);
        }
    }
    }
    }

    int oldLine = hunk.oldStart;
    int newLine = hunk.newStart;

    // Buffers of pending deletions/additions to pair up at each flush point.
    std::set<int> oldNoNewline, newNoNewline;
    std::set<int> oldMoved, newMoved;
    int oldNumber = oldLine, newNumber = newLine;
    for (size_t i = 0; i < hunk.lines.size(); ++i) {
        char sign = hunk.lines[i].empty() ? ' ' : hunk.lines[i].front();
        if (hunk.movedLines.contains(i)) {
            if (sign == '-') oldMoved.insert(oldNumber);
            if (sign == '+') newMoved.insert(newNumber);
        }
        if (hunk.noNewline.contains(i)) {
            if (sign != '+') oldNoNewline.insert(oldNumber);
            if (sign != '-') newNoNewline.insert(newNumber);
        }
        if (sign != '+') ++oldNumber;
        if (sign != '-') ++newNumber;
    }
    std::vector<std::pair<std::string, std::string>> dels; // (num, content)
    std::vector<std::pair<std::string, std::string>> adds;

    auto emitRow = [&](const std::string& lNum, const std::string& lContent,
                       SbsKind lKind, const std::string& rNum,
                       const std::string& rContent, SbsKind rKind) {
        float gutter = diff_sel::content_x_offset(*sel, code_gutter::pad(lNum.size() > rNum.size() ? lNum : rNum) + "  + ");
        float available = std::max(1.f, contentWidth * zoom::get() * .5f - gutter - 12.f);
        auto left = diff_sel::wrapped_rows(*sel, lContent, available, lNum.empty() || !oldNoNewline.contains(std::stoi(lNum)));
        auto right = diff_sel::wrapped_rows(*sel, rContent, available, rNum.empty() || !newNoNewline.contains(std::stoi(rNum)));
        auto changes = lKind == SbsKind::Del && rKind == SbsKind::Add
            ? code_highlight::changed_ranges(lContent, rContent)
            : std::pair<code_highlight::Range, code_highlight::Range>{};
        for (size_t part = 0; part + 1 < std::max(left.size(), right.size()); ++part) {
            int rowId = nextId++;
            bool hasLeft = part + 1 < left.size(), hasRight = part + 1 < right.size();
            if (vp) {
                if (hasRight && !rNum.empty()) vp->observe_line(fileDiff.filePath, std::stoi(rNum), reading::DiffSide::After,
                    rKind == SbsKind::Add ? '+' : ' ', rContent, right[part], right[part + 1]);
                if (hasLeft && !lNum.empty()) vp->observe_line(fileDiff.filePath, std::stoi(lNum), reading::DiffSide::Before,
                    lKind == SbsKind::Del ? '-' : ' ', lContent, left[part], left[part + 1]);
            }
            auto found = [&](bool exists, const std::vector<size_t>& breaks, const std::string& num, char sign) {
                return exists && !num.empty() && diff_sel::found_line(sel, fileDiff.filePath, std::stoi(num), sign) &&
                    sel->findMatch->column >= breaks[part] &&
                    (sel->findMatch->column < breaks[part + 1] || part + 2 == breaks.size());
            };
            if (sel->findNavigate && vp && (found(hasLeft, left, lNum, lKind == SbsKind::Del ? '-' : ' ') ||
                found(hasRight, right, rNum, rKind == SbsKind::Add ? '+' : ' '))) vp->reveal();
            if (culling && !vp->visible(diff_detail::code_line_height())) {
                vp->skipped(diff_detail::code_line_height());
                continue;
            }
            if (vp) { vp->flush(ctx, parent, nextId); vp->built(diff_detail::code_line_height()); }
            auto rowDiv = div(ctx, mk(parent, rowId), ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{w, pixels(diff_detail::code_line_height())})
                .with_flex_direction(FlexDirection::Row).with_no_wrap().with_roundness(0.f).with_debug_name("sbs_row"));
            auto cell = [&](bool exists, const std::vector<size_t>& breaks, const std::string& num,
                            const std::string& text, SbsKind kind, bool isLeft, code_highlight::Range change) {
                size_t begin = exists ? breaks[part] : 0, end = exists ? breaks[part + 1] : 0;
                diff_detail::render_sbs_cell(ctx, rowDiv.ent(), isLeft ? 0 : 1, exists ? num : "",
                    text.substr(begin, end - begin), exists ? kind : SbsKind::Empty, isLeft, fileDiff.filePath, sel,
                    code_wrap::intersect(change, begin, end), num.empty() ||
                        !(isLeft ? oldNoNewline : newNoNewline).contains(std::stoi(num)),
                    !num.empty() && (isLeft ? oldMoved : newMoved).contains(std::stoi(num)), begin,
                    exists && part + 2 == breaks.size(), exists ? &text : nullptr, available);
            };
            cell(hasLeft, left, lNum, lContent, lKind, true, changes.first);
            cell(hasRight, right, rNum, rContent, rKind, false, changes.second);
        }
    };

    auto flush = [&]() {
        size_t n = std::max(dels.size(), adds.size());
        for (size_t i = 0; i < n; ++i) {
            bool hasDel = i < dels.size();
            bool hasAdd = i < adds.size();
            emitRow(hasDel ? dels[i].first : "",
                    hasDel ? dels[i].second : "",
                    hasDel ? SbsKind::Del : SbsKind::Empty,
                    hasAdd ? adds[i].first : "",
                    hasAdd ? adds[i].second : "",
                    hasAdd ? SbsKind::Add : SbsKind::Empty);
        }
        dels.clear();
        adds.clear();
    };

    for (auto& line : hunk.lines) {
        char prefix = line.empty() ? ' ' : line[0];
        std::string content = line.size() > 1 ? line.substr(1) : "";
        if (prefix == '-') {
            dels.emplace_back(std::to_string(oldLine++), content);
        } else if (prefix == '+') {
            adds.emplace_back(std::to_string(newLine++), content);
        } else {
            flush();
            emitRow(std::to_string(oldLine), content, SbsKind::Context,
                    std::to_string(newLine), content, SbsKind::Context);
            ++oldLine;
            ++newLine;
        }
    }
    flush();
}


// Render the complete diff view for all file diffs. Shared by inline and
// side-by-side modes; only the per-hunk rendering differs.
// This is the main entry point called by MainContentSystem.
// When embedInParentScroll is true, diff content is added directly to the parent
// without creating a nested scroll container (used by commit detail view).
inline float diff_controls_height(float width, bool optionsOpen, bool findOpen,
                                  bool filterable, bool hasDiffs) {
    return (filterable ? (width < 680.f ? 72.f : 36.f) + (optionsOpen ? 90.f : 0.f) : 0.f) +
        (findOpen ? 34.f : 0.f) + (filterable && hasDiffs ? 24.f : 0.f);
}

inline void render_diff(UIContext<InputAction>& ctx,
                        Entity& parent,
                        const std::vector<ecs::FileDiff>& diffs,
                        float contentWidth, float contentHeight,
                        bool embedInParentScroll = false,
                        bool resetScroll = false,
                        bool sideBySide = false,
                        const std::string& repoPath = "",
                        ecs::ReviewComponent* review = nullptr,
                        const std::string& reviewScope = "wt",
                        Entity* findParent = nullptr) {
    if (!diffs.empty() && diffs.front().isFullContent) sideBySide = false;
    int nextId = diff_detail::BASE_ID;
    std::string imageContext = repoPath + "\n" + reviewScope;
    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>())
        imageContext += std::to_string(repo->repoVersion) + ":" + std::to_string(repo->dataGeneration);
    for (const auto& file : diffs) imageContext += "\n" + file.filePath;
    image_diff::begin(imageContext);
    auto* ownerRepo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
    if (ownerRepo && !repoPath.empty() && !diffs.empty() && reviewScope != "snapshot") {
        auto revision = diff_revisions(reviewScope).second;
        std::string key = repoPath + "\n" + revision;
        if (revision.empty() || revision == "INDEX") key += "\n" + std::to_string(ownerRepo->dataGeneration);
        if (ownerRepo->codeownersKey != key) {
            ownerRepo->codeownersKey = key;
            ownerRepo->codeownersDocument = {};
            ownerRepo->codeownersByPath.clear();
            ownerRepo->codeownersFuture = codeowners::load_async(repoPath, revision);
        }
        if (ownerRepo->codeownersFuture.valid() && ownerRepo->codeownersFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ownerRepo->codeownersDocument = ownerRepo->codeownersFuture.get();
            ownerRepo->codeownersByPath.clear();
            ownerRepo->codeownersFuture = {};
        }
    }

    diff_sel::Session sess;
    sess.reviewActions =
        (review != nullptr) && (reviewScope == "wt" || diff_target(reviewScope).kind == DiffTarget::Kind::Comparison ||
            diff_target(reviewScope).kind == DiffTarget::Kind::ParentComparison || review->reviewing);
    sess.embedded = embedInParentScroll;
    sess.repoPath = repoPath;
    sess.review = review;
    sess.reviewScope = reviewScope;
    auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
    sess.visibleWhitespace = layout && layout->visibleWhitespace;
    float findHeight = 0.f;
    auto* filterRepo = ownerRepo;
    const auto anchorRequest = filterRepo ? std::optional{navigation::stamp(*filterRepo, "reading-anchor")} : std::nullopt;
    bool filterable = diffs.empty() || !diffs.front().isFullContent;
    if (filterRepo && filterable && layout) {
        const auto& candidates = reviewScope == "wt" ? filterRepo->currentDiff :
            reviewScope == "index" ? filterRepo->stagedDiff : diffs;
        bool narrow = contentWidth < 680.f;
        auto toolbar = div(ctx, mk(findParent ? *findParent : parent, 597010), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{percent(1.f), pixels(narrow ? 72.f : 36.f)})
            .with_flex_direction(narrow ? FlexDirection::Column : FlexDirection::Row)
            .with_align_items(AlignItems::Center).with_no_wrap()
            .with_debug_name("review_primary_toolbar"));
        int additions = 0, deletions = 0;
        for (const auto& file : diffs) {
            additions += file.additions;
            deletions += file.deletions;
        }
        auto stats = div(ctx, mk(toolbar.ent(), 0), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{narrow ? percent(1.f) : expand(), pixels(30)})
            .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
            .with_gap(pixels(8)).with_no_wrap());
        div(ctx, mk(stats.ent(), 0), ComponentConfig{}
            .with_label(std::to_string(diffs.size()) + (diffs.size() == 1 ? " file changed" : " files changed"))
            .with_size(ComponentSize{children(), pixels(30)})
            .with_custom_text_color(theme::TEXT_SECONDARY).with_font_size(pixels(14))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("diff_stats_label"));
        div(ctx, mk(stats.ent(), 1), ComponentConfig{}.with_label("+" + std::to_string(additions))
            .with_size(ComponentSize{children(), pixels(30)}).with_font("mono", pixels(13))
            .with_custom_text_color(theme::DIFF_ADD_TEXT).with_debug_name("diff_additions"));
        div(ctx, mk(stats.ent(), 2), ComponentConfig{}.with_label("-" + std::to_string(deletions))
            .with_size(ComponentSize{children(), pixels(30)}).with_font("mono", pixels(13))
            .with_custom_text_color(theme::DIFF_DEL_TEXT).with_debug_name("diff_deletions"));
        const bool compact = contentWidth < 480.f;
        auto actions = div(ctx, mk(toolbar.ent(), 1), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{narrow ? percent(1.f) : pixels(464), pixels(32)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(compact ? 6 : 8))
            .with_align_items(AlignItems::Center).with_debug_name("diff_mode_toggle"));
        auto modes = div(ctx, mk(actions.ent(), 10), ComponentConfig{}
            .with_size(ComponentSize{pixels(compact ? 108 : 124), pixels(32)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap()
            .with_padding(Padding{.top = pixels(3), .right = pixels(3), .bottom = pixels(3), .left = pixels(3)})
            .with_custom_background(theme::BUTTON_SECONDARY).with_border(theme::BORDER, pixels(1))
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(5.f)
            .with_debug_name("review_segments"));
        auto modeButton = [&](int id, const char* label, bool active) {
            return button(ctx, mk(modes.ent(), id), preset::Button(label)
                .with_size(ComponentSize{expand(), pixels(26)})
                .with_custom_background(active ? segment_selected_color() : theme::BUTTON_SECONDARY)
                .with_custom_text_color(active ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY)
                .with_font_size(pixels(13))
                .with_padding(Padding{.left = pixels(4), .right = pixels(4)})
                .with_debug_name(active ? "diff_mode_active" : "diff_mode_inactive"));
        };
        if (modeButton(0, "Unified", !sideBySide)) layout->diffViewMode = ecs::LayoutComponent::DiffViewMode::Inline;
        if (modeButton(1, "Split", sideBySide)) layout->diffViewMode = ecs::LayoutComponent::DiffViewMode::SideBySide;
        if (review) {
            const auto comments = std::count_if(review->comments.begin(), review->comments.end(),
                [&](const auto& comment) { return comment.scope == reviewScope; });
            auto feedback = button(ctx, mk(actions.ent(), 2), preset::Button("")
                .with_size(ComponentSize{expand(), pixels(30)}).with_transparent_bg()
                .with_border(theme::BORDER, pixels(1)).with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center).with_gap(pixels(6)).with_no_wrap()
                .with_debug_name("basket_toggle_btn"));
            chrome_icon(ctx, mk(feedback.ent(), 0), ChromeIcon::Message, theme::TEXT_SECONDARY, "feedback_icon");
            div(ctx, mk(feedback.ent(), 1), ComponentConfig{}.with_label("Feedback " + std::to_string(comments))
                .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(13))
                .with_custom_text_color(theme::TEXT_PRIMARY).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
            if (feedback) review->basketOpen = !review->basketOpen;
        }
        if (review) {
            auto counts = ecs::review_progress(*review, reviewScope, candidates);
            auto verdict = ecs::current_review_verdict(*review, reviewScope, candidates);
            auto finish = button(ctx, mk(actions.ent(), 3), preset::Button("")
                    .with_size(ComponentSize{pixels(compact ? 104 : 124), pixels(30)})
                    .with_padding(Padding{.left = pixels(6), .right = pixels(6)})
                    .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
                    .with_gap(pixels(4)).with_no_wrap()
                    .with_custom_background(theme::TEXT_ACCENT).with_custom_text_color(theme::WINDOW_BG)
                    .with_debug_name("finish_review"));
            chrome_icon(ctx, mk(finish.ent(), 0), ChromeIcon::Check, theme::WINDOW_BG, "finish_review_icon");
            div(ctx, mk(finish.ent(), 1), ComponentConfig{}
                .with_label(verdict == ReviewVerdict::InProgress ? "Finish review" : review_verdict_label(verdict))
                .with_size(ComponentSize{expand(), pixels(28)}).with_font("ui-bold", pixels(13))
                .with_custom_text_color(theme::WINDOW_BG).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
            if (finish) {
                std::vector<ContextMenuItem> choices;
                for (auto value : {ReviewVerdict::Approved, ReviewVerdict::ChangesRequested, ReviewVerdict::Commented, ReviewVerdict::InProgress})
                    choices.push_back(ContextMenuItem::item(value == ReviewVerdict::InProgress ? "Reopen review" : review_verdict_label(value),
                        [scope = reviewScope, storage = review->storageScope, path = filterRepo->repoPath,
                         signature = ecs::review_target_signature(candidates), value] {
                            auto* active = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
                            if (!active || active->storageScope != storage || active->storageRepoPath != path) return;
                            active->verdicts[scope] = {value, signature};
                            if (value == ReviewVerdict::InProgress) active->queue.completed.erase(diff_target(scope).after);
                            active->dirty = true;
                        }, value != ReviewVerdict::Approved || counts.can_approve()));
                show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(choices));
            }
        }
        auto options = button(ctx, mk(actions.ent(), 4), preset::Button(compact ? "..." : "Options")
                .with_size(ComponentSize{pixels(compact ? 32 : 64), pixels(30)}).with_font_size(pixels(13))
                .with_padding(Padding{.left = pixels(4), .right = pixels(4)})
                .with_transparent_bg().with_border(theme::BORDER, pixels(1))
                .with_debug_name("diff_options_toggle"));
        set_tooltip(options.ent(), "Diff options");
        if (options) layout->diffOptionsOpen = !layout->diffOptionsOpen;
        findHeight = narrow ? 72.f : 36.f;
    }
    if (filterRepo && filterable && layout && layout->diffOptionsOpen) {
        auto filters = div(ctx, mk(findParent ? *findParent : parent, 597000), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{pixels(contentWidth), pixels(30)}).with_flex_direction(FlexDirection::Row)
            .with_debug_name("review_file_filters"));
        auto toggle = [&](int id, const std::string& label, const std::string& debugName, bool& hidden) {
            if (button(ctx, mk(filters.ent(), id), preset::Button(label + (hidden ? ": hidden" : ": shown"))
                    .with_size(ComponentSize{percent(0.2f), pixels(26)}).with_font_size(pixels(12))
                    .with_custom_background(hidden ? theme::BUTTON_PRIMARY : theme::BUTTON_SECONDARY)
                    .with_debug_name(debugName))) hidden = !hidden;
        };
        toggle(0, "Generated", "filter_Generated", filterRepo->fileFilter.hideGenerated);
        toggle(1, "Vendor", "filter_Vendor", filterRepo->fileFilter.hideVendor);
        toggle(2, "Lockfiles", "filter_Lockfiles", filterRepo->fileFilter.hideLockfiles);
        if (button(ctx, mk(filters.ent(), 4), preset::Button(filterRepo->fileFilter.onlyUnresolved ? "Unresolved: only" : "Unresolved: all")
                .with_size(ComponentSize{percent(0.2f), pixels(26)}).with_font_size(pixels(12))
                .with_custom_background(filterRepo->fileFilter.onlyUnresolved ? theme::BUTTON_PRIMARY : theme::BUTTON_SECONDARY)
                .with_debug_name("filter_unresolved"))) filterRepo->fileFilter.onlyUnresolved = !filterRepo->fileFilter.onlyUnresolved;
        auto hidden = std::count_if(diffs.begin(), diffs.end(), [&](const auto& file) {
            return !ecs::review_file_visible(file, filterRepo->fileFilter, review, reviewScope);
        });
        div(ctx, mk(filters.ent(), 3), ComponentConfig{}.with_skip_grid_snap().with_label(std::to_string(hidden) + " hidden by filters")
            .with_size(ComponentSize{expand(), pixels(26)}).with_font_size(pixels(12)));
        auto facets = div(ctx, mk(findParent ? *findParent : parent, 597001), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{pixels(contentWidth), pixels(30)}).with_flex_direction(FlexDirection::Row));
        if (button(ctx, mk(facets.ent(), 0), preset::Button("Language: " + (filterRepo->fileFilter.language.empty() ? "All" : filterRepo->fileFilter.language))
                .with_size(ComponentSize{percent(0.333f), pixels(26)}).with_font_size(pixels(12))
                .with_custom_background(theme::BUTTON_SECONDARY).with_debug_name("filter_language"))) {
            std::set<std::string> languages;
            for (const auto& file : diffs) languages.insert(review_files::language(file.filePath));
            std::vector<ContextMenuItem> choices;
            auto add = [&](const std::string& label, const std::string& value) {
                choices.push_back(ContextMenuItem::item(label, [path = filterRepo->repoPath, value] {
                    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>(); repo && repo->repoPath == path)
                        repo->fileFilter.language = value;
                }));
            };
            add("All languages", "");
            for (const auto& value : languages) add(value, value);
            show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(choices));
        }
        if (button(ctx, mk(facets.ent(), 1), preset::Button("Change: " + review_files::change_label(filterRepo->fileFilter.change))
                .with_size(ComponentSize{percent(0.333f), pixels(26)}).with_font_size(pixels(12))
                .with_custom_background(theme::BUTTON_SECONDARY).with_debug_name("filter_change"))) {
            std::vector<ContextMenuItem> choices;
            for (char value : {' ', 'A', 'M', 'D', 'R'})
                choices.push_back(ContextMenuItem::item(review_files::change_label(value), [path = filterRepo->repoPath, value] {
                    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>(); repo && repo->repoPath == path)
                        repo->fileFilter.change = value;
                }));
            show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(choices));
        }
        if (button(ctx, mk(facets.ent(), 2), preset::Button("Sort: " + review_files::sort_label(filterRepo->fileFilter.sort))
                .with_size(ComponentSize{percent(0.333f), pixels(26)}).with_font_size(pixels(12))
                .with_custom_background(theme::BUTTON_SECONDARY).with_debug_name("review_file_sort"))) {
            std::vector<ContextMenuItem> choices;
            for (auto value : {review_files::Sort::Path, review_files::Sort::MostChanges, review_files::Sort::FewestChanges})
                choices.push_back(ContextMenuItem::item(review_files::sort_label(value), [path = filterRepo->repoPath, value] {
                    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>(); repo && repo->repoPath == path)
                        repo->fileFilter.sort = value;
                }));
            show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(choices));
        }
        auto progress = div(ctx, mk(findParent ? *findParent : parent, 597002), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{pixels(contentWidth), pixels(30)}).with_flex_direction(FlexDirection::Row));
        const auto& candidates = reviewScope == "wt" ? filterRepo->currentDiff :
            reviewScope == "index" ? filterRepo->stagedDiff : diffs;
        if (review && button(ctx, mk(progress.ent(), 0), preset::Button("Next unreviewed file")
                .with_size(ComponentSize{children(), pixels(26)}).with_font_size(pixels(12))
                .with_custom_background(theme::BUTTON_SECONDARY).with_debug_name("next_unreviewed_file"))) {
            auto current = filterRepo->diffTargetFile().empty() ? filterRepo->selectedFilePath() : filterRepo->diffTargetFile();
            auto next = ecs::next_unreviewed_file(*review, reviewScope, candidates, filterRepo->fileFilter, current);
            if (next) {
                navigation::open(*filterRepo, reading::review(reviewScope, candidates[*next].filePath));
            } else afterhours::toast::send_info(ctx, "All visible files reviewed", 2.f);
        }
        if (review) {
            auto counts = ecs::review_progress(*review, reviewScope, candidates);
            div(ctx, mk(progress.ent(), 1), ComponentConfig{}.with_skip_grid_snap()
                .with_label(std::to_string(counts.reviewed) + "/" + std::to_string(counts.total) + " files reviewed · " +
                    std::to_string(counts.unresolved) + " unresolved")
                .with_size(ComponentSize{expand(), pixels(26)}).with_font_size(pixels(12))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("review_completion"));
            if (!review->approvedHunks.empty() && button(ctx, mk(progress.ent(), 2), preset::Button(review->showApproved ? "Hide approved" : "Show approved")
                    .with_size(ComponentSize{children(), pixels(26)}).with_font_size(pixels(12))
                    .with_debug_name("toggle_approved"))) review->showApproved = !review->showApproved;
        }
        findHeight += 90.f;
    }
    auto fileVisible = [&](const ecs::FileDiff& file) {
        return !filterable || !filterRepo || ecs::review_file_visible(file, filterRepo->fileFilter, review, reviewScope);
    };
    auto fileOrder = ecs::visible_review_file_indices(diffs, filterRepo ? filterRepo->fileFilter : review_files::Filter{}, review, reviewScope);
    if (layout && layout->diffFindOpen) {
        findHeight += 34.f;
        auto bar = div(ctx, mk(findParent ? *findParent : parent, 580001), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{pixels(contentWidth), pixels(34.f)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_debug_name("diff_find_bar"));
        if (filterRepo) bind_focus(bar.ent(), *filterRepo, reading::focus::Region::Find);
        auto previous = layout->diffFindQuery;
        auto input = afterhours::text_input::text_input(ctx, mk(bar.ent(), 0), layout->diffFindQuery,
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{pixels(std::max(80.f, contentWidth - 240.f)), pixels(28)})
                .with_debug_name("diff_find_input"));
        if (layout->diffFindFocus) {
            ui::focus_control(ctx, input.ent());
            layout->diffFindFocus = false;
        }
        if (previous != layout->diffFindQuery) {
            layout->diffFindIndex = 0;
            layout->diffFindNavigate = 3;
        }
        auto matches = ecs::find_diff_matches(diffs, layout->diffFindQuery);
        std::set<std::string_view> visiblePaths;
        for (const auto& file : diffs) if (fileVisible(file)) visiblePaths.insert(file.filePath);
        std::erase_if(matches, [&](const auto& match) {
            return !visiblePaths.contains(match.file);
        });
        int count = static_cast<int>(matches.size());
        int step = 0;
        if (button(ctx, mk(bar.ent(), 1), preset::Button("Previous")
                .with_size(ComponentSize{pixels(70), pixels(28)}).with_debug_name("diff_find_previous"))) step = -1;
        if (button(ctx, mk(bar.ent(), 2), preset::Button("Next")
                .with_size(ComponentSize{pixels(48), pixels(28)}).with_debug_name("diff_find_next"))) step = 1;
        if (filterRepo && !shortcuts_blocked(*layout) && shortcut_owner(ctx, *filterRepo).input(reading::focus::Region::Find) &&
            afterhours::input::is_key_pressed(257))
            step = afterhours::input::is_key_down(340) ? -1 : 1;
        if (count > 0) {
            layout->diffFindIndex = (layout->diffFindIndex + step + count) % count;
            sess.findMatch = matches[layout->diffFindIndex];
            sess.findQuery = layout->diffFindQuery;
            if (step != 0) layout->diffFindNavigate = 3;
            sess.findNavigate = layout->diffFindNavigate > 0;
            if (layout->diffFindNavigate > 0) --layout->diffFindNavigate;
            if (review) {
                for (const auto& file : diffs) {
                    if (file.filePath != sess.findMatch->file) continue;
                    for (const auto& hunk : file.hunks) {
                        int start = sess.findMatch->sign == '-' ? hunk.oldStart : hunk.newStart;
                        int length = sess.findMatch->sign == '-' ? hunk.oldCount : hunk.newCount;
                        if (sess.findMatch->line >= start && sess.findMatch->line < start + length)
                            review->foldedHunks.erase(reviewScope + "\n" + ecs::ReviewComponent::hunk_key(file.filePath, hunk));
                    }
                }
            }
        }
        div(ctx, mk(bar.ent(), 3), ComponentConfig{}.with_skip_grid_snap()
            .with_label(count == 0 ? "No matches" : std::to_string(layout->diffFindIndex + 1) + "/" + std::to_string(count))
            .with_size(ComponentSize{pixels(80), pixels(28)})
            .with_font_size(pixels(12)).with_debug_name("diff_find_count"));
        if (button(ctx, mk(bar.ent(), 4), preset::Button("x")
                .with_size(ComponentSize{pixels(28), pixels(28)}).with_debug_name("diff_find_close")))
            layout->diffFindOpen = false;
    }
    if (!diffs.empty() && diffs.front().isFullContent && !(layout && layout->diffFindOpen)) {
        if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            repo && diff_target(reviewScope).kind == DiffTarget::Kind::File && repo->fullFileTargetLine() > 0 && !diffs.front().hunks.empty()) {
            const auto& lines = diffs.front().hunks.front().lines;
            int index = repo->fullFileTargetLine() - diffs.front().hunks.front().newStart;
            if (index >= 0 && static_cast<size_t>(index) < lines.size()) {
                const auto text = lines[static_cast<size_t>(index)].substr(1);
                const auto column = reading::byte_at_column(text, repo->workspace().source()->column);
                sess.findMatch = ecs::DiffMatch{diffs.front().filePath, repo->fullFileTargetLine(), ' ', column};
                sess.findQuery = text.substr(column);
                sess.findNavigate = repo->fullFileNavigateFrames > 0;
                if (repo->fullFileNavigateFrames > 0) --repo->fullFileNavigateFrames;
            }
        }
    }
    bool selEnabled = true;
    if (selEnabled) {
        std::string context = repoPath + "\n" + reviewScope + (sideBySide ? "\nsplit" : "\ninline") +
            std::to_string(contentWidth) + ":" + std::to_string(zoom::get()) + ":" + std::to_string(Settings::get().get_code_font_size()) +
            (sess.visibleWhitespace ? ":spaces" : ":plain");
        for (const auto& diff : diffs) if (fileVisible(diff)) context += "\n" + diff.filePath + diff_metrics().signature(diff);
        if (diff_sel::state().context != context) {
            diff_sel::reset();
            diff_sel::state().context = std::move(context);
        }
        sess.enabled = true;
        sess.tmc = &EntityHelper::get_singleton_cmp_enforce<
            afterhours::ui::TextMeasureCache>();
        sess.fontSize = Settings::get().get_code_font_size() * zoom::get();
        diff_sel::handle_mouse(ctx, sess); // update selection from prior frame

        // Cmd+C copies the current selection (keyboard path; the header button
        // is the mouse path). 343/347 = L/R Super, 67 = 'C' (GLFW keycodes).
        bool superDown = afterhours::input::is_key_down(343) ||
                         afterhours::input::is_key_down(347) ||
                         afterhours::input::is_key_down(341);
        if (filterRepo && layout && reader_shortcuts(ctx, *filterRepo, *layout) && superDown && afterhours::input::is_key_pressed(67) &&
            diff_sel::state().hasSel) {
            std::string txt = diff_sel::build_copy_text(
                diff_sel::state(), Settings::get().get_copy_with_location());
            if (!txt.empty()) {
                afterhours::clipboard::set_text(txt);
                afterhours::toast::send_info(ctx, "Copied selection", 1.5f);
            }
        }
        diff_sel::state().curLines.clear();
    }

    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);

    // When embedded, attach directly to parent; otherwise create our own scroll wrapper.
    // We always resolve contentParent to the entity that will own the diff rows.
    Entity* contentParent = &parent;
    float stickyHeight = diffs.empty() || diffs.front().isFullContent ? 0.f : 24.f;
    auto stickyHost = div(ctx, mk(findParent ? *findParent : parent, 593100), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{w, pixels(stickyHeight)}).with_custom_background(theme::WINDOW_BG));
    if (!embedInParentScroll) {
        auto h = contentHeight > 0
                     ? pixels(std::max(0.f, contentHeight - findHeight - stickyHeight))
                     : (findHeight + stickyHeight > 0 ? pixels(std::max(0.f, parent.get<afterhours::ui::UIComponent>().rect().height / zoom::get() - findHeight - stickyHeight))
                                       : percent(1.0f));
        auto scrollContainer = div(ctx, mk(parent, nextId++),
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{w, h})
                .with_overflow(Overflow::Scroll)
                .with_flex_direction(FlexDirection::Column)
                .with_no_wrap()  // scroll list stacks; never wrap into a 2nd column
                .with_custom_background(theme::PANEL_BG)
                .with_roundness(0.0f)
                .with_debug_name("diff_scroll"));
        if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) {
            std::string view = reviewScope + (sideBySide ? "\nsplit" : "\ninline");
            for (const auto& file : diffs) view += "\n" + file.filePath;
            bind_reading_view(*repo, scrollContainer.ent(), view);
        } else if (resetScroll && scrollContainer.ent().has<afterhours::ui::HasScrollView>()) {
            auto& scroll = scrollContainer.ent().get<afterhours::ui::HasScrollView>();
            scroll.scroll_offset = scroll.scroll_target = scroll.last_eased_offset = {0, 0};
        }
        contentParent = &scrollContainer.ent();
    }
    float codeWidth = contentWidth;

    diff_detail::DiffViewport vp;
    {
        vp.active = true;
        vp.screenH = (float)afterhours::graphics::get_screen_height();
        vp.contentWidth = codeWidth;
        float scrollY = 0.f, viewportH = 0.f;
        if (contentParent->has<afterhours::ui::HasScrollView>()) {
            auto& sv = contentParent->get<afterhours::ui::HasScrollView>();
            vp.scroll = &sv;
            scrollY = sv.scroll_offset.y;
            viewportH = sv.viewport_or_zero().y;
            if (embedInParentScroll) {
                auto origin = div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                    .with_size(ComponentSize{w, pixels(0)}).with_debug_name("embedded_diff_origin"));
                auto rect = afterhours::ui::detail::apply_scroll_offset(
                    origin.ent(), origin.ent().get<afterhours::ui::UIComponent>().rect());
                vp.curY = std::max(0.f, rect.y + scrollY - contentParent->get<afterhours::ui::UIComponent>().rect().y);
            }
        }
        if (viewportH <= 0.f)
            viewportH = contentHeight > 0
                            ? contentHeight * zoom::get()
                            : vp.screenH;
        float overscan = viewportH; // one screen of pre-built rows each side
        vp.top = scrollY - overscan;
        vp.bottom = scrollY + viewportH + overscan;
    }

    if (anchorRequest && navigation::accepts(*filterRepo, *anchorRequest, "reading-anchor")) {
        auto& state = filterRepo->reading;
        state.key += "\n" + std::to_string(contentWidth) + ":" + std::to_string(contentHeight) + ":" +
            std::to_string(sess.fontSize) + ":" + std::to_string(zoom::get()) + ":" + std::to_string(sideBySide);
        for (const auto& file : diffs) state.key += ":" + std::to_string(file.renderIdentity);
        if (review) {
            for (const auto& fold : review->foldedFiles) state.key += "\nfile:" + fold;
            for (const auto& fold : review->foldedHunks) state.key += "\nhunk:" + fold;
        }
        const auto viewport = visible_rect(*contentParent);
        const auto wheel = afterhours::input::get_mouse_wheel_move_v();
        const bool scrolling = (vp.scroll && vp.scroll->dragging_scrollbar) ||
            ((wheel.x != 0.f || wheel.y != 0.f) && afterhours::ui::is_mouse_inside(ctx.mouse.pos, viewport));
        if (scrolling) navigation::cancel_anchor(*filterRepo);
        else if (state.previousDocument == filterRepo->workspace().active_id() && state.key != state.previousKey &&
            filterRepo->fullFileNavigateFrames == 0 && filterRepo->diffTargetFrames == 0)
            navigation::restore_anchor(*filterRepo);
        const auto* document = filterRepo->workspace().document(filterRepo->workspace().active_id());
        vp.layout = &state;
        if (document->restoreAnchor && document->anchor && vp.scroll) vp.restoreAnchor = document->anchor;
    }

    struct ContextLocation { float y; const ecs::FileDiff* file; const ecs::DiffHunk* hunk; };
    std::vector<ContextLocation> contextLocations;
    if (fileOrder.empty() && !diffs.empty())
        div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap().with_label("No files match these filters")
            .with_size(ComponentSize{pixels(contentWidth), pixels(32)}).with_font_size(pixels(12)));
    for (size_t fileIndex : fileOrder) {
        auto& fileDiff = diffs[fileIndex];
        if (fileIndex != fileOrder.front()) {
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{w, pixels(14)}).with_debug_name("file_spacer"));
            vp.built(14.f);
        }
        contextLocations.push_back({vp.curY, &fileDiff, nullptr});
        std::string fileLabel = diff_detail::file_header_label(fileDiff);
        if (review) fileLabel += ecs::unresolved_file_badge(*review, reviewScope, fileDiff.filePath, fileDiff.oldPath);
        std::string fileFoldKey = reviewScope + "\n" + fileDiff.filePath;
        if (review && ((filterRepo && filterRepo->diffTargetFrames > 0 && filterRepo->diffTargetFile() == fileDiff.filePath) ||
            (sess.findMatch && sess.findMatch->file == fileDiff.filePath))) review->foldedFiles.erase(fileFoldKey);
        bool fileFolded = review && review->foldedFiles.contains(fileFoldKey);
        const bool narrowFile = contentWidth < 600.f;
        const float actionsHeight = !fileDiff.isFullContent && narrowFile ? 64.f : 32.f;
        float fileHeaderHeight = fileDiff.isFullContent ? (narrowFile ? 64.f : 40.f)
            : narrowFile ? 40.f + actionsHeight : 44.f;

        vp.flush(ctx, *contentParent, nextId);
        int fileHeaderRowId = nextId++;
        auto fileHeaderRow = div(ctx, mk(*contentParent, fileHeaderRowId),
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{w, pixels(fileHeaderHeight)})
                .with_flex_direction(contentWidth < 600.f ? FlexDirection::Column : FlexDirection::Row)
                .with_no_wrap()
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_align_items(AlignItems::Center)
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_border(theme::BORDER, pixels(1))
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(6.f)
                .with_debug_name("file_header_row"));
        if (filterRepo) bind_focus(fileHeaderRow.ent(), *filterRepo, reading::focus::Region::Code, fileDiff.filePath);
        set_tooltip(fileHeaderRow.ent(), fileLabel);
        vp.built(fileHeaderHeight);
        if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            repo && repo->diffTargetFrames > 0 && repo->diffTargetFile() == fileDiff.filePath &&
            contentParent->has<afterhours::ui::HasScrollView>()) {
            auto& scroll = contentParent->get<afterhours::ui::HasScrollView>();
            float target = vp.curY - vp.px(fileHeaderHeight);
            target = std::clamp(target, 0.f, std::max(0.f, scroll.content_size.y - scroll.viewport_or_zero().y));
            scroll.scroll_offset = scroll.scroll_target = scroll.last_eased_offset = {0.f, target};
            vp.top = target - scroll.viewport_or_zero().y;
            vp.bottom = target + scroll.viewport_or_zero().y * 2.f;
            --repo->diffTargetFrames;
        }

        if (!vp.active || (vp.curY >= vp.top && vp.curY - vp.px(fileHeaderHeight) <= vp.bottom)) {
            bool showApproveFile = review && !fileDiff.isFullContent;
            auto fileTitle = div(ctx, mk(fileHeaderRow.ent(), 0), ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{contentWidth < 600.f ? percent(1.f) : expand(), pixels(32)})
                .with_flex_direction(FlexDirection::Row).with_no_wrap().with_align_items(AlignItems::Center)
                .with_overflow(Overflow::Hidden));
            if (review && !fileDiff.isFullContent) {
                auto fold = button(ctx, mk(fileTitle.ent(), 1), preset::Button("")
                    .with_size(ComponentSize{pixels(28), pixels(28)})
                    .with_padding(Padding{.top = pixels(6), .right = pixels(6), .bottom = pixels(6), .left = pixels(6)})
                    .with_transparent_bg().with_debug_name("fold_file:" + fileDiff.filePath));
                chrome_icon(ctx, mk(fold.ent(), 0), fileFolded ? ChromeIcon::ChevronRight : ChromeIcon::ChevronDown,
                    theme::TEXT_SECONDARY, "file_fold_icon");
                set_tooltip(fold.ent(), fileFolded ? "Expand file" : "Collapse file");
                if (fold) {
                    if (fileFolded) review->foldedFiles.erase(fileFoldKey);
                    else review->foldedFiles.insert(fileFoldKey);
                    fileFolded = !fileFolded;
                }
            }
            const auto directoryEnd = fileDiff.filePath.find_last_of('/');
            const auto basenameStart = directoryEnd == std::string::npos ? 0 : directoryEnd + 1;
            std::vector<afterhours::ui::TextSpan> pathSpans{
                {fileDiff.filePath.substr(0, basenameStart), theme::TEXT_TERTIARY},
                {fileDiff.filePath.substr(basenameStart), theme::TEXT_PRIMARY}};
            div(ctx, mk(fileTitle.ent(), 0),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_styled_label(std::move(pathSpans))
                    .with_size(ComponentSize{afterhours::ui::expand(), percent(1.0f)})
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font("mono", pixels(15))
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_padding(Padding{
                        .right = pixels(4), .left = pixels(8)})
                    .with_debug_name("file_header_label"));

            auto fileBtns = div(ctx, mk(fileHeaderRow.ent(), 1),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_size(ComponentSize{narrowFile ? pixels(contentWidth - 24.f) : children(), pixels(actionsHeight)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_align_items(AlignItems::Center)
                    .with_wrap()
                    .with_gap(pixels(6))
                    .with_margin(Margin{.right = pixels(12)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                .with_debug_name("file_header_btns"));
            if (!fileDiff.isFullContent) {
                std::string stateLabel = fileDiff.isPartialContent ? "Partial preview" : fileDiff.isRenamed ? "Renamed" : fileDiff.isNew ? "New file" :
                    fileDiff.isDeleted ? "Deleted" : fileDiff.isBinary ? "Binary" : "";
                const auto unresolved = review ? ecs::unresolved_file_count(*review, reviewScope,
                    fileDiff.filePath, fileDiff.oldPath) : 0;
                if (unresolved) stateLabel += (stateLabel.empty() ? "" : " · ") + std::to_string(unresolved) + " unresolved";
                if (!stateLabel.empty())
                    div(ctx, mk(fileBtns.ent(), 9), ComponentConfig{}.with_skip_grid_snap()
                        .with_label(stateLabel).with_size(ComponentSize{children(), pixels(28)})
                        .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_debug_name("file_state_badge"));
                div(ctx, mk(fileBtns.ent(), 7), ComponentConfig{}.with_skip_grid_snap()
                    .with_label("+" + std::to_string(fileDiff.additions))
                    .with_size(ComponentSize{children(), pixels(28)}).with_font("mono", pixels(12))
                    .with_custom_text_color(theme::DIFF_ADD_TEXT).with_debug_name("file_additions"));
                div(ctx, mk(fileBtns.ent(), 8), ComponentConfig{}.with_skip_grid_snap()
                    .with_label("-" + std::to_string(fileDiff.deletions))
                    .with_size(ComponentSize{children(), pixels(28)}).with_font("mono", pixels(12))
                    .with_custom_text_color(theme::DIFF_DEL_TEXT).with_debug_name("file_deletions"));
            }
            if (sess.reviewActions && diff_sel::state().hasSel) {
                std::vector<std::pair<int, int>> selectedLines;
                const auto& selection = diff_sel::state();
                for (const auto& line : selection.lastLines) {
                    auto span = selection.hl.find(line.ent);
                    if (line.filePath == fileDiff.filePath && span != selection.hl.end() && span->second.second > span->second.first)
                        selectedLines.emplace_back(line.oldLine, line.newLine);
                }
                if (!selectedLines.empty()) {
                    if (button(ctx, mk(fileBtns.ent(), 6), preset::Button("Comment selection")
                            .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12)).with_transparent_bg()
                            .with_debug_name("comment_selection_btn"))) {
                        auto range = review_selection::range(selectedLines);
                        if (!range) afterhours::toast::send_info(ctx, "Select lines from one side of the diff", 2.f);
                        else {
                            for (const auto& hunk : fileDiff.hunks) {
                                int first = range->oldSide ? hunk.oldStart : hunk.newStart;
                                int count = range->oldSide ? hunk.oldCount : hunk.newCount;
                                if (range->first < first || range->first >= first + count) continue;
                                begin_diff_comment(*review, reviewScope + "\n" + ecs::ReviewComponent::hunk_key(fileDiff.filePath, hunk),
                                    {reviewScope, range->oldSide && !fileDiff.oldPath.empty() ? fileDiff.oldPath : fileDiff.filePath,
                                     range->first, "", range->last, range->oldSide}, hunk);
                                review->foldedHunks.erase(review->composingKey);
                                diff_sel::reset();
                                break;
                            }
                        }
                    }
                }
            }
            auto* activeRepo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if ((!activeRepo || !activeRepo->reviewWorkspace) && reviewScope == "wt" && !fileDiff.isFullContent && !fileDiff.isRenamed &&
                !fileDiff.isSubmodule && diff_sel::state().hasSel) {
                auto stage = button(ctx, mk(fileBtns.ent(), 3), preset::Button("Stage selection")
                    .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12)).with_transparent_bg()
                    .with_debug_name("stage_selected_lines"));
                if (stage) {
                    std::vector<std::set<size_t>> selected(fileDiff.hunks.size());
                    const auto& state = diff_sel::state();
                    for (size_t h = 0; h < fileDiff.hunks.size(); ++h) {
                        const auto& hunk = fileDiff.hunks[h];
                        int oldLine = hunk.oldStart, newLine = hunk.newStart;
                        for (size_t i = 0; i < hunk.lines.size(); ++i) {
                            char sign = hunk.lines[i].empty() ? ' ' : hunk.lines[i].front();
                            int number = sign == '-' ? oldLine : newLine;
                            for (const auto& record : state.lastLines) {
                                if (record.filePath == fileDiff.filePath && record.sign == sign &&
                                    record.lineNo == number && state.hl.contains(record.ent))
                                    selected[h].insert(i);
                            }
                            if (sign != '+') ++oldLine;
                            if (sign != '-') ++newLine;
                        }
                    }
                    auto result = git::stage_selected_lines(repoPath, fileDiff, selected);
                    if (result.success()) {
                        diff_sel::reset();
                        if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) repo->refreshRequested = true;
                        afterhours::toast::send_info(ctx, "Selected lines staged", 1.5f);
                    } else afterhours::toast::send_info(ctx, "Stage selection failed: " + result.stderr_str(), 3.f);
                }
            }

            if (!fileDiff.isFullContent && !repoPath.empty() && reviewScope != "snapshot") {
                auto open = button(ctx, mk(fileBtns.ent(), 2), preset::Button(fileDiff.isDeleted ? "Open previous file" : "Open file")
                    .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
                    .with_transparent_bg().with_debug_name("open_full_file"));
                if (open) {
                    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) {
                        navigation::open_source(*repo, fileDiff,
                            diff_sel::source_point(fileDiff, visible_rect(*contentParent), reviewScope));
                    }
                }
            }

            if (showApproveFile) {
                bool viewed = ecs::file_reviewed(*review, reviewScope, fileDiff);
                auto approveFileBtn = button(ctx, mk(fileBtns.ent(), 0),
                    preset::Button("", !fileDiff.isPartialContent)
                        .with_size(ComponentSize{pixels(78), pixels(28)})
                        .with_padding(Padding{
                            .top = pixels(6), .right = pixels(4),
                            .bottom = pixels(6), .left = pixels(4)})
                        .with_flex_direction(FlexDirection::Row).with_gap(pixels(5)).with_no_wrap()
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_font_size(pixels(12))
                        .with_debug_name("approve_file_btn"));
                if (fileDiff.isPartialContent) set_tooltip(approveFileBtn.ent(),
                    "Only part of this file is loaded. A preview cannot mark the whole file reviewed.");
                auto checkbox = div(ctx, mk(approveFileBtn.ent(), 0), ComponentConfig{}
                    .with_size(ComponentSize{pixels(16), pixels(16)})
                    .with_border(viewed ? theme::DIFF_ADD_TEXT : theme::TEXT_TERTIARY, pixels(1))
                    .with_debug_name("viewed_checkbox"));
                if (viewed) chrome_icon(ctx, mk(checkbox.ent(), 0), ChromeIcon::Check, theme::DIFF_ADD_TEXT, "viewed_check");
                div(ctx, mk(approveFileBtn.ent(), 1), ComponentConfig{}.with_label("Viewed")
                    .with_size(ComponentSize{expand(), pixels(16)}).with_font_size(pixels(12))
                    .with_custom_text_color(viewed ? theme::DIFF_ADD_TEXT : theme::TEXT_SECONDARY));
                if (approveFileBtn) {
                    if (viewed) {
                        review->reviewedFiles.erase(reviewScope + "\n" + fileDiff.filePath);
                        for (const auto& hunk : fileDiff.hunks)
                            review->approvedHunks.erase(reviewScope + "\n" + ecs::ReviewComponent::hunk_key(fileDiff.filePath, hunk));
                    } else {
                        review->reviewedFiles[reviewScope + "\n" + fileDiff.filePath] = ecs::diff_signature(fileDiff);
                        for (const auto& hunk : fileDiff.hunks)
                            review->approvedHunks.insert(reviewScope + "\n" + ecs::ReviewComponent::hunk_key(fileDiff.filePath, hunk));
                    }
                    review->dirty = true;
                    afterhours::toast::send_info(ctx, viewed ? "File marked unreviewed; index unchanged" : "File approved for review; index unchanged", 1.5f);
                }
            }
            if (reviewScope == "wt" && !fileDiff.isFullContent && (!activeRepo || !activeRepo->reviewWorkspace)) {
                if (button(ctx, mk(fileBtns.ent(), 4), preset::Button("Stage file")
                        .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12))
                        .with_transparent_bg().with_debug_name("stage_file_btn"))) {
                    auto result = git::stage_file(repoPath, fileDiff.filePath);
                    if (result.success()) {
                        if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) repo->refreshRequested = true;
                    } else afterhours::toast::send_info(ctx, "Stage failed: " + diff_detail::git_err(result), 2.5f);
                }
            }

            {
                std::string diffText = diff_detail::file_diff_to_text(fileDiff);
                if (fileDiff.isFullContent) {
                    diffText.clear();
                    for (const auto& hunk : fileDiff.hunks)
                        for (size_t i = 0; i < hunk.lines.size(); ++i) {
                            diffText += hunk.lines[i].substr(1);
                            if (!hunk.noNewline.contains(i)) diffText += '\n';
                        }
                }
                auto fileCopyBtn = button(ctx, mk(fileBtns.ent(), 1),
                    preset::Button(fileDiff.isPartialContent ? "Copy loaded page" : fileDiff.isFullContent ? "Copy file" : "Copy Diff")
                        .with_size(ComponentSize{children(), pixels(28)})
                        .with_padding(Padding{
                            .top = pixels(2), .right = pixels(8),
                            .bottom = pixels(2), .left = pixels(8)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_font_size(pixels(12))
                        .with_debug_name("copy_file_diff_btn"));
                if (fileCopyBtn) {
                    afterhours::clipboard::set_text(diffText);
                    afterhours::toast::send_info(ctx, fileDiff.isPartialContent ? "Copied loaded page to clipboard" : "Copied diff to clipboard", 1.5f);
                }
            }

        }

        if (fileFolded) {
            if (vp.layout) vp.layout->rows.push_back({fileHeaderRow.ent().id, fileDiff.filePath,
                1, std::numeric_limits<int>::max(), 1, std::numeric_limits<int>::max(),
                1, std::numeric_limits<int>::max(), true});
            continue;
        }

        if (std::any_of(fileDiff.hunks.begin(), fileDiff.hunks.end(), [](const auto& hunk) { return !hunk.movedLines.empty(); })) {
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                .with_label("Exact moved blocks are blue · addition and deletion signs are preserved")
                .with_size(ComponentSize{w, pixels(28)}).with_font_size(pixels(12))
                .with_custom_text_color(afterhours::Color{125, 180, 255, 255}).with_debug_name("moved_code_legend"));
            vp.built(28.f);
        }
        if (ownerRepo && reviewScope != "snapshot" && !ownerRepo->codeownersDocument.path.empty()) {
            if (!vp.visible(28.f)) {
                vp.skipped(28.f);
            } else {
            const auto& document = ownerRepo->codeownersDocument;
            auto& cache = ownerRepo->codeownersByPath;
            if (cache.size() >= 512) cache.clear();
            auto [owner, inserted] = cache.try_emplace(fileDiff.filePath);
            if (inserted) owner->second = codeowners::owners_for(document, fileDiff.filePath);
            const auto& owners = owner->second;
            auto label = document.error.empty() ? "Owners: " + (owners.empty() ? "no matching owners" : owners) + " · " + document.path : document.error;
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                .with_label(label).with_size(ComponentSize{w, pixels(28)})
                .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
                .with_debug_name("codeowners_label"));
            vp.built(28.f);
            }
        }
        if (fileDiff.hunks.size() == 1) {
            const auto& hunk = fileDiff.hunks.front();
            for (bool oldSide : {true, false}) {
                if ((fileDiff.isFullContent && oldSide) || (oldSide ? hunk.oldStart : hunk.newStart) != 1) continue;
                std::string pointerText;
                for (const auto& line : hunk.lines) {
                    if (!line.empty() && line.front() != (oldSide ? '+' : '-')) pointerText += line.substr(1) + "\n";
                    if (pointerText.size() > 1024) break;
                }
                if (pointerText.size() > 1024) continue;
                if (auto pointer = lfs_pointer::parse(pointerText)) {
                    const auto* lfsRepo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
                    std::string label = std::string("Git LFS asset · ") + (oldSide ? "Before: " : "After: ") +
                        std::to_string(pointer->size) + " bytes · SHA-256 " + pointer->oid + "\n" +
                        (lfs_pointer::cached_availability(repoPath, *pointer, lfsRepo ? lfsRepo->dataGeneration : 0) ? "Available in default local LFS cache" : "Not present in default local LFS cache") +
                        " · pointer shown; no asset download";
                    vp.flush(ctx, *contentParent, nextId);
                    div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                        .with_label(label).with_size(ComponentSize{w, pixels(48)})
                        .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
                        .with_custom_text_color(theme::TEXT_SECONDARY).with_debug_name("lfs_asset_metadata"));
                    vp.built(48.f);
                }
            }
        }
        if (fileDiff.oldMode == "120000" || fileDiff.newMode == "120000") {
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                .with_label("Symbolic link target · target text only, link not followed")
                .with_size(ComponentSize{w, pixels(28)}).with_font_size(pixels(12))
                .with_custom_text_color(theme::TEXT_SECONDARY).with_debug_name("symlink_target_notice"));
            vp.built(28.f);
        }
        if (fileDiff.oldMode != fileDiff.newMode) {
            std::string modeLabel = "File mode: " + (fileDiff.oldMode.empty() ? "absent" : fileDiff.oldMode) +
                " -> " + (fileDiff.newMode.empty() ? "absent" : fileDiff.newMode);
            if (fileDiff.oldMode == "100644" && fileDiff.newMode == "100755") modeLabel += " · executable enabled";
            if (fileDiff.oldMode == "100755" && fileDiff.newMode == "100644") modeLabel += " · executable removed";
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                .with_label(modeLabel).with_size(ComponentSize{w, pixels(28)})
                .with_font_size(pixels(12)).with_custom_text_color(theme::STATUS_MODIFIED)
                .with_debug_name("file_mode_change"));
            vp.built(28.f);
        }

        // Binary files: just show the header, no hunks
        if (fileDiff.isBinary) {
            vp.flush(ctx, *contentParent, nextId);
            if (image_diff::render(ctx, *contentParent, nextId++, fileDiff, repoPath, reviewScope, contentWidth)) {
                vp.built(300.f);
                continue;
            }
            div(ctx, mk(*contentParent, nextId++),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_size(ComponentSize{w, pixels(24)})
                    .with_custom_background(theme::PANEL_BG)
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_label("Binary file not shown")
                    .with_font_size(pixels(14))
                    .with_alignment(TextAlignment::Center)
                    .with_padding(Padding{
                        .top = pixels(4), .right = pixels(8),
                        .bottom = pixels(4), .left = pixels(8)})
                    .with_roundness(0.0f)
                    .with_debug_name("binary_notice"));
            vp.built(24.0f);
            continue;
        }

        // Render each hunk (passing contentWidth for proper sizing)
        for (auto& hunk : fileDiff.hunks) {
            float hunkY = vp.curY;
            render_hunk(ctx, *contentParent, fileDiff, hunk, nextId,
                        contentWidth, &sess, &vp, sideBySide, codeWidth);
            if (vp.curY > hunkY) contextLocations.push_back({hunkY, &fileDiff, &hunk});
        }

        if (!fileDiff.isFullContent) {
            const auto type = file_tree_style::type_marker(fileDiff.filePath);
            const std::string language = type == "H" ? "C++" : type == "PY" ? "Python" :
                type == "TS" ? "TypeScript" : type == "JS" ? "JavaScript" : type == "MD" ? "Markdown" :
                type == "{}" ? "JSON" : type == "<>" ? "Markup" : type == "·" ? "Text" : type;
            std::string footer = "Up to " + std::to_string(filterRepo ? filterRepo->diffContext : 3) +
                " lines of context · " + language;
            if (fileDiff.isRenamed) footer += " · Renamed from " + fileDiff.oldPath;
            else if (fileDiff.isNew) footer += " · New file";
            else if (fileDiff.isDeleted) footer += " · Deleted file";
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                .with_label(footer).with_size(ComponentSize{w, pixels(28)})
                .with_padding(Padding{.left = pixels(12), .right = pixels(12)})
                .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_TERTIARY)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_border_top(theme::BORDER, pixels(1)).with_debug_name("file_context_footer"));
            vp.built(28.f);
        }
    }
    if (filterable && !fileOrder.empty()) {
        const auto& candidates = filterRepo && reviewScope == "wt" ? filterRepo->currentDiff :
            filterRepo && reviewScope == "index" ? filterRepo->stagedDiff : diffs;
        auto progress = review ? ecs::review_progress(*review, reviewScope, candidates) : ecs::ReviewProgress{};
        std::string end = reviewScope == "wt" || reviewScope == "index" || reviewScope == "snapshot" ? "End of changes" : "End of commit";
        if (review) end += " · " + std::to_string(progress.reviewed) + " of " + std::to_string(progress.total) + " files viewed";
        vp.flush(ctx, *contentParent, nextId);
        div(ctx, mk(*contentParent, nextId++), ComponentConfig{}.with_skip_grid_snap()
            .with_label(end).with_size(ComponentSize{w, pixels(40)})
            .with_font_size(pixels(12)).with_alignment(TextAlignment::Center)
            .with_custom_text_color(theme::TEXT_TERTIARY).with_debug_name("diff_reading_end"));
        vp.built(40.f);
    }

    // Flush any trailing skipped rows so the content_size (scrollbar extent)
    // reflects the full diff height, not just what was built.
    vp.flush(ctx, *contentParent, nextId);

    if (stickyHeight > 0.f && !contextLocations.empty()) {
        auto current = contextLocations.front();
        float scrollY = vp.scroll ? vp.scroll->scroll_offset.y : 0.f;
        for (const auto& location : contextLocations) {
            if (location.y > scrollY) break;
            current = location;
        }
        std::string label = diff_detail::file_header_label(*current.file);
        if (current.hunk) label += "   " + current.hunk->header;
        if (scrollY <= contextLocations.front().y) label.clear();
        auto stickyLabel = div(ctx, mk(stickyHost.ent(), 0), ComponentConfig{}.with_skip_grid_snap().with_label(label)
            .with_size(ComponentSize{w, pixels(stickyHeight)}).with_font_size(pixels(12))
            .with_padding(Padding{.left = pixels(8), .right = pixels(8)})
            .with_custom_text_color(theme::TEXT_PRIMARY).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_debug_name("sticky_diff_context"));
        set_tooltip(stickyLabel.ent(), label);
    }

    if (anchorRequest && navigation::accepts(*filterRepo, *anchorRequest, "reading-anchor") &&
        vp.restoreAnchor && vp.nearestAnchor) {
        filterRepo->reading.projectedLine = vp.nearestAnchor->line;
        if (!vp.restoredAnchor) vp.restore_at(vp.nearestAnchor->y);
    }

    // This frame's registry becomes next frame's hit-test source.
    if (selEnabled) {
        diff_sel::state().lastLines = std::move(diff_sel::state().curLines);
    }
    // Record visible-hunk count for keyboard cursor clamping.
    if (review) review->hunkCount = sess.hunkOrdinal;
}

// Backward-compatible entry points. render_inline_diff keeps its original
// signature so existing callers (commit detail) are unaffected.
inline void render_inline_diff(UIContext<InputAction>& ctx,
                               Entity& parent,
                               const std::vector<ecs::FileDiff>& diffs,
                               float contentWidth, float contentHeight,
                               bool embedInParentScroll = false,
                               bool resetScroll = false,
                               const std::string& repoPath = "",
                               ecs::ReviewComponent* review = nullptr,
                               const std::string& reviewScope = "wt") {
    render_diff(ctx, parent, diffs, contentWidth, contentHeight,
                embedInParentScroll, resetScroll, /*sideBySide=*/false,
                repoPath, review, reviewScope);
}

inline void render_side_by_side_diff(UIContext<InputAction>& ctx,
                                     Entity& parent,
                                     const std::vector<ecs::FileDiff>& diffs,
                                     float contentWidth, float contentHeight,
                                     bool embedInParentScroll = false,
                                     bool resetScroll = false,
                                     const std::string& repoPath = "") {
    render_diff(ctx, parent, diffs, contentWidth, contentHeight,
                embedInParentScroll, resetScroll, true, repoPath);
}

} // namespace ui
