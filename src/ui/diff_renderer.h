#pragma once

#include "../git/source_find.h"
#include "../git/selection_copy.h"

#include "../util/reading_anchor.h"
#include "../util/code_position.h"
#include "../util/code_words.h"
#include "../util/code_motion.h"
#include "../util/hunk_syntax.h"
#include "diff_syntax.h"
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
#include "hunk_context.h"
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
    if (repo) {
        const auto& selection = repo->workspace().document(repo->workspace().active_id())->selection;
        if (selection && selection->anchor.path == location.file)
            if (const auto range = review_selection::range(*selection)) {
                location.line = range->first;
                location.endLine = range->last;
                location.oldSide = range->oldSide;
            }
    }
    ecs::begin_comment(review, key, ecs::comment_with_context(std::move(location), hunk,
        repo ? repo->headCommitHash : ""));
}

struct PreparedCode {
    std::shared_ptr<const std::vector<code_highlight::Token>> tokens;
    size_t offset = 0;
    int column = 1;
};

inline std::vector<afterhours::ui::TextSpan> highlighted_code(
    const std::string& prefix, const std::string& content, const std::string& path,
    bool visibleWhitespace = false, bool hasNewline = true,
    const std::string* original = nullptr, size_t offset = 0, bool finalFragment = true, const std::string& ending = "", const PreparedCode* prepared = nullptr, bool activeGutter = false) {
    std::vector<afterhours::ui::TextSpan> spans{{prefix, activeGutter ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY}};
    const auto& source = original ? *original : content;
    auto tokens = prepared ? prepared->tokens : code_highlight::token_cache().get(code_highlight::display_text(source, visibleWhitespace), path);
    size_t begin = prepared ? prepared->offset : code_highlight::display_size(std::string_view(source).substr(0, offset), visibleWhitespace);
    size_t end = begin + code_highlight::display_size(content, visibleWhitespace);
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
    bool finalFragment = true;
};

inline int number_on_side(const Rec& row, reading::DiffSide side) {
    if (side == reading::DiffSide::Before) return row.side == 2 ? 0 : row.oldLine;
    return row.side == 1 ? 0 : row.newLine;
}

struct State {
    bool dragging = false;
    bool hasSel = false;
    int clickCount = 0;
    double clickTime = -1.;
    float clickX = 0.f, clickY = 0.f;
    bool extending = false;
    reading::SourceDestination source;
    std::string sourceIdentity;
    unsigned sourceGeneration = 0;
    std::optional<float> preferredX;
    std::string context;
    reading::CodePosition anchor, head;
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
    s.clickCount = 0;
    s.clickTime = -1.;
    s.extending = false;
    s.preferredX.reset();
    s.anchor = {};
    s.head = {};
    s.lastLines.clear();
    s.curLines.clear();
    s.hl.clear();
}

struct Session {
    ecs::RepoComponent* owner = nullptr;
    const std::vector<ecs::FileDiff>* files = nullptr;
    std::optional<reading::CodePosition> caret;
    bool codeFocused = false;
    int sourceStartLine = 0;
    int sourceStartColumn = 1;
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

inline std::vector<size_t> wrapped_rows(const Session& session, const std::string& text, float available, bool newline, const std::string& identity) {
    auto breaks = diff_metrics().wraps(text, available, session.fontSize, session.visibleWhitespace,
        [&](std::string_view glyph) { return code_mw(session, std::string(glyph)); }, identity);
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

inline reading::CodePosition code_position(const Rec& row, int byte, std::optional<reading::DiffSide> requestedSide = {}) {
    const auto side = requestedSide.value_or(row.side == 1 || row.sign == '-' ? reading::DiffSide::Before : reading::DiffSide::After);
    return {row.filePath, side, number_on_side(row, side), row.logicalColumn + reading::column_at_byte(row.content, static_cast<size_t>(std::max(0, byte))) - 1};
}

inline std::optional<Pos> rendered_position(const reading::CodePosition& position, const std::vector<Rec>& rows) {
    for (const auto& row : rows) {
        if (auto byte = reading::caret_byte(position, row.filePath, position.side, number_on_side(row, position.side), row.content, row.logicalColumn, row.finalFragment))
            return Pos{row.ent, static_cast<int>(*byte)};
    }
    return {};
}

// Resolve anchor/head into an ordered span (i1,c1) <= (i2,c2) as indices into
// `lines`. Returns false if either endpoint's line is no longer present.
inline bool ordered_span(const std::vector<Rec>& lines, reading::CodePosition anchor, reading::CodePosition head,
                         int& i1, int& c1, int& i2, int& c2) {
    const auto a = rendered_position(anchor, lines);
    const auto h = rendered_position(head, lines);
    if (!a || !h || anchor.path != head.path || anchor.side != head.side) return false;
    int ai = -1, hi = -1;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (lines[i].ent == a->ent) ai = i;
        if (lines[i].ent == h->ent) hi = i;
    }
    if (ai < 0 || hi < 0) return false;
    i1 = ai; c1 = a->col; i2 = hi; c2 = h->col;
    if (i1 > i2 || (i1 == i2 && c1 > c2)) { std::swap(i1, i2); std::swap(c1, c2); }
    return true;
}

inline void recompute_highlight(State& st) {
    st.hl.clear();
    st.hasSel = st.anchor.path == st.head.path && st.anchor.side == st.head.side && st.anchor != st.head;
    if (!st.hasSel) return;
    auto first = st.anchor, last = st.head;
    if (std::tie(first.line, first.column) > std::tie(last.line, last.column)) std::swap(first, last);
    for (const auto& row : st.lastLines) {
        const int line = number_on_side(row, first.side);
        if (row.filePath != first.path || line < first.line || line > last.line) continue;
        const int a = line == first.line ? std::max(1, first.column - row.logicalColumn + 1) : 1;
        const int b = line == last.line ? std::max(1, last.column - row.logicalColumn + 1) : INT_MAX;
        auto begin = reading::byte_at_column(row.content, a);
        auto end = reading::byte_at_column(row.content, b);
        if (begin < end) st.hl[row.ent] = {static_cast<int>(begin), static_cast<int>(end)};
    }
}

// Build the clipboard text for the current selection. Optionally prepends a
// "path:Lstart[-Lend]" location so it's ready to paste into an AI review chat.
inline std::string build_copy_text(State& st, bool withLocation) {
    if (!st.hasSel) return {};
    int i1, c1, i2, c2;
    if (!ordered_span(st.lastLines, st.anchor, st.head, i1, c1, i2, c2)) return "";
    std::string out;
    if (withLocation) {
        const Rec& r = st.lastLines[i1];
        int firstNo = number_on_side(r, st.anchor.side);
        int endNo = number_on_side(st.lastLines[i2], st.anchor.side);
        out += r.filePath + ":L" + std::to_string(firstNo);
        if (endNo != firstNo) out += "-" + std::to_string(endNo);
        out += "\n";
    }
    bool emitted = false;
    int previous = i1;
    for (int k = i1; k <= i2; ++k) {
        if (st.lastLines[k].side != st.lastLines[i1].side ||
            st.lastLines[k].filePath != st.lastLines[i1].filePath ||
            (number_on_side(st.lastLines[k], st.anchor.side) == 0)) continue;
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

inline std::string copy_selection(bool withLocation) {
    auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
    if (!repo) return "No selection to copy";
    const auto& document = *repo->workspace().document(repo->workspace().active_id());
    if (!document.selection || document.selection->anchor == document.selection->head) return "No selection to copy";
    const auto& selection = *document.selection;
    if (selection.dataGeneration != repo->dataGeneration &&
        (std::holds_alternative<reading::WorkingTree>(selection.source.revision) || std::holds_alternative<reading::Index>(selection.source.revision)))
        return "Source changed; select the text again";
    auto& runtime = repo->selectionCopy;
    runtime = {};
    runtime.selection = selection;
    runtime.withLocation = withLocation;
    runtime.request = navigation::stamp(*repo, "selection-copy");
    runtime.future = git::copy_source_selection_async({repo->repoPath, selection.source.path,
        reading::revision_text(selection.source.revision), {}, repo->workspace().source() ? repo->fullFileEncodingOverride : "auto"}, selection, withLocation);
    return {};
}

inline void poll_copy(UIContext<InputAction>& ctx, ecs::RepoComponent& repo) {
    auto& runtime = repo.selectionCopy;
    if (!runtime.future.valid() || runtime.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto result = runtime.future.get();
    const auto& selection = repo.workspace().document(repo.workspace().active_id())->selection;
    if (!navigation::accepts(repo, runtime.request, "selection-copy") || !selection || *selection != runtime.selection) return;
    if (!result.error.empty()) afterhours::toast::send_info(ctx, result.error, 4.f);
    else {
        afterhours::clipboard::set_text(result.text);
        afterhours::toast::send_info(ctx, runtime.withLocation ? "Copied selection with location" : "Copied selection", 1.5f);
    }
}

inline void remember_selection(const Session& session) {
    if (!session.owner) return;
    auto& st = state();
    if (st.hasSel || st.extending)
        navigation::set_selection(*session.owner, reading::CodeSelection{st.anchor, st.head, st.source, st.sourceIdentity, st.sourceGeneration});
    else navigation::set_selection(*session.owner, {});
}

inline void bind_selection_source(const Session& session) {
    if (!session.owner) return;
    auto& st = state();
    auto& repo = *session.owner;
    st.sourceGeneration = repo.dataGeneration;
    st.sourceIdentity.clear();
    if (const auto* source = repo.workspace().source()) {
        st.source = source->destination;
        st.sourceIdentity = repo.fullFilePage.sourceIdentity;
    } else if (session.files) {
        const auto file = std::find_if(session.files->begin(), session.files->end(), [&](const auto& value) { return value.filePath == st.anchor.path; });
        if (file != session.files->end()) {
            reading::ReadingAnchor point{st.anchor.path, session.reviewScope, st.anchor.side, st.anchor.line, st.anchor.column};
            st.source = reading::source_at_diff(repo.workspace().review(), *file, point).destination;
        }
    }
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
            if (!ctx.is_input_allowed(st.lastLines[i].ent)) continue;
            auto entity = afterhours::ui::UICollectionHolder::getEntityForID(st.lastLines[i].ent);
            if (!entity.valid() || !entity->has<afterhours::ui::UIComponent>()) continue;
            const auto rc = ui::visible_rect(**entity);
            if (sess.owner && sess.owner->workspace().source() &&
                mx < ui::screen_rect(**entity).x + code_mw(sess, "  ")) continue;
            if (rc.width > 0.f && rc.height > 0.f && mx >= rc.x && mx <= rc.x + rc.width &&
                my >= rc.y && my <= rc.y + rc.height) return i;
        }
        return -1;
    };
    auto nearestLine = [&]() -> int {
        int nn = -1; float bd = 1e30f;
        for (int i = 0; i < (int)st.lastLines.size(); ++i) {
            if (st.lastLines[i].filePath != st.anchor.path || number_on_side(st.lastLines[i], st.anchor.side) == 0) continue;
            const Rectangle& rc = st.lastLines[i].rect;
            float d = std::fabs(my - (rc.y + rc.height / 2.f));
            if (d < bd) { bd = d; nn = i; }
        }
        return nn;
    };

    if (mouse.just_pressed) {
        st.extending = false;
        st.preferredX.reset();
        const int li = lineUnder();
        if (li >= 0) {
            const auto& row = st.lastLines[li];
            const Pos hit{row.ent, colAt(row)};
            auto position = code_position(row, hit.col);
            const bool shift = afterhours::input::is_key_down(340) || afterhours::input::is_key_down(344);
            const double now = afterhours::graphics::get_time();
            const float tolerance = 4.f * zoom::get();
            const bool repeated = !shift && st.clickTime >= 0. && now - st.clickTime < .4 &&
                std::abs(mx - st.clickX) <= tolerance && std::abs(my - st.clickY) <= tolerance;
            st.clickCount = repeated ? st.clickCount + 1 : 1;
            st.clickTime = now;
            st.clickX = mx;
            st.clickY = my;
            if (st.clickCount >= 2) {
                auto sameLine = [&](const Rec& candidate) {
                    return candidate.filePath == row.filePath && candidate.side == row.side &&
                        candidate.sign == row.sign && candidate.lineNo == row.lineNo;
                };
                std::vector<int> fragments;
                std::string text;
                size_t hitOffset = 0;
                for (int i = 0; i < static_cast<int>(st.lastLines.size()); ++i) {
                    if (!sameLine(st.lastLines[i])) continue;
                    if (i == li) hitOffset = text.size() + static_cast<size_t>(hit.col);
                    fragments.push_back(i);
                    text += st.lastLines[i].content;
                }
                auto resolve = [&](size_t byte) {
                    for (int i : fragments) {
                        const auto& fragment = st.lastLines[i];
                        if (byte <= fragment.content.size()) return Pos{fragment.ent, static_cast<int>(byte)};
                        byte -= fragment.content.size();
                    }
                    const auto& last = st.lastLines[fragments.back()];
                    return Pos{last.ent, static_cast<int>(last.content.size())};
                };
                const auto range = st.clickCount == 2 ? reading::word_at(text, hitOffset)
                    : std::pair<size_t, size_t>{0, text.size()};
                const auto start = resolve(range.first), end = resolve(range.second);
                auto locate = [&](Pos point) {
                    const auto found = std::find_if(st.lastLines.begin(), st.lastLines.end(), [&](const Rec& value) { return value.ent == point.ent; });
                    return code_position(*found, point.col);
                };
                st.anchor = locate(start);
                st.head = locate(end);
                bind_selection_source(sess);
                st.dragging = false;
                if (st.clickCount >= 3) st.clickCount = 0;
            } else {
                if (!shift || st.anchor.path != position.path || number_on_side(row, st.anchor.side) == 0) {
                    st.anchor = position;
                    bind_selection_source(sess);
                } else position = code_position(row, hit.col, st.anchor.side);
                st.head = position;
                st.dragging = true;
            }
            st.hasSel = !(st.anchor == st.head);
            if (sess.owner && st.head.line > 0) navigation::set_caret(*sess.owner, st.head, true);
        } else {
            st.clickCount = 0;
            st.clickTime = -1.;
        }
    }
    if (st.dragging && sess.owner && !ctx.is_input_allowed(sess.owner->reading.entity)) st.dragging = false;
    if (mouse.left_down && st.dragging && sess.owner) {
        auto entity = afterhours::ui::UICollectionHolder::getEntityForID(sess.owner->reading.entity);
        if (entity.valid() && entity->has<afterhours::ui::HasScrollView>()) {
            const auto viewport = ui::visible_rect(**entity);
            const float edge = 32.f * zoom::get();
            float velocity = my < viewport.y + edge ? -1.f : my > viewport.y + viewport.height - edge ? 1.f : 0.f;
            if (velocity != 0.f && viewport.height > edge * 2) {
                auto& scroll = entity->get<afterhours::ui::HasScrollView>();
                float target = std::clamp(scroll.scroll_offset.y + velocity * 600.f * zoom::get() *
                    std::min(.05f, afterhours::graphics::get_frame_time()), 0.f, std::max(0.f, scroll.content_size.y - viewport.height));
                if (target != scroll.scroll_offset.y) {
                    navigation::cancel_anchor(*sess.owner);
                    scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = target;
                }
            }
            my = std::clamp(my, viewport.y + 1.f, viewport.y + std::max(1.f, viewport.height - 1.f));
        }
    }
    if (mouse.left_down && st.dragging) {
        int li = nearestLine();
        if (li >= 0) {
            st.head = code_position(st.lastLines[li], colAt(st.lastLines[li]), st.anchor.side);
            if (sess.owner && st.head.line > 0) navigation::set_caret(*sess.owner, st.head);
            if (!(st.head == st.anchor)) st.hasSel = true;
        }
    }
    if (mouse.just_released) {
        st.dragging = false;
        if (st.head == st.anchor) st.hasSel = false;
    }
    recompute_highlight(st);
    if (mouse.just_pressed || mouse.left_down || mouse.just_released) remember_selection(sess);
}

inline void append_code_lines(std::vector<reading::CodeLine>& lines, const ecs::DiffHunk& hunk,
                              reading::DiffSide side, int firstColumn = 1) {
    int oldLine = hunk.oldStart, newLine = hunk.newStart;
    for (const auto& raw : hunk.lines) {
        if (raw.empty()) continue;
        const char sign = raw.front();
        const int oldNumber = sign == '+' ? 0 : oldLine++;
        const int newNumber = sign == '-' ? 0 : newLine++;
        const int number = side == reading::DiffSide::Before ? oldNumber : newNumber;
        if (number <= 0) continue;
        std::string_view text(raw);
        text.remove_prefix(1);
        if (text.ends_with('\r')) text.remove_suffix(1);
        lines.push_back({number, lines.empty() ? firstColumn : 1, text});
    }
}

inline std::vector<reading::CodeLine> code_lines(const ecs::FileDiff& file, reading::DiffSide side, int firstColumn = 1) {
    std::vector<reading::CodeLine> lines;
    for (const auto& hunk : file.hunks) append_code_lines(lines, hunk, side, firstColumn);
    if (file.isFullContent && lines.empty()) lines.push_back({1, 1, {}});
    return lines;
}

inline void keyboard_selection(const reading::CodePosition& head) {
    auto& st = state();
    if (st.extending) st.head = head;
    else if (!st.hasSel) st.anchor = st.head = head;
    recompute_highlight(st);
}

inline void handle_keyboard(UIContext<InputAction>& ctx, Session& session, const std::vector<ecs::FileDiff>& diffs,
                             const ecs::LayoutComponent& layout) {
    if (!session.owner || !reader_shortcuts(ctx, *session.owner, layout)) return;
    auto& repo = *session.owner;
    const auto owner = shortcut_owner(ctx, repo);
    if (owner.text || owner.region != reading::focus::Region::Code) return;
    using Motion = reading::CodeMotion;
    const bool shift = afterhours::input::is_key_down(340) || afterhours::input::is_key_down(344);
    const bool alt = afterhours::input::is_key_down(342) || afterhours::input::is_key_down(346);
    const bool command = afterhours::input::is_key_down(343) || afterhours::input::is_key_down(347) ||
        afterhours::input::is_key_down(341) || afterhours::input::is_key_down(345);
    const bool control = afterhours::input::is_key_down(341) || afterhours::input::is_key_down(345);
    std::optional<Motion> motion;
    auto key = [&](int code, InputAction action, Motion plain, Motion word, Motion boundary) {
        if (!afterhours::input::is_key_pressed(code)) return;
        (void)ctx.pressed(action);
        motion = command ? boundary : alt || control ? word : plain;
    };
    key(263, InputAction::WidgetLeft, Motion::Left, Motion::WordLeft, Motion::LineStart);
    key(262, InputAction::WidgetRight, Motion::Right, Motion::WordRight, Motion::LineEnd);
    key(265, InputAction::WidgetUp, Motion::Up, Motion::Up, Motion::DocumentStart);
    key(264, InputAction::WidgetDown, Motion::Down, Motion::Down, Motion::DocumentEnd);
    if (afterhours::input::is_key_pressed(268)) motion = control || command ? Motion::DocumentStart : Motion::LineStart;
    if (afterhours::input::is_key_pressed(269)) motion = control || command ? Motion::DocumentEnd : Motion::LineEnd;
    auto& st = state();
    const auto* document = repo.workspace().document(repo.workspace().active_id());
    if (!motion || diffs.empty()) return;
    auto point = document->caret.value_or(reading::CodePosition{diffs.front().filePath});
    const auto file = std::find_if(diffs.begin(), diffs.end(), [&](const ecs::FileDiff& value) { return value.filePath == point.path; });
    if (file == diffs.end() || file->isBinary) return;
    auto lines = code_lines(*file, point.side, file->isFullContent ? repo.fullFilePage.begin.column : 1);
    for (const auto& [key, entry] : repo.hunkContext.entries)
        if (key.starts_with(file->filePath + "\n")) append_code_lines(lines, entry.result.lines, point.side);
    std::stable_sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.number < b.number; });
    lines.erase(std::unique(lines.begin(), lines.end(), [](const auto& a, const auto& b) { return a.number == b.number; }), lines.end());
    if (lines.empty()) return;
    std::optional<reading::CodePosition> collapsed;
    if (!shift && st.hasSel && (*motion == Motion::Left || *motion == Motion::Right)) {
        auto first = st.anchor, last = st.head;
        if (std::tie(first.line, first.column) > std::tie(last.line, last.column)) std::swap(first, last);
        collapsed = *motion == Motion::Left ? first : last;
    }
    if (shift) {
        if (!st.hasSel && !st.extending) { st.anchor = point; bind_selection_source(session); }
        st.extending = true;
    } else {
        st.extending = false;
        st.hasSel = false;
        st.anchor = st.head = point;
        st.hl.clear();
    }
    remember_selection(session);
    auto moved = collapsed.value_or(reading::move_code(point, *motion, lines));
    const bool backward = *motion == Motion::Left || *motion == Motion::WordLeft || *motion == Motion::Up ||
        *motion == Motion::LineStart || *motion == Motion::DocumentStart;
    if (*motion == Motion::Up || *motion == Motion::Down) {
        std::vector<const Rec*> rows;
        for (const auto& row : st.lastLines) if (row.filePath == point.path && number_on_side(row, point.side) > 0) rows.push_back(&row);
        auto current = std::find_if(rows.begin(), rows.end(), [&](const Rec* row) {
            return reading::caret_byte(point, row->filePath, point.side, number_on_side(*row, point.side), row->content, row->logicalColumn, row->finalFragment).has_value();
        });
        if (current != rows.end()) {
            const auto& row = **current;
            if (!st.preferredX) st.preferredX = code_mw(session, row.content.substr(0, reading::byte_at_column(row.content, point.column - row.logicalColumn + 1)));
            auto next = current;
            if (backward && current != rows.begin()) --next;
            if (!backward && std::next(current) != rows.end()) ++next;
            if (next != current) {
                int byte = 0;
                float distance = *st.preferredX;
                for (size_t end : code_wrap::character_ends((*next)->content)) {
                    const float candidate = std::abs(code_mw(session, (*next)->content.substr(0, end)) - *st.preferredX);
                    if (candidate < distance) { distance = candidate; byte = static_cast<int>(end); }
                }
                moved = code_position(**next, byte, point.side);
            }
        }
    } else st.preferredX.reset();
    if (file->isFullContent) {
        const auto& page = repo.fullFilePage;
        std::optional<ecs::FilePageRequest> request;
        std::optional<Motion> afterLoad;
        if (*motion == Motion::DocumentStart && page.begin.offset > 0) {
            request = ecs::FilePageRequest{};
            moved.line = moved.column = 1;
        } else if (*motion == Motion::DocumentEnd && page.next.offset < page.totalBytes) {
            request = ecs::FilePageRequest{ecs::FilePageRequest::Action::Previous, {page.totalBytes}};
            afterLoad = Motion::DocumentEnd;
        } else if ((*motion == Motion::LineStart && point.line == page.begin.line && page.begin.column > 1) ||
                   (backward && moved == point && page.begin.offset > 0)) {
            moved.line = *motion == Motion::LineStart || page.begin.continuation ? point.line : std::max(1, point.line - 1);
            moved.column = *motion == Motion::LineStart ? 1 : std::max(1, point.column - 1);
            if (!page.begin.continuation && *motion != Motion::Up) moved.column = std::numeric_limits<int>::max();
            request = ecs::FilePageRequest{ecs::FilePageRequest::Action::TargetLine, {}, moved.line, {}, 0, moved.column};
        } else if ((*motion == Motion::LineEnd && point.line == page.next.line && page.next.continuation && page.next.offset < page.totalBytes) ||
                   (!backward && moved == point && page.next.offset < page.totalBytes)) {
            moved.line = page.next.line;
            moved.column = *motion == Motion::LineEnd ? std::numeric_limits<int>::max() : page.next.column;
            request = ecs::FilePageRequest{ecs::FilePageRequest::Action::TargetLine, {}, moved.line, {}, 0, moved.column};
        }
        if (request && point.line == moved.line &&
            (*motion == Motion::Left || *motion == Motion::Right || *motion == Motion::WordLeft || *motion == Motion::WordRight)) {
            moved = point;
            afterLoad = *motion;
        }
        if (request) {
            navigation::request_caret_page(repo, std::move(*request), moved, afterLoad);
            return;
        }
    }
    if (session.review) {
        session.review->foldedFiles.erase(session.reviewScope + "\n" + file->filePath);
        for (const auto& hunk : file->hunks) {
            const int start = point.side == reading::DiffSide::Before ? hunk.oldStart : hunk.newStart;
            const int count = point.side == reading::DiffSide::Before ? hunk.oldCount : hunk.newCount;
            if (moved.line >= start && moved.line < start + count)
                session.review->foldedHunks.erase(session.reviewScope + "\n" + ecs::ReviewComponent::hunk_key(file->filePath, hunk));
        }
    }
    navigation::set_caret(repo, moved);
    session.caret = moved;
    if (!shift) st.anchor = st.head = moved;
    keyboard_selection(moved);
    remember_selection(session);
    auto viewportEntity = afterhours::ui::UICollectionHolder::getEntityForID(repo.reading.entity);
    if (!viewportEntity.valid()) return;
    const auto viewport = ui::visible_rect(**viewportEntity);
    if (viewport.height <= 0.f) return;
    const float margin = 36.f * zoom::get();
    float fraction = backward ? margin / viewport.height : 1.f - (session.fontSize + 8.f * zoom::get()) / viewport.height;
    for (const auto& row : st.lastLines) {
        if (!reading::caret_byte(moved, row.filePath, moved.side, number_on_side(row, moved.side), row.content, row.logicalColumn, row.finalFragment)) continue;
        if (row.rect.y >= viewport.y + margin && row.rect.y + row.rect.height <= viewport.y + viewport.height) return;
        fraction = row.rect.y < viewport.y + margin ? margin / viewport.height : 1.f - row.rect.height / viewport.height;
        break;
    }
    navigation::reveal_caret(repo, moved, std::clamp(fraction, 0.f, 1.f));
}

inline std::optional<reading::ReadingAnchor> source_point(const ecs::FileDiff& file, const Rectangle& viewport,
                                                         const std::string& revision, const std::optional<reading::CodePosition>& caret) {
    const auto& selection = state();
    const auto position = selection.hasSel ? std::optional<reading::CodePosition>{selection.anchor} : caret;
    if (position && position->path == file.filePath)
        return reading::ReadingAnchor{file.filePath, revision, position->side, position->line, position->column, 0.f,
            position->side == reading::DiffSide::Before ? '-' : ' '};
    auto anchor = [&](const Rec& row, int column) {
        const auto side = row.sign == '-' || row.side == 1 ? reading::DiffSide::Before : reading::DiffSide::After;
        const auto text = reading::diff_text_at(file, row.lineNo, side);
        return reading::ReadingAnchor{file.filePath, revision, side, row.lineNo,
            reading::column_at_byte(text, row.sourceOffset + static_cast<size_t>(std::max(0, column))), 0.f, row.sign};
    };
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
    double curY = 0.;           // running content-Y of the next row (px)
    double pending = 0.;        // height of skipped rows not yet flushed (px)
    ecs::ReadingLayout* layout = nullptr;
    std::optional<reading::ReadingAnchor> restoreAnchor;
    struct AnchorCandidate { int distance; int line; double y; };
    std::optional<AnchorCandidate> nearestAnchor;
    bool restoredAnchor = false;
    std::optional<size_t> anchorByte;

    void restore_at(double y) {
        const float height = scroll->viewport_or_zero().y;
        const float target = std::max(0., y - restoreAnchor->viewportFraction * height);
        scroll->scroll_offset.y = scroll->scroll_target.y = scroll->last_eased_offset.y = target;
        scroll->anchor_child = -1;
        top = target - height;
        bottom = target + height * 2.f;
        restoredAnchor = true;
    }

    void observe_line(const std::string& path, int line, reading::DiffSide side, char,
                      const std::string& text, size_t begin, size_t end, int columnBase = 1) {
        if (!scroll || scroll->viewport_or_zero().y <= 0.f) return;
        if (restoreAnchor && !restoredAnchor && path == restoreAnchor->path && side == restoreAnchor->side) {
            const int distance = std::abs(line - restoreAnchor->line);
            if (!nearestAnchor || distance < nearestAnchor->distance) nearestAnchor = AnchorCandidate{distance, line, curY};
            if (line == restoreAnchor->line) {
                if (!anchorByte) anchorByte = reading::byte_at_column(text, restoreAnchor->column - columnBase + 1);
                if (*anchorByte >= begin && (*anchorByte < end || end == text.size())) restore_at(curY);
            }
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
        float target = std::clamp(curY - px(36.f), 0.,
                                 static_cast<double>(std::max(0.f, scroll->content_size.y - height)));
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

namespace diff_sel {

inline bool caret_line(const Session* session, const std::string& path, reading::DiffSide side, int line) {
    return session && session->caret && session->caret->path == path && session->caret->side == side && session->caret->line == line;
}

inline afterhours::Color active_background(afterhours::Color color) {
    auto tint = [](unsigned char channel) { return static_cast<unsigned char>(channel + (255 - channel) * .045f); };
    return {tint(color.r), tint(color.g), tint(color.b), color.a};
}

inline void render_caret(UIContext<InputAction>& ctx, Entity& row, const Session& session,
                          const std::string& path, reading::DiffSide side, int line,
                          const std::string& text, float prefix, int firstColumn, bool finalFragment) {
    if (!caret_line(&session, path, side, line)) return;
    div(ctx, mk(row, 90004), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{pixels(std::max(0.f, prefix / zoom::get() - 2.f)), pixels(diff_detail::code_line_height())})
        .with_absolute_position(2.f, 0.f).with_custom_background(afterhours::Color{110, 156, 220, 24})
        .with_roundness(0.f).with_debug_name("code_active_gutter"));
    if (!session.codeFocused) return;
    const auto at = reading::caret_byte(*session.caret, path, side, line, text, firstColumn, finalFragment);
    if (!at) return;
    const float x = (prefix + code_mw(session, text.substr(0, *at))) / zoom::get();
    div(ctx, mk(row, 90005), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{pixels(1.5f), pixels(std::max(1.f, diff_detail::code_line_height() - 6.f))})
        .with_absolute_position(x, 3.f).with_custom_background(theme::TEXT_PRIMARY)
        .with_roundness(0.f).with_debug_name("code_caret"));
}

}

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
                              size_t sourceOffset = 0, bool finalFragment = true, const std::string* original = nullptr, const PreparedCode* prepared = nullptr,
                              const source_folding::Range* fold = nullptr, bool folded = false) {
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
    const auto caretSide = prefix == '-' || (prefix == ' ' && sel && sel->caret && sel->caret->side == reading::DiffSide::Before)
        ? reading::DiffSide::Before : reading::DiffSide::After;
    const int caretLine = caretSide == reading::DiffSide::Before ? (oldNum.empty() ? 0 : std::stoi(oldNum)) : (newNum.empty() ? 0 : std::stoi(newNum));
    const bool activeLine = diff_sel::caret_line(sel, filePath, caretSide, caretLine);
    if (activeLine) bgColor = diff_sel::active_background(bgColor);


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
                                                sel && sel->visibleWhitespace, hasNewline, original, sourceOffset, finalFragment, ending, prepared, activeLine))
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
             (prepared ? prepared->column : reading::column_at_byte(original ? *original : content, sourceOffset) +
                 (lno == sel->sourceStartLine ? sel->sourceStartColumn - 1 : 0)), finalFragment});
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
    if (fold && !sourceOffset && sel) {
        const float foldWidth = diff_sel::code_mw(*sel, "  ") / zoom::get();
        auto* foldRepo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
        const bool enabled = foldRepo && foldRepo->workspace().document(foldRepo->workspace().active_id())->sourceFolds.can_toggle(*fold);
        auto toggle = button(ctx, mk(lineDiv.ent(), 90003), preset::Button(folded ? ">" : "v")
            .with_skip_grid_snap().with_size(ComponentSize{pixels(foldWidth), pixels(16)})
            .with_absolute_position(0.f, std::max(0.f, (diff_detail::code_line_height() - 16.f) * .5f))
            .with_transparent_bg().with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
            .with_padding(Padding{}).with_disabled(!enabled).with_debug_name("source_fold_" + std::to_string(fold->first)));
        set_tooltip(toggle.ent(), enabled ? std::string(folded ? "Expand " : "Collapse ") + std::to_string(fold->end - fold->first - 1) + " lines" :
            "Unfold a block before folding another");
        if (toggle && foldRepo) navigation::toggle_source_fold(*foldRepo, *fold);
    }
    if (sel) diff_sel::render_caret(ctx, lineDiv.ent(), *sel, filePath, caretSide, caretLine, content,
        diff_sel::content_x_offset(*sel, gutter), prepared ? prepared->column : reading::column_at_byte(original ? *original : content, sourceOffset), finalFragment);
}

// Render a single hunk with its header and all diff lines.
inline void render_sbs_hunk(UIContext<InputAction>&, Entity&, const ecs::FileDiff&,
                            const ecs::DiffHunk&, int&, float,
                            diff_detail::DiffViewport*, diff_sel::Session*, bool);

inline void render_hunk_lines(UIContext<InputAction>& ctx, Entity& parent, const ecs::FileDiff& fileDiff,
                              const ecs::DiffHunk& hunk, int& nextId, float contentWidth,
                              diff_sel::Session* sel, diff_detail::DiffViewport* vp,
                              bool sideBySide, float lineWidth) {
    if (fileDiff.isFullContent && hunk.lines.empty()) {
        if (vp) { vp->flush(ctx, parent, nextId); vp->built(diff_detail::code_line_height()); }
        int oldLine = 1, newLine = 1;
        render_diff_line(ctx, parent, nextId++, " ", oldLine, newLine, contentWidth, fileDiff.filePath,
            sel, {}, false, false, true);
        return;
    }
    if (sideBySide) {
        render_sbs_hunk(ctx, parent, fileDiff, hunk, nextId,
                        lineWidth > 0 ? lineWidth : contentWidth, vp, sel, false);
        return;
    }

    int oldLine = hunk.oldStart;
    int newLine = hunk.newStart;

    const float width = lineWidth > 0 ? lineWidth : contentWidth;
    std::vector<size_t> sourceRows;
    if (fileDiff.isFullContent && vp && vp->active) {
        sourceRows = diff_metrics().source_rows(fileDiff.renderIdentity, width * zoom::get(), sel->fontSize, sel->visibleWhitespace, [&] {
            std::vector<size_t> rows{0};
            rows.reserve(hunk.lines.size() + 1);
            for (size_t i = 0; i < hunk.lines.size(); ++i) {
                const auto& line = hunk.lines[i];
                const auto content = line.empty() ? "" : line.substr(1);
                const int number = hunk.newStart + static_cast<int>(i);
                const auto gutter = code_gutter::prefix(std::to_string(number), std::to_string(number), ' ', true);
                const float available = std::max(1.f, width * zoom::get() - diff_sel::content_x_offset(*sel, gutter) - 12.f);
                const auto breaks = diff_sel::wrapped_rows(*sel, content, available, !hunk.noNewline.contains(i),
                    std::to_string(fileDiff.renderIdentity) + ":a:" + std::to_string(number));
                rows.push_back(rows.back() + breaks.size() - 1);
            }
            return rows;
        });
    }
    auto* foldOwner = fileDiff.isFullContent ? ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>() : nullptr;
    if (foldOwner && sel->findNavigate && sel->findMatch) navigation::reveal_source_line(*foldOwner, sel->findMatch->line);
    std::vector<source_folding::Range> closed;
    if (foldOwner) for (auto range : foldOwner->sourceFoldRanges)
        if (foldOwner->workspace().document(foldOwner->workspace().active_id())->sourceFolds.closed(range)) closed.push_back(range);
    auto changedRanges = code_highlight::hunk_ranges(hunk.lines);
    for (size_t index = 0; index < hunk.lines.size(); ++index) {
        auto hidden = std::find_if(closed.begin(), closed.end(), [&](auto range) { return range.contains(newLine); });
        if (hidden != closed.end()) {
            const auto next = std::min(hunk.lines.size(), static_cast<size_t>(hidden->end - hunk.newStart));
            nextId += static_cast<int>(sourceRows.empty() ? next - index : sourceRows[next] - sourceRows[index]);
            oldLine += static_cast<int>(next - index);
            newLine += static_cast<int>(next - index);
            index = next - 1;
            continue;
        }
        if (!sourceRows.empty()) {
            const auto count = sourceRows[index + 1] - sourceRows[index];
            const double height = static_cast<double>(vp->px(diff_detail::code_line_height())) * count;
            const bool anchor = vp->restoreAnchor && !vp->restoredAnchor &&
                newLine == std::clamp(vp->restoreAnchor->line, hunk.newStart, hunk.newStart + static_cast<int>(hunk.lines.size()) - 1);
            const bool find = sel->findNavigate && diff_sel::found_line(sel, fileDiff.filePath, newLine, ' ');
            if (!anchor && !find && (vp->curY + height < vp->top || vp->curY > vp->bottom)) {
                vp->pending += height;
                vp->curY += height;
                nextId += static_cast<int>(count);
                ++oldLine;
                ++newLine;
                continue;
            }
        }
        const auto& line = hunk.lines[index];
        char sign = line.empty() ? ' ' : line.front();
        std::string content = line.empty() ? "" : line.substr(1);
        auto gutter = code_gutter::prefix(sign == '+' ? "" : std::to_string(oldLine),
            sign == '-' ? "" : std::to_string(newLine), sign, fileDiff.isFullContent);
        float available = std::max(1.f, width * zoom::get() - diff_sel::content_x_offset(*sel, gutter) - 12.f);
        const auto identity = std::to_string(fileDiff.renderIdentity) + (sign == '-' ? ":b:" : ":a:") + std::to_string(sign == '-' ? oldLine : newLine);
        auto breaks = diff_sel::wrapped_rows(*sel, content, available, !hunk.noNewline.contains(index), identity);
        PreparedCode prepared;
        prepared.column = newLine == sel->sourceStartLine ? sel->sourceStartColumn : 1;
        for (size_t part = 0; part + 1 < breaks.size(); ++part) {
            int lineId = nextId++;
            auto begin = breaks[part], end = breaks[part + 1];
            if (vp) {
                if (sign != '-') vp->observe_line(fileDiff.filePath, newLine, reading::DiffSide::After, sign, content, begin, end, newLine == sel->sourceStartLine ? sel->sourceStartColumn : 1);
                if (sign != '+') vp->observe_line(fileDiff.filePath, oldLine, reading::DiffSide::Before, sign, content, begin, end);
            }
            if (sel->findNavigate && vp &&
                diff_sel::found_line(sel, fileDiff.filePath, sign == '-' ? oldLine : newLine, sign) &&
                sel->findMatch->column >= begin && (sel->findMatch->column < end || part + 2 == breaks.size()))
                vp->reveal();
            if (!vp || vp->visible(diff_detail::code_line_height())) {
                if (vp) vp->flush(ctx, parent, nextId);
                int oldNumber = oldLine, newNumber = newLine;
                if (!prepared.tokens) prepared.tokens = code_highlight::token_cache().get_source(content, sign == '-' && !fileDiff.oldPath.empty() ? fileDiff.oldPath : fileDiff.filePath, sel->visibleWhitespace, identity, hunk_syntax::at(hunk, index, sign == '-'));
                const source_folding::Range* fold = nullptr;
                if (foldOwner) {
                    auto found = std::lower_bound(foldOwner->sourceFoldRanges.begin(), foldOwner->sourceFoldRanges.end(), newLine,
                        [](auto range, int number) { return range.first < number; });
                    if (found != foldOwner->sourceFoldRanges.end() && found->first == newLine) fold = &*found;
                }
                render_diff_line(ctx, parent, lineId, std::string(1, sign) + content.substr(begin, end - begin),
                    oldNumber, newNumber, width, fileDiff.filePath, sel,
                    code_wrap::intersect(changedRanges[index], begin, end), !hunk.noNewline.contains(index),
                    hunk.movedLines.contains(index), fileDiff.isFullContent, begin, part + 2 == breaks.size(), &content, &prepared, fold,
                    fold && foldOwner->workspace().document(foldOwner->workspace().active_id())->sourceFolds.closed(*fold));
                if (vp) vp->built(diff_detail::code_line_height());
            } else vp->skipped(diff_detail::code_line_height());
            const auto fragment = std::string_view(content).substr(begin, end - begin);
            prepared.offset += code_highlight::display_size(fragment, sel->visibleWhitespace);
            prepared.column += reading::column_at_byte(fragment, fragment.size()) - 1;
        }
        if (sign != '+') ++oldLine;
        if (sign != '-') ++newLine;
    }
}

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
    const std::string hkey = fileDiff.isFullContent ? std::string{} : (sel ? sel->reviewScope : "") + "\n" +
        ecs::ReviewComponent::hunk_key(fileDiff.filePath, hunk);
    bool isCursor = false;
    if (reviewOn) {
        if (sel->review->approvedHunks.count(hkey) && !sel->review->showApproved)
            return;
        // Keyboard chunk cursor + pending vim actions (a=approve, c=comment).
        int ord = sel->hunkOrdinal++;
        // Read-only embedded (commit-detail) diff has no review cursor, so the
        // first hunk must not pick up the cursor highlight.
        isCursor = !sel->embedded && (ord == sel->review->cursor);
        if (isCursor && sel->review->cursorMoved && vp && vp->scroll) {
            float viewportHeight = vp->scroll->viewport_or_zero().y;
            float target = std::clamp(vp->curY - vp->px(24.f), 0.,
                                     static_cast<double>(std::max(0.f, vp->scroll->content_size.y - viewportHeight)));
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
        if (sel && !sel->repoPath.empty() && !fileDiff.isFullContent && !fileDiff.isNew && !fileDiff.isDeleted && sel->reviewScope != "snapshot") {
            for (bool above : {true, false}) {
                auto key = hunk_context::key(fileDiff, hunk, above);
                auto stamp = navigation::stamp(owner->get<ecs::RepoComponent>(), key);
                items.push_back(ContextMenuItem::item(above ? "Show 20 lines above" : "Show 20 lines below", [currentTab, key, stamp] {
                    if (auto* tab = currentTab()) {
                        auto& repo = tab->get<ecs::RepoComponent>();
                        if (navigation::accepts(repo, stamp, key)) navigation::expand_hunk_context(repo, key);
                    }
                }));
            }
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
            const bool folded = sel->review->foldedHunks.contains(key);
            items.push_back(ContextMenuItem::item(folded ? "Unfold hunk" : "Fold hunk", [currentReview, key, folded] {
                if (auto* review = currentReview()) {
                    if (folded) review->foldedHunks.erase(key);
                    else review->foldedHunks.insert(key);
                    review->dirty = true;
                }
            }));
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
    const auto headerRect = visible_rect(hunkRow.ent());
    bool revealActions = ctx.is_input_allowed(hunkRow.ent().id) && headerRect.width > 0.f && headerRect.height > 0.f &&
        afterhours::ui::is_mouse_inside(ctx.mouse.pos, headerRect);
    if (auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id); focused.valid()) {
        const auto target = focus_target(**focused);
        const auto owner = focus_target(hunkRow.ent());
        revealActions |= target && owner && target->repository == owner->repository && target->document == owner->document &&
            target->region == reading::focus::Region::Code && target->item == hkey;
    }
    const bool compactActions = contentWidth > 0.f && contentWidth < 680.f;
    auto hunkBtns = div(ctx, mk(hunkRow.ent(), 9),
        ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{children(), percent(1.0f)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_gap(pixels(compactActions ? 4 : 6))
            .with_margin(Margin{.right = pixels(8)})
            .with_transparent_bg()
            .with_roundness(0.0f)
            .with_opacity(revealActions ? 1.f : 0.f)
            .with_debug_name("hunk_header_btns"));

    if (sel && !sel->repoPath.empty() && !fileDiff.isFullContent && !fileDiff.isNew && !fileDiff.isDeleted && sel->reviewScope != "snapshot") {
        auto context = button(ctx, mk(hunkBtns.ent(), 4), preset::Button(compactActions ? "+" : "Expand context")
            .with_size(ComponentSize{compactActions ? pixels(24) : children(), pixels(compactActions ? 22 : 18)})
            .with_padding(Padding{}).with_font_size(pixels(12))
            .with_custom_background(theme::BUTTON_SECONDARY).with_debug_name("expand_diff_context"));
        set_tooltip(context.ent(), "Show 20 more unchanged lines above or below this hunk");
        if (context) {
            remember_focus_origin(ctx, context.ent());
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            std::vector<ContextMenuItem> items;
            for (bool above : {true, false}) {
                auto key = hunk_context::key(fileDiff, hunk, above);
                auto stamp = navigation::stamp(*repo, key);
                items.push_back(ContextMenuItem::item(above ? "Show 20 lines above" : "Show 20 lines below", [key, stamp] {
                    auto* active = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
                    if (active && navigation::accepts(*active, stamp, key)) navigation::expand_hunk_context(*active, key);
                }));
            }
            auto rect = visible_rect(context.ent());
            show_context_menu(rect.x, rect.y + rect.height, std::move(items));
        }
    }

    // Copy button only where drag-select-to-copy isn't available (i.e. the
    // embedded commit-detail diff). In the working-tree diff, select-to-copy
    // (with file:line) replaces it.
    if (!(sel && sel->enabled)) {
        std::string hunkText = diff_detail::hunk_to_text(hunk);
        auto copyBtn = button(ctx, mk(hunkBtns.ent(), 1),
            preset::Button("Copy")
                .with_size(ComponentSize{compactActions ? pixels(36) : children(), pixels(compactActions ? 22 : 18)})
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(compactActions ? 0 : 8),
                    .bottom = pixels(2), .left = pixels(compactActions ? 0 : 8)})
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
                preset::Button(compactActions ? "" : approved ? "Unapprove" : "Approve")
                    .with_size(ComponentSize{compactActions ? pixels(24) : children(), pixels(compactActions ? 22 : 18)})
                    .with_padding(Padding{
                        .top = pixels(2), .right = pixels(compactActions ? 0 : 8),
                        .bottom = pixels(2), .left = pixels(compactActions ? 0 : 8)})
                    .with_custom_background(theme::BUTTON_SECONDARY)
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(pixels(12))
                    .with_align_items(AlignItems::Center).with_justify_content(JustifyContent::Center)
                    .with_debug_name("approve_hunk_btn"));
            set_tooltip(approveBtn.ent(), approved ? "Remove hunk approval" : "Approve hunk for review");
            if (compactActions) chrome_icon(ctx, mk(approveBtn.ent(), 0), ChromeIcon::Check,
                approved ? theme::DIFF_ADD_TEXT : theme::TEXT_PRIMARY, "hunk_approve_icon");
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
                    .with_size(ComponentSize{compactActions ? pixels(40) : children(), pixels(compactActions ? 22 : 18)})
                    .with_padding(Padding{})
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
            preset::Button(compactActions ? "" : "Comment")
                .with_size(ComponentSize{compactActions ? pixels(24) : children(), pixels(compactActions ? 22 : 18)})
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(compactActions ? 0 : 8),
                    .bottom = pixels(2), .left = pixels(compactActions ? 0 : 8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(pixels(12))
                .with_align_items(AlignItems::Center).with_justify_content(JustifyContent::Center)
                .with_debug_name("comment_hunk_btn"));
        set_tooltip(commentBtn.ent(), "Comment on hunk");
        if (compactActions) chrome_icon(ctx, mk(commentBtn.ent(), 0), ChromeIcon::Message,
            theme::TEXT_PRIMARY, "hunk_comment_icon");
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
        auto input = ui::text_area(
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
        if (sel->owner) {
            sel->owner->reading.editorEntity = composeRow.ent().id;
            sel->owner->reading.revealEditor = sel->review->composingFocus;
        }
        if (sel->review->composingFocus) {
            focus_control(ctx, input.ent());
            sel->review->composingFocus = false;
        }
        if (previousDraft != sel->review->composingText) sel->review->dirty = true;
        render_comment_kind(ctx, composeRow.ent(), 2, *sel->review, false);
        auto addBtn = button(ctx, mk(composeRow.ent(), 1),
            preset::Button(addLabel)
                .with_size(ComponentSize{pixels(addWidth), pixels(18)})
                .with_font_size(pixels(12))
                .with_debug_name("comment_add_btn"));
        if (addBtn) {
            const auto comment = ecs::pending_comment(*sel->review);
            ecs::commit_pending_comment(*sel->review);
            if (sel->owner && !comment.text.empty()) navigation::return_to_feedback(*sel->owner, comment);
        }
    }

    if (reviewOn && sel->review->foldedHunks.count(hkey)) {
        if (vp) { vp->flush(ctx, parent, nextId); vp->built(20.0f); }
        auto expand = button(ctx, mk(parent, nextId++),
            ComponentConfig{}.with_skip_grid_snap()
                .with_label("Folded hunk · click to expand")
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

    auto* contextRepo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
    const bool localContext = contextRepo && sel && !contextRepo->workspace().document(contextRepo->workspace().active_id())->contextLines.empty() && !fileDiff.isFullContent && !fileDiff.isNew &&
        !fileDiff.isDeleted && sel->reviewScope != "snapshot";
    size_t hunkIndex = static_cast<size_t>(&hunk - fileDiff.hunks.data());
    auto drawContext = [&](bool above) {
        if (!localContext) return;
        int previousBelow = 0;
        if (above && hunkIndex > 0) {
            const auto& previous = fileDiff.hunks[hunkIndex - 1];
            auto previousKey = sel->reviewScope + "\n" + ecs::ReviewComponent::hunk_key(fileDiff.filePath, previous);
            if (!reviewOn || (!sel->review->foldedHunks.contains(previousKey) &&
                (sel->review->showApproved || !sel->review->approvedHunks.contains(previousKey))))
                if (const auto* context = hunk_context::content(*contextRepo, fileDiff, hunkIndex - 1, false, sel->reviewScope))
                    previousBelow = context->lines.newCount;
        }
        const auto* context = hunk_context::content(*contextRepo, fileDiff, hunkIndex, above, sel->reviewScope, previousBelow);
        if (!context) return;
        if (!context->error.empty()) {
            if (vp) { vp->flush(ctx, parent, nextId); vp->built(24.f); }
            div(ctx, mk(parent, nextId++), ComponentConfig{}.with_skip_grid_snap()
                .with_label(context->error).with_size(ComponentSize{w, pixels(24)})
                .with_font_size(pixels(12)).with_custom_text_color(theme::STATUS_MODIFIED)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("hunk_context_notice"));
            return;
        }
        render_hunk_lines(ctx, parent, fileDiff, context->lines, nextId, contentWidth, sel, vp, sideBySide, lineWidth);
    };
    drawContext(true);
    render_hunk_lines(ctx, parent, fileDiff, hunk, nextId, contentWidth, sel, vp, sideBySide, lineWidth);
    drawContext(false);
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
                            bool hasNewline = true, bool moved = false, size_t sourceOffset = 0, bool finalFragment = true, const std::string* original = nullptr, float available = 0.f, const PreparedCode* prepared = nullptr) {
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
    const auto caretSide = leftBorder ? reading::DiffSide::Before : reading::DiffSide::After;
    const int caretLine = num.empty() ? 0 : std::stoi(num);
    const bool activeLine = kind != SbsKind::Empty && diff_sel::caret_line(sel, filePath, caretSide, caretLine);
    if (activeLine) bg = diff_sel::active_background(bg);

    std::string gutter = code_gutter::pad(num) + "  " + sign + " ";
    if (sourceOffset) gutter.assign(gutter.size(), ' ');
    std::string label = gutter + content;

    auto ending = diff_sel::ending_label(*sel, original ? *original : content, hasNewline, available);
    auto cfg = ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{percent(0.5f), pixels(code_line_height())})
        .with_custom_background(bg)
        .with_custom_text_color(fg)
        .with_styled_label(highlighted_code(label.substr(0, label.size() - content.size()), content, filePath,
                                            sel && sel->visibleWhitespace && kind != SbsKind::Empty, hasNewline, original, sourceOffset, finalFragment, ending, prepared, activeLine))
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
             reading::column_at_byte(original ? *original : content, sourceOffset), finalFragment});
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
    if (sel && kind != SbsKind::Empty) diff_sel::render_caret(ctx, cell.ent(), *sel, filePath, caretSide, caretLine, content,
        diff_sel::content_x_offset(*sel, gutter), reading::column_at_byte(original ? *original : content, sourceOffset), finalFragment);

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
    struct SyntaxRow { std::string number, content; code_lexer::State state; };
    std::vector<SyntaxRow> dels, adds;

    auto emitRow = [&](const std::string& lNum, const std::string& lContent,
                       SbsKind lKind, const std::string& rNum,
                       const std::string& rContent, SbsKind rKind, code_lexer::State lState, code_lexer::State rState) {
        float gutter = diff_sel::content_x_offset(*sel, code_gutter::pad(lNum.size() > rNum.size() ? lNum : rNum) + "  + ");
        float available = std::max(1.f, contentWidth * zoom::get() * .5f - gutter - 12.f);
        auto left = diff_sel::wrapped_rows(*sel, lContent, available, lNum.empty() || !oldNoNewline.contains(std::stoi(lNum)),
            std::to_string(fileDiff.renderIdentity) + ":b:" + lNum);
        auto right = diff_sel::wrapped_rows(*sel, rContent, available, rNum.empty() || !newNoNewline.contains(std::stoi(rNum)),
            std::to_string(fileDiff.renderIdentity) + ":a:" + rNum);
        auto changes = lKind == SbsKind::Del && rKind == SbsKind::Add
            ? code_highlight::changed_ranges(lContent, rContent)
            : std::pair<code_highlight::Range, code_highlight::Range>{};
        PreparedCode leftCode, rightCode;
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
                auto& prepared = isLeft ? leftCode : rightCode;
                if (!prepared.tokens) prepared.tokens = code_highlight::token_cache().get_source(text,
                    isLeft && !fileDiff.oldPath.empty() ? fileDiff.oldPath : fileDiff.filePath, sel->visibleWhitespace,
                    std::to_string(fileDiff.renderIdentity) + (isLeft ? ":b:" : ":a:") + num, isLeft ? lState : rState);
                prepared.offset = code_highlight::display_size(std::string_view(text).substr(0, begin), sel->visibleWhitespace);
                diff_detail::render_sbs_cell(ctx, rowDiv.ent(), isLeft ? 0 : 1, exists ? num : "",
                    text.substr(begin, end - begin), exists ? kind : SbsKind::Empty, isLeft, fileDiff.filePath, sel,
                    code_wrap::intersect(change, begin, end), num.empty() ||
                        !(isLeft ? oldNoNewline : newNoNewline).contains(std::stoi(num)),
                    !num.empty() && (isLeft ? oldMoved : newMoved).contains(std::stoi(num)), begin,
                    exists && part + 2 == breaks.size(), exists ? &text : nullptr, available, &prepared);
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
            emitRow(hasDel ? dels[i].number : "",
                    hasDel ? dels[i].content : "",
                    hasDel ? SbsKind::Del : SbsKind::Empty,
                    hasAdd ? adds[i].number : "",
                    hasAdd ? adds[i].content : "",
                    hasAdd ? SbsKind::Add : SbsKind::Empty,
                    hasDel ? dels[i].state : code_lexer::State{}, hasAdd ? adds[i].state : code_lexer::State{});
        }
        dels.clear();
        adds.clear();
    };

    for (size_t index = 0; index < hunk.lines.size(); ++index) {
        const auto& line = hunk.lines[index];
        char prefix = line.empty() ? ' ' : line[0];
        std::string content = line.size() > 1 ? line.substr(1) : "";
        if (prefix == '-') {
            dels.push_back({std::to_string(oldLine++), content, hunk_syntax::at(hunk, index, true)});
        } else if (prefix == '+') {
            adds.push_back({std::to_string(newLine++), content, hunk_syntax::at(hunk, index, false)});
        } else {
            flush();
            emitRow(std::to_string(oldLine), content, SbsKind::Context,
                    std::to_string(newLine), content, SbsKind::Context,
                    hunk_syntax::at(hunk, index, true), hunk_syntax::at(hunk, index, false));
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
inline void open_find(ecs::RepoComponent& repo) {
    auto& selection = diff_sel::state();
    std::string seed;
    std::optional<reading::ReadingAnchor> position;
    int first, begin, last, end;
    if (selection.hasSel && diff_sel::ordered_span(selection.lastLines, selection.anchor, selection.head,
                                                   first, begin, last, end)) {
        seed = diff_sel::build_copy_text(selection, false);
        if (seed.find_first_of("\r\n") != std::string::npos) seed.clear();
        if (!seed.empty()) {
            const auto& row = selection.lastLines[first];
            const int number = row.sign == '-' ? row.oldLine : row.newLine;
            position = reading::ReadingAnchor{row.filePath, reading::anchor_revision(repo.workspace().location()),
                row.sign == '-' ? reading::DiffSide::Before : reading::DiffSide::After,
                number > 0 ? number : row.lineNo,
                row.logicalColumn + reading::column_at_byte(row.content, begin) - 1, .15f, row.sign};
        }
    }
    navigation::open_find(repo, std::move(seed), std::move(position));
}

inline float diff_controls_height(float width, bool optionsOpen,
                                  bool filterable, bool hasDiffs) {
    return (filterable ? (width < 680.f ? 72.f : 36.f) + (optionsOpen ? 90.f : 0.f) : 0.f) +
        (filterable && hasDiffs ? 24.f : 0.f);
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
    auto* ownerRepo = reviewScope == "snapshot" ? nullptr : ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
    if (ownerRepo) {
        const auto focusOwner = shortcut_owner(ctx, *ownerRepo);
        hunk_context::poll(*ownerRepo, focusOwner.region == reading::focus::Region::Code && !focusOwner.text);
    }
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
    if (ownerRepo && !diffs.empty() && diffs.front().isFullContent) {
        sess.sourceStartLine = ownerRepo->fullFilePage.begin.line;
        sess.sourceStartColumn = ownerRepo->fullFilePage.begin.column;
    }
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
    const bool selectedFileOnly = filterable && filterRepo && !filterRepo->selectedFilePath().empty() &&
        Settings::get().get_review_display_mode(filterRepo->repoPath) == review_files::DisplayMode::SelectedFile;
    auto fileVisible = [&](const ecs::FileDiff& file) {
        return (!selectedFileOnly || file.filePath == filterRepo->selectedFilePath()) &&
            (!filterable || !filterRepo || ecs::review_file_visible(file, filterRepo->fileFilter, review, reviewScope));
    };
    auto fileOrder = ecs::visible_review_file_indices(diffs, filterRepo ? filterRepo->fileFilter : review_files::Filter{}, review, reviewScope);
    std::erase_if(fileOrder, [&](size_t index) { return !fileVisible(diffs[index]); });
    if (layout && filterRepo && navigation::find(*filterRepo).open) {
        auto& find = navigation::find(*filterRepo);
        const float width = std::min(420.f, std::max(180.f, layout->mainContent.width - 16.f));
        auto bar = div(ctx, mk(ui_imm::getUIRootEntity(), 580001), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{pixels(width), pixels(34.f)})
            .with_absolute_position(layout->mainContent.x + layout->mainContent.width - width - 8.f,
                                    layout->mainContent.y + 8.f)
            .with_custom_background(theme::SIDEBAR_BG).with_border(theme::BORDER, pixels(1))
            .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(4))
            .with_align_items(AlignItems::Center).with_render_layer(900)
            .with_debug_name("diff_find_bar"));
        bind_focus(bar.ent(), *filterRepo, reading::focus::Region::Find);
        auto previous = find.query;
        auto input = afterhours::text_input::text_input(ctx, mk(bar.ent(), 0), find.query,
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{expand(), pixels(28)})
                .with_debug_name("diff_find_input"));
        if (find.focus) {
            ui::focus_control(ctx, input.ent());
            find.focus = false;
        }
        if (previous != find.query) {
            find.index = 0;
            find.position.reset();
            find.pendingStep = 0;
            find.navigate = !find.query.empty();
        }
        const bool sourceFind = filterRepo->workspace().active() == reading::Slot::Source;
        auto& sourceRuntime = filterRepo->sourceFind;
        if (sourceFind) {
            const auto key = filterRepo->repoPath + "\n" + filterRepo->fullFilePath() + "\n" + filterRepo->fullFileRevision() +
                "\n" + filterRepo->fullFileEncodingOverride + "\n" + filterRepo->fullFilePage.sourceIdentity +
                "\n" + std::to_string(filterRepo->dataGeneration) + "\n" + find.query;
            if (key != sourceRuntime.key) {
                sourceRuntime = {};
                sourceRuntime.key = key;
                sourceRuntime.request = navigation::stamp(*filterRepo, key);
                if (!find.query.empty()) sourceRuntime.future = git::find_source_async(
                    {filterRepo->repoPath, filterRepo->fullFilePath(), filterRepo->fullFileRevision(), {}, filterRepo->fullFileEncodingOverride}, find.query);
            }
            if (sourceRuntime.future.valid() && sourceRuntime.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                auto result = sourceRuntime.future.get();
                if (navigation::accepts(*filterRepo, sourceRuntime.request, key)) {
                    if (result.error.empty() && result.sourceIdentity != filterRepo->fullFilePage.sourceIdentity) {
                        result.matches.clear();
                        result.error = "File changed. Reload before searching this version.";
                    }
                    sourceRuntime.result = std::move(result);
                } else sourceRuntime.key.clear();
            }
        }
        auto matches = sourceFind ? std::vector<ecs::DiffMatch>{} : ecs::find_diff_matches(diffs, find.query);
        std::set<std::string_view> visiblePaths;
        for (const auto& file : diffs) if (fileVisible(file)) visiblePaths.insert(file.filePath);
        std::erase_if(matches, [&](const auto& match) { return !visiblePaths.contains(match.file); });
        const int count = static_cast<int>(sourceFind ? sourceRuntime.result.matches.size() : matches.size());
        auto matchAt = [&](size_t index) {
            if (!sourceFind) return matches[index];
            const auto& value = sourceRuntime.result.matches[index];
            size_t byte = 0;
            for (const auto& file : diffs) for (const auto& hunk : file.hunks) {
                const int row = value.line - hunk.newStart;
                if (row < 0 || static_cast<size_t>(row) >= hunk.lines.size()) continue;
                const int base = value.line == filterRepo->fullFilePage.begin.line ? filterRepo->fullFilePage.begin.column : 1;
                byte = reading::byte_at_column(std::string_view(hunk.lines[row]).substr(1), value.column - base + 1);
            }
            return ecs::DiffMatch{filterRepo->fullFilePath(), value.line, ' ', byte, value.column};
        };
        if (find.position) {
            if (sourceFind) {
                const auto match = std::ranges::find_if(sourceRuntime.result.matches, [&](const auto& value) {
                    return value.line == find.position->line && value.column == find.position->column;
                });
                if (match != sourceRuntime.result.matches.end()) find.index = static_cast<size_t>(match - sourceRuntime.result.matches.begin());
            } else {
                const auto match = std::ranges::find_if(matches, [&](const auto& value) {
                    return value.file == find.position->path && value.line == find.position->line &&
                        value.sign == find.position->sign && value.logicalColumn == find.position->column;
                });
                if (match != matches.end()) find.index = static_cast<size_t>(match - matches.begin());
            }
        }
        int step = 0;
        if (button(ctx, mk(bar.ent(), 1), preset::Button("<")
                .with_size(ComponentSize{pixels(28), pixels(28)}).with_debug_name("diff_find_previous"))) step = -1;
        if (button(ctx, mk(bar.ent(), 2), preset::Button(">")
                .with_size(ComponentSize{pixels(28), pixels(28)}).with_debug_name("diff_find_next"))) step = 1;
        if (!shortcuts_blocked(*layout) && shortcut_owner(ctx, *filterRepo).input(reading::focus::Region::Find) &&
            afterhours::input::is_key_pressed(257)) step = afterhours::input::is_key_down(340) ? -1 : 1;
        find.pendingStep += step;
        if (count > 0) {
            find.index = (static_cast<int>(find.index % count) + find.pendingStep % count + count) % count;
            sess.findMatch = matchAt(find.index);
            sess.findQuery = find.query;
            const auto& match = *sess.findMatch;
            find.position = reading::ReadingAnchor{match.file, reading::anchor_revision(filterRepo->workspace().location()),
                match.sign == '-' ? reading::DiffSide::Before : reading::DiffSide::After,
                match.line, match.logicalColumn, .15f, match.sign};
            if (find.navigate || find.pendingStep != 0) {
                if (review) {
                    review->foldedFiles.erase(reviewScope + "\n" + match.file);
                    for (const auto& file : diffs) {
                        if (file.filePath != match.file) continue;
                        for (const auto& hunk : file.hunks) {
                            int start = match.sign == '-' ? hunk.oldStart : hunk.newStart;
                            int length = match.sign == '-' ? hunk.oldCount : hunk.newCount;
                            if (match.line >= start && match.line < start + length)
                                review->foldedHunks.erase(reviewScope + "\n" + ecs::ReviewComponent::hunk_key(file.filePath, hunk));
                        }
                    }
                }
                auto location = filterRepo->workspace().location();
                if (auto* source = std::get_if<reading::SourceLocation>(&location)) {
                    source->line = match.line;
                    source->column = match.logicalColumn;
                } else std::get<reading::ReviewLocation>(location).file = match.file;
                navigation::preview(*filterRepo, std::move(location), find.position);
                if (sourceFind && !filterRepo->fullFilePage.contains(match.line, match.logicalColumn,
                        reading::column_at_byte(find.query, find.query.size()) - 1)) {
                    filterRepo->fullFilePageRequest = {ecs::FilePageRequest::Action::TargetLine, {}, match.line,
                        sourceRuntime.result.sourceIdentity, 3, match.logicalColumn};
                    filterRepo->fullFileRequestedTargetLine = match.line;
                    filterRepo->fullFileRequestedTargetColumn = match.logicalColumn;
                    find.focus = true;
                }
            }
        }
        if (!sourceFind || (!sourceRuntime.future.valid() && !sourceRuntime.key.empty())) {
            find.navigate = false;
            find.pendingStep = 0;
        }
        div(ctx, mk(bar.ent(), 3), ComponentConfig{}.with_skip_grid_snap()
            .with_label(sourceFind && sourceRuntime.future.valid() ? "..." : count == 0 ? "0/0" : std::to_string(find.index + 1) + "/" + std::to_string(count))
            .with_size(ComponentSize{pixels(76), pixels(28)})
            .with_font_size(pixels(12)).with_debug_name("diff_find_count"));
        if (sourceFind && (sourceRuntime.result.limited || !sourceRuntime.result.error.empty()))
            div(ctx, mk(bar.ent(), 5), ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{pixels(width), pixels(24)})
                .with_absolute_position(0.f, 34.f).with_custom_background(theme::SIDEBAR_BG)
                .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_label(sourceRuntime.result.error.empty() ? "Limited to the first 5,000 matches" : sourceRuntime.result.error)
                .with_debug_name("source_find_notice"));
        if (button(ctx, mk(bar.ent(), 4), preset::Button("x")
                .with_size(ComponentSize{pixels(28), pixels(28)}).with_debug_name("diff_find_close")))
            navigation::close_find(*filterRepo);
    }
    if (!diffs.empty() && diffs.front().isFullContent && !(filterRepo && navigation::find(*filterRepo).open)) {
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
    sess.tmc = &EntityHelper::get_singleton_cmp_enforce<afterhours::ui::TextMeasureCache>();
    sess.fontSize = Settings::get().get_code_font_size() * zoom::get();
    bool selEnabled = reviewScope != "snapshot";
    if (!selEnabled) diff_sel::reset();
    if (selEnabled) {
        std::string context = repoPath + "\n" + (filterRepo ? std::to_string(filterRepo->workspace().active_id().value) : "") + "\n" + reviewScope + (sideBySide ? "\nsplit" : "\ninline") +
            std::to_string(contentWidth) + ":" + std::to_string(zoom::get()) + ":" + std::to_string(Settings::get().get_code_font_size()) +
            (sess.visibleWhitespace ? ":spaces" : ":plain");
        for (const auto& diff : diffs) if (fileVisible(diff)) context += "\n" + diff.filePath + diff_metrics().signature(diff);
        if (diff_sel::state().context != context) {
            diff_sel::reset();
            auto& selectionState = diff_sel::state();
            selectionState.context = std::move(context);
        }
        sess.enabled = true;
        sess.owner = filterRepo;
        sess.files = &diffs;
        if (filterRepo && !diffs.empty() && diffs.front().isFullContent && !filterRepo->workspace().document(filterRepo->workspace().active_id())->caret) {
            const auto& anchor = filterRepo->workspace().document(filterRepo->workspace().active_id())->anchor;
            navigation::set_caret(*filterRepo, {diffs.front().filePath, reading::DiffSide::After,
                anchor ? anchor->line : 1, anchor ? anchor->column : 1});
        }
        if (filterRepo) {
            auto& selectionState = diff_sel::state();
            const auto& document = *filterRepo->workspace().document(filterRepo->workspace().active_id());
            if (const auto& saved = document.selection) {
                selectionState.anchor = saved->anchor;
                selectionState.head = saved->head;
                selectionState.source = saved->source;
                selectionState.sourceIdentity = saved->sourceIdentity;
                selectionState.sourceGeneration = saved->dataGeneration;
                selectionState.extending = true;
            } else {
                selectionState.anchor = selectionState.head = document.caret.value_or(reading::CodePosition{});
                selectionState.extending = false;
            }
            diff_sel::recompute_highlight(selectionState);
        }
        diff_sel::handle_mouse(ctx, sess);
        if (layout) diff_sel::handle_keyboard(ctx, sess, diffs, *layout);
        if (filterRepo) {
            if (sess.findNavigate && navigation::find(*filterRepo).position) {
                const auto& point = *navigation::find(*filterRepo).position;
                navigation::set_selection(*filterRepo, {});
                navigation::set_caret(*filterRepo, {point.path, point.side, point.line, point.column});
            }
            sess.caret = filterRepo->workspace().document(filterRepo->workspace().active_id())->caret;
            const auto focusOwner = shortcut_owner(ctx, *filterRepo);
            sess.codeFocused = focusOwner.region == reading::focus::Region::Code && !focusOwner.text;
        }


        // Cmd+C copies the current selection (keyboard path; the header button
        // is the mouse path). 343/347 = L/R Super, 67 = 'C' (GLFW keycodes).
        bool superDown = afterhours::input::is_key_down(343) ||
                         afterhours::input::is_key_down(347) ||
                         afterhours::input::is_key_down(341);
        if (filterRepo && layout && reader_shortcuts(ctx, *filterRepo, *layout) && superDown && afterhours::input::is_key_pressed(67) &&
            diff_sel::state().hasSel) {
            const bool withLocation = afterhours::input::is_key_down(340) || afterhours::input::is_key_down(344);
            auto message = diff_sel::copy_selection(withLocation);
            if (!message.empty()) afterhours::toast::send_info(ctx, message, 4.f);
        }
        diff_sel::state().curLines.clear();
    }

    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);

    // When embedded, attach directly to parent; otherwise create our own scroll wrapper.
    // We always resolve contentParent to the entity that will own the diff rows.
    Entity* contentParent = &parent;
    float stickyHeight = diffs.empty() || diffs.front().isFullContent ? 0.f : 24.f;
    auto stickyHost = div(ctx, mk(findParent ? *findParent : parent, 593100), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{w, pixels(stickyHeight)}).with_custom_background(theme::WINDOW_BG)
        .with_flex_direction(FlexDirection::Row).with_no_wrap().with_align_items(AlignItems::Center));
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
        if (auto* repo = ownerRepo) {
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
            std::to_string(sess.fontSize) + ":" + std::to_string(zoom::get()) + ":" + std::to_string(sideBySide) +
            (sess.visibleWhitespace ? ":spaces" : ":plain");
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

    struct ContextLocation { double y; const ecs::FileDiff* file; const ecs::DiffHunk* hunk; };
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
        if (review && sess.findNavigate && sess.findMatch && sess.findMatch->file == fileDiff.filePath)
            review->foldedFiles.erase(fileFoldKey);
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
            repo && reviewScope != "snapshot" && repo->diffTargetFrames > 0 && repo->diffTargetFile() == fileDiff.filePath &&
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
                !fileDiff.isSubmodule && selEnabled && diff_sel::state().hasSel) {
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
                            diff_sel::source_point(fileDiff, visible_rect(*contentParent), reviewScope,
                                repo->workspace().document(repo->workspace().active_id())->caret));
                        return;
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
            std::string footer = "Initial context: " + std::to_string(filterRepo ? filterRepo->diffContext : 3) +
                " lines · " + language;
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
        if (scrollY <= contextLocations.front().y && !selectedFileOnly) label.clear();
        auto stickyLabel = div(ctx, mk(stickyHost.ent(), 0), ComponentConfig{}.with_skip_grid_snap().with_label(label)
            .with_size(ComponentSize{expand(), pixels(stickyHeight)}).with_font_size(pixels(12))
            .with_padding(Padding{.left = pixels(8), .right = pixels(8)})
            .with_custom_text_color(theme::TEXT_PRIMARY).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_debug_name("sticky_diff_context"));
        set_tooltip(stickyLabel.ent(), label);
    }
    if (stickyHeight > 0.f && filterRepo) {
        auto display = button(ctx, mk(stickyHost.ent(), 1), preset::Button(selectedFileOnly ? "Selected file" : "All files")
            .with_size(ComponentSize{pixels(96), pixels(24)}).with_font_size(pixels(12))
            .with_transparent_bg().with_debug_name("review_display_mode"));
        set_tooltip(display.ent(), "Choose selected file or all files");
        if (display) {
            remember_focus_origin(ctx, display.ent());
            const auto request = navigation::stamp(*filterRepo, "review-display-mode");
            std::vector<ContextMenuItem> choices;
            for (auto mode : {review_files::DisplayMode::SelectedFile, review_files::DisplayMode::AllFiles}) {
                choices.push_back(ContextMenuItem::item(mode == review_files::DisplayMode::SelectedFile ? "Selected file" : "All files",
                    [path = filterRepo->repoPath, request, mode, first = diffs.front().filePath] {
                        auto* active = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
                        if (!active || active->repoPath != path || !navigation::accepts(*active, request, "review-display-mode")) return;
                        navigation::set_review_display_mode(*active, mode);
                        if (mode == review_files::DisplayMode::SelectedFile && active->selectedFilePath().empty()) {
                            auto destination = active->workspace().review();
                            destination.file = first;
                            navigation::open(*active, destination);
                        }
                    }));
            }
            const auto rect = screen_rect(display.ent());
            show_context_menu(rect.x, rect.y + rect.height, std::move(choices));
        }
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
