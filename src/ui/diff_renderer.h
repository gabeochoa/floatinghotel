#pragma once

#include "../ecs/ui_imports.h"
#include "../git/git_commands.h"
#include "../settings.h"
#include "code_highlight.h"
#include <afterhours/src/core/text_cache.h>
#include <afterhours/src/plugins/clipboard.h>
#include <afterhours/src/plugins/toast.h>
#include <afterhours/src/plugins/ui/text_input/text_input.h>
#include <cmath>
#include <optional>
#include <unordered_map>

namespace ui {

inline std::vector<afterhours::ui::TextSpan> highlighted_code(
    const std::string& prefix, const std::string& content, const std::string& path,
    bool visibleWhitespace = false, bool hasNewline = true) {
    std::vector<afterhours::ui::TextSpan> spans{{prefix, theme::TEXT_SECONDARY}};
    for (const auto& token : code_highlight::tokenize(code_highlight::display_text(content, visibleWhitespace, true, hasNewline), path)) {
        auto color = theme::TEXT_PRIMARY;
        switch (token.kind) {
            case code_highlight::Kind::Plain: break;
            case code_highlight::Kind::Keyword: color = {125, 180, 255, 255}; break;
            case code_highlight::Kind::String: color = {220, 170, 135, 255}; break;
            case code_highlight::Kind::Number: color = {170, 205, 160, 255}; break;
            case code_highlight::Kind::Comment: color = {135, 170, 125, 255}; break;
        }
        spans.push_back({token.text, color});
    }
    return spans;
}

// ============================================================================
// Diff text selection (drag to select code, copy with file:line for AI review)
// ============================================================================
namespace diff_sel {

// Index in the inline label where the code content begins:
//   [oldNum:5][space][newNum:5][2 spaces][sign][space] = 15 chars, then content.
constexpr int CONTENT_START = 15;

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
    Entity* scrollParent = nullptr;
    bool enabled = false;
    afterhours::ui::TextMeasureCache* tmc = nullptr;
    float fontSize = 0.f;
    float padLeftPx = 0.f;
    // Ballroom review (working-tree diff only): Approve a hunk (= stage it) and
    // hide it until reset; Comment adds to the feedback basket.
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

inline float code_mw(const Session& s, const std::string& raw) {
    return mw(s, code_highlight::display_text(raw, s.visibleWhitespace));
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
    div(ctx, mk(entity, 90003), ComponentConfig{}
        .with_size(ComponentSize{pixels(width), percent(1.f)})
        .with_absolute_position(x, 0.f)
        .with_custom_background(deletion ? afterhours::Color{240, 100, 100, 90}
                                         : afterhours::Color{90, 230, 140, 80})
        .with_roundness(0.f)
        .with_debug_name("intraline_change"));
}

inline void render_find_match(UIContext<InputAction>& ctx, Entity& lineEntity,
                              Session& s, const std::string& content, float prefix) {
    size_t at = std::min(s.findMatch->column, content.size());
    float x = prefix + code_mw(s, content.substr(0, at));
    float width = code_mw(s, content.substr(at, s.findQuery.size()));
    div(ctx, mk(lineEntity, 90002), ComponentConfig{}
        .with_size(ComponentSize{pixels(width), percent(1.f)})
        .with_absolute_position(x, 0.f)
        .with_custom_background(afterhours::Color{230, 180, 30, 100})
        .with_roundness(0.f)
        .with_debug_name("diff_find_match"));
    if (s.findNavigate && s.embedded && s.scrollParent &&
        s.scrollParent->has<afterhours::ui::HasScrollView>()) {
        auto& sv = s.scrollParent->get<afterhours::ui::HasScrollView>();
        auto rect = afterhours::ui::detail::apply_scroll_offset(
            lineEntity, lineEntity.get<afterhours::ui::UIComponent>().rect());
        float target = sv.scroll_offset.y + rect.y -
                       s.scrollParent->get<afterhours::ui::UIComponent>().rect().y - 50.f;
        target = std::clamp(target, 0.f, std::max(0.f, sv.content_size.y - sv.viewport_or_zero().y));
        sv.scroll_offset.y = sv.scroll_target.y = sv.last_eased_offset.y = target;
    }
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
    for (int k = i1; k <= i2; ++k) {
        if (st.lastLines[k].side != st.lastLines[i1].side ||
            st.lastLines[k].filePath != st.lastLines[i1].filePath) continue;
        const std::string& c = st.lastLines[k].content;
        int a = std::min((k == i1) ? c1 : 0, (int)c.size());
        int b = std::min((k == i2) ? c2 : (int)c.size(), (int)c.size());
        if (emitted) out += "\n";
        out += c.substr(a, b - a);
        emitted = true;
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
        int n = (int)r.content.size();
        float best = 1e30f; int bc = 0;
        for (int c = 0; c <= n; ++c) {
            float d = std::fabs(code_mw(sess, r.content.substr(0, c)) - rel);
            if (d < best) { best = d; bc = c; }
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

} // namespace diff_sel

namespace diff_detail {

// Diff colors — all defined in theme.h, aliased here for brevity
const auto& DIFF_ADD_BG    = theme::DIFF_ADD_BG;
const auto& DIFF_DEL_BG    = theme::DIFF_DEL_BG;
const auto& HUNK_HEADER_BG = theme::DIFF_HUNK_BG;

constexpr float LINE_HEIGHT   = 18.0f;  // denser diff rows (mock)
constexpr float HUNK_HEADER_H = 24.0f;
constexpr float FILE_HEADER_H = 28.0f;
constexpr float DIFF_HEADER_H = 28.0f;
constexpr float CODE_PAD_LEFT = 8.0f;
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

// Right-pad a line number into a fixed-width gutter string.
inline std::string pad_gutter(const std::string& n, size_t width = 5) {
    if (n.empty()) return std::string(width, ' ');
    if (n.size() >= width) return n;
    return std::string(width - n.size(), ' ') + n;
}

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

// ----------------------------------------------------------------------------
// Row virtualization (inline diff only)
// ----------------------------------------------------------------------------
// A large diff builds one div per line EVERY frame — afterhours culls the draw
// but the build + autolayout + text-measure is O(total lines), the freeze
// cliff. We track a running content-Y as rows are emitted and only build the
// rows whose span intersects the visible scroll window (+1 screen overscan);
// skipped runs collapse into a single spacer div of equal height so the
// scroll container's content_size (and thus scrollbar) stays exact.
//
// Units: scroll_offset/content_size are in real pixels, but row heights are
// authored in 720-space (h720). We resolve each to px so curY matches the
// scroll offset. Embedded (commit-detail) diffs build fully (vp.active ==
// false) because they scroll with their parent.
struct DiffViewport {
    afterhours::ui::HasScrollView* scroll = nullptr;
    bool active = false;
    float screenH = 720.f;
    float contentWidth = 0.f;   // for spacer width; <=0 -> percent(1.0)
    float top = 0.f, bottom = 1e30f; // visible content-Y window (px, w/ overscan)
    float curY = 0.f;           // running content-Y of the next row (px)
    float pending = 0.f;        // height of skipped rows not yet flushed (px)

    float px(float raw720) const { return resolve_to_pixels(h720(raw720), screenH); }

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
            ComponentConfig{}
                .with_size(ComponentSize{w, pixels(pending)})
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
                              bool hasNewline = true) {
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

    // Format: "OldLn NewLn  <sign> content"
    // The dedicated sign column makes add/del/context scannable without
    // relying on background color alone.
    std::string label = diff_detail::pad_gutter(oldNum) + " "
                      + diff_detail::pad_gutter(newNum)
                      + "  " + sign + " " + content;

    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);
    auto lineDiv = div(ctx, mk(parent, id),
        ComponentConfig{}
            .with_size(ComponentSize{w, h720(diff_detail::LINE_HEIGHT)})
            .with_custom_background(bgColor)
            .with_custom_text_color(textColor)
            .with_styled_label(highlighted_code(label.substr(0, label.size() - content.size()), content, filePath,
                                                sel && sel->visibleWhitespace, hasNewline))
            .with_font("mono", h720(theme::layout::FONT_CODE))
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = h720(0), .right = w1280(0),
                .bottom = h720(0), .left = w1280(diff_detail::CODE_PAD_LEFT)})
            .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_roundness(0.0f)
            .with_debug_name("diff_line"));

    if (sel && sel->enabled) {
        // Register this line (using the prior frame's resolved rect) so the next
        // frame can hit-test drags and the copy action can extract text.
        Rectangle r = afterhours::ui::detail::apply_scroll_offset(
            lineDiv.ent(), lineDiv.ent().get<afterhours::ui::UIComponent>().rect());
        float prefixW = diff_sel::mw(*sel, label.substr(0, diff_sel::CONTENT_START));
        float cx0 = r.x + sel->padLeftPx + prefixW;
        diff_sel::render_changed_range(ctx, lineDiv.ent(), *sel, content,
                                       sel->padLeftPx + prefixW, changed, prefix == '-');
        int lno = !newNum.empty() ? std::stoi(newNum)
                                  : (!oldNum.empty() ? std::stoi(oldNum) : 0);
        diff_sel::state().curLines.push_back(
            {lineDiv.ent().id, content, filePath, lno, r, cx0});
        if (diff_sel::found_line(sel, filePath, lno, prefix))
            diff_sel::render_find_match(ctx, lineDiv.ent(), *sel, content, sel->padLeftPx + prefixW);

        // Draw the selection highlight for the covered column range, if any.
        auto it = diff_sel::state().hl.find(lineDiv.ent().id);
        if (it != diff_sel::state().hl.end()) {
            int n = static_cast<int>(content.size());
            int a = std::min(it->second.first, n);
            int b = std::min(it->second.second, n);
            float x0 = sel->padLeftPx + prefixW + diff_sel::code_mw(*sel, content.substr(0, a));
            float x1 = sel->padLeftPx + prefixW + diff_sel::code_mw(*sel, content.substr(0, b));
            if (x1 > x0) {
                // Translucent selection overlay. afterhours now alpha-blends div
                // backgrounds, so a low-alpha box drawn OVER the already-rendered
                // line tints the selected span while the text shows through — no
                // need to re-draw the text on an opaque box anymore.
                div(ctx, mk(lineDiv.ent(), 90001),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(x1 - x0),
                                                 h720(diff_detail::LINE_HEIGHT)})
                        .with_absolute_position(x0, 0.f)
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
        // Approved hunks are hidden until reset (⟳/refresh).
        if (sel->review->approvedHunks.count(hkey))
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
            if (sel->reviewScope == "wt") {
                auto res = git::stage_hunk(sel->repoPath, fileDiff, hunk);
                if (res.success()) {
                    sel->review->approvedHunks.insert(hkey);
                    sel->review->dirty = true;
                    auto* r =
                        ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
                    if (r) r->refreshRequested = true;
                    afterhours::toast::send_info(ctx, "Approved hunk (staged)", 1.5f);
                    return;
                }
            }
        }
        if (isCursor && sel->review->cursorComment) {
            sel->review->cursorComment = false;
            sel->review->composingKey = hkey;
            sel->review->composingText.clear();
            sel->review->composingFile = fileDiff.filePath;
            sel->review->composingScope = sel->reviewScope;
            sel->review->composingLine = hunk.newStart;
        }
    }

    // Hunk header row: label + copy button. Culled like the lines: a diff
    // with thousands of hunks used to build every header (six entities each)
    // no matter what was on screen, which dwarfed the culled line rows.
    int hunkHeaderId = nextId++;
    const bool headerVisible =
        !vp || !vp->active || vp->visible(diff_detail::HUNK_HEADER_H);
    if (!headerVisible) {
        vp->skipped(diff_detail::HUNK_HEADER_H);
    } else {
    if (vp) vp->flush(ctx, parent, nextId);
    auto hunkRow = div(ctx, mk(parent, hunkHeaderId),
        ComponentConfig{}
            .with_size(ComponentSize{w, h720(diff_detail::HUNK_HEADER_H)})
            .with_flex_direction(FlexDirection::Row)
            .with_justify_content(JustifyContent::SpaceBetween)
            .with_align_items(AlignItems::Center)
            .with_custom_background(isCursor ? afterhours::Color{38, 79, 140, 255}
                                             : diff_detail::HUNK_HEADER_BG)
            .with_roundness(0.0f)
            .with_debug_name("hunk_header_row"));
    if (vp) vp->built(diff_detail::HUNK_HEADER_H);

    // The label takes what the action cluster (Copy/Comment/Approve) leaves.
    // This used to subtract a hardcoded reserve, because percent(1.0) took the
    // whole row and shoved the buttons off-screen.
    div(ctx, mk(hunkRow.ent(), 0),
        ComponentConfig{}
            .with_label(hunk.header)
            .with_size(ComponentSize{afterhours::ui::expand(), percent(1.0f)})
            .with_custom_text_color(theme::DIFF_HUNK_HEADER)
            .with_font("mono", h720(theme::layout::FONT_CODE))
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = h720(4), .right = w1280(0),
                .bottom = h720(4), .left = w1280(12)})
            .with_debug_name("hunk_header_label"));

    // Buttons live in a right-aligned group so they cluster together instead of
    // being spread apart by the row's SpaceBetween.
    auto hunkBtns = div(ctx, mk(hunkRow.ent(), 9),
        ComponentConfig{}
            .with_size(ComponentSize{children(), percent(1.0f)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_gap(pixels(6))
            .with_margin(Margin{.right = w1280(8)})
            .with_transparent_bg()
            .with_roundness(0.0f)
            .with_debug_name("hunk_header_btns"));

    // Copy button only where drag-select-to-copy isn't available (i.e. the
    // embedded commit-detail diff). In the working-tree diff, select-to-copy
    // (with file:line) replaces it.
    if (!(sel && sel->enabled)) {
        std::string hunkText = diff_detail::hunk_to_text(hunk);
        auto copyBtn = button(ctx, mk(hunkBtns.ent(), 1),
            preset::Button("Copy")
                .with_size(ComponentSize{children(), h720(18)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_custom_background(afterhours::Color{78, 78, 86, 255})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(afterhours::ui::FontSize::Small)
                .with_debug_name("copy_hunk_btn"));
        if (copyBtn) {
            afterhours::clipboard::set_text(hunkText);
            afterhours::toast::send_info(ctx, "Copied hunk to clipboard", 1.5f);
        }
    }

    // Approve = stage this hunk (git apply --cached) and hide it until reset.
    // Comment = open an inline compose row and add the note to the basket.
    if (reviewOn) {
        // Approve = stage (working-tree only; committed hunks can't be staged).
        if (sel->reviewScope == "wt") {
            auto approveBtn = button(ctx, mk(hunkBtns.ent(), 2),
                preset::Button("Approve")
                    .with_size(ComponentSize{children(), h720(18)})
                    .with_padding(Padding{
                        .top = h720(2), .right = w1280(8),
                        .bottom = h720(2), .left = w1280(8)})
                    .with_custom_background(theme::BUTTON_SECONDARY)
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_debug_name("approve_hunk_btn"));
            if (approveBtn) {
                // A submodule hunk is a gitlink pointer change; build_patch can't
                // represent it, so stage the whole path instead of a hunk patch.
                auto res = fileDiff.isSubmodule
                    ? git::stage_file(sel->repoPath, fileDiff.filePath)
                    : git::stage_hunk(sel->repoPath, fileDiff, hunk);
                if (res.success()) {
                    sel->review->approvedHunks.insert(hkey);
                    sel->review->dirty = true;
                    auto* r =
                        ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
                    if (r) r->refreshRequested = true;
                    afterhours::toast::send_info(ctx, "Approved hunk (staged)", 1.5f);
                } else {
                    afterhours::toast::send_info(
                        ctx, "Approve failed: " + diff_detail::git_err(res), 2.5f);
                }
            }
        }
        auto commentBtn = button(ctx, mk(hunkBtns.ent(), 3),
            preset::Button("Comment")
                .with_size(ComponentSize{children(), h720(18)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(afterhours::ui::FontSize::Small)
                .with_debug_name("comment_hunk_btn"));
        if (commentBtn) {
            sel->review->composingKey = hkey;
            sel->review->composingText.clear();
            sel->review->composingFile = fileDiff.filePath;
            sel->review->composingScope = sel->reviewScope;
            sel->review->composingLine = hunk.newStart;
        }
    }
    } // headerVisible

    // Inline compose row for this hunk.
    if (reviewOn && sel->review->composingKey == hkey) {
        if (vp) { vp->flush(ctx, parent, nextId); vp->built(diff_detail::COMMENT_COMPOSE_H); }
        auto composeRow = div(ctx, mk(parent, nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{w, h720(diff_detail::COMMENT_COMPOSE_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_custom_background(afterhours::Color{35, 35, 39, 255})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(12)})
                .with_debug_name("comment_compose_row"));
        float screenH = static_cast<float>(afterhours::graphics::get_screen_height());
        float editorH = resolve_to_pixels(h720(diff_detail::COMMENT_COMPOSE_H - 8.f), screenH);
        afterhours::text_input::text_area(
            ctx, mk(composeRow.ent(), 0), sel->review->composingText,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(std::max(80.f, contentWidth - 100.f)), pixels(editorH)})
                .with_custom_background(theme::INPUT_BG)
                .with_font("mono", h720(14.f))
                .with_line_height(pixels(resolve_to_pixels(h720(22.f), screenH)))
                .with_word_wrap(true)
                .with_overflow(afterhours::ui::Overflow::Hidden)
                .with_corner_radius(4.0f)
                .with_debug_name("comment_input"));
        auto addBtn = button(ctx, mk(composeRow.ent(), 1),
            preset::Button("Add")
                .with_size(ComponentSize{children(), h720(18)})
                .with_font_size(afterhours::ui::FontSize::Small)
                .with_debug_name("comment_add_btn"));
        if (addBtn)
            ecs::commit_pending_comment(*sel->review);
    }

    // Folded (commented) hunks collapse — show a marker instead of the lines.
    if (reviewOn && sel->review->foldedHunks.count(hkey)) {
        if (vp) { vp->flush(ctx, parent, nextId); vp->built(20.0f); }
        auto expand = button(ctx, mk(parent, nextId++),
            ComponentConfig{}
                .with_label("\xe2\x9c\x8e commented \xc2\xb7 click to expand")
                .with_size(ComponentSize{w, h720(20)})
                .with_custom_text_color(theme::STATUS_MODIFIED)
                .with_font_size(afterhours::ui::FontSize::Small)
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(52)})
                .with_debug_name("hunk_folded_marker"));
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
    for (auto& line : hunk.lines) {
        // Always consume an id per line so a given line keeps a stable entity
        // id across frames whether or not it's built (avoids scroll churn).
        int lineId = nextId++;
        char findSign = line.empty() ? ' ' : line.front();
        if (sel && sel->findNavigate && vp &&
            diff_sel::found_line(sel, fileDiff.filePath, findSign == '-' ? oldLine : newLine, findSign))
            vp->reveal();
        if (!vp || !vp->active) {
            render_diff_line(ctx, parent, lineId, line, oldLine, newLine,
                             lineWidth > 0 ? lineWidth : contentWidth, fileDiff.filePath, sel,
                             changedRanges[static_cast<size_t>(&line - hunk.lines.data())],
                             !hunk.noNewline.contains(static_cast<size_t>(&line - hunk.lines.data())));
        } else if (vp->visible(diff_detail::LINE_HEIGHT)) {
            vp->flush(ctx, parent, nextId);
            render_diff_line(ctx, parent, lineId, line, oldLine, newLine,
                             lineWidth > 0 ? lineWidth : contentWidth, fileDiff.filePath, sel,
                             changedRanges[static_cast<size_t>(&line - hunk.lines.data())],
                             !hunk.noNewline.contains(static_cast<size_t>(&line - hunk.lines.data())));
            vp->built(diff_detail::LINE_HEIGHT);
        } else {
            // Offscreen: advance line-number counters so gutters stay correct
            // when this line scrolls into view, but don't build the div.
            char prefix = line.empty() ? ' ' : line[0];
            if (prefix == '+') ++newLine;
            else if (prefix == '-') ++oldLine;
            else { ++oldLine; ++newLine; }
            vp->skipped(diff_detail::LINE_HEIGHT);
        }
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
                            bool hasNewline = true) {
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

    std::string label = pad_gutter(num) + "  " + sign + " " + content;

    auto cfg = ComponentConfig{}
        .with_size(ComponentSize{percent(0.5f), h720(LINE_HEIGHT)})
        .with_custom_background(bg)
        .with_custom_text_color(fg)
        .with_styled_label(highlighted_code(label.substr(0, label.size() - content.size()), content, filePath,
                                            sel && sel->visibleWhitespace && kind != SbsKind::Empty, hasNewline))
        .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
        .with_font("mono", h720(theme::layout::FONT_CODE))
        .with_alignment(TextAlignment::Left)
        .with_padding(Padding{
            .top = h720(0), .right = w1280(0),
            .bottom = h720(0), .left = w1280(CODE_PAD_LEFT)})
        .with_roundness(0.0f)
        .with_debug_name("sbs_cell");
    if (leftBorder) cfg = cfg.with_border_right(theme::BORDER);
    auto cell = div(ctx, mk(row, id), cfg);
    if (sel && sel->enabled && kind != SbsKind::Empty) {
        auto rect = afterhours::ui::detail::apply_scroll_offset(
            cell.ent(), cell.ent().get<afterhours::ui::UIComponent>().rect());
        float prefix = sel->padLeftPx + diff_sel::mw(*sel, label.substr(0, label.size() - content.size()));
        diff_sel::render_changed_range(ctx, cell.ent(), *sel, content, prefix, changed, kind == SbsKind::Del);
        diff_sel::state().curLines.push_back(
            {cell.ent().id, content, filePath, num.empty() ? 0 : std::stoi(num),
             rect, rect.x + prefix, leftBorder ? 1 : 2});
        if (diff_sel::found_line(sel, filePath, num.empty() ? 0 : std::stoi(num), sign) &&
            (kind != SbsKind::Context || !leftBorder))
            diff_sel::render_find_match(ctx, cell.ent(), *sel, content, prefix);
        auto it = diff_sel::state().hl.find(cell.ent().id);
        if (it != diff_sel::state().hl.end()) {
            auto a = std::min(static_cast<size_t>(it->second.first), content.size());
            auto b = std::min(static_cast<size_t>(it->second.second), content.size());
            float x0 = prefix + diff_sel::code_mw(*sel, content.substr(0, a));
            float x1 = prefix + diff_sel::code_mw(*sel, content.substr(0, b));
            div(ctx, mk(cell.ent(), 90001), ComponentConfig{}
                .with_size(ComponentSize{pixels(std::max(0.f, x1 - x0)), h720(LINE_HEIGHT)})
                .with_absolute_position(x0, 0.f)
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
    if (culling && !vp->visible(diff_detail::HUNK_HEADER_H)) {
        vp->skipped(diff_detail::HUNK_HEADER_H);
    } else {
    if (culling) vp->flush(ctx, parent, nextId);
    auto hunkRow = div(ctx, mk(parent, hunkHeaderId),
        ComponentConfig{}
            .with_size(ComponentSize{w, h720(diff_detail::HUNK_HEADER_H)})
            .with_flex_direction(FlexDirection::Row)
            .with_justify_content(JustifyContent::SpaceBetween)
            .with_align_items(AlignItems::Center)
            .with_custom_background(diff_detail::HUNK_HEADER_BG)
            .with_roundness(0.0f)
            .with_debug_name("sbs_hunk_header_row"));
    if (culling) vp->built(diff_detail::HUNK_HEADER_H);
    div(ctx, mk(hunkRow.ent(), 0),
        ComponentConfig{}
            .with_label(hunk.header)
            .with_size(ComponentSize{afterhours::ui::expand(), percent(1.0f)})
            .with_custom_text_color(theme::DIFF_HUNK_HEADER)
            .with_font("mono", h720(theme::layout::FONT_CODE))
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = h720(4), .right = w1280(0),
                .bottom = h720(4), .left = w1280(12)})
            .with_debug_name("sbs_hunk_header_label"));
    {
        std::string hunkText = diff_detail::hunk_to_text(hunk);
        auto copyBtn = button(ctx, mk(hunkRow.ent(), 1),
            preset::Button("Copy")
                .with_size(ComponentSize{children(), h720(18)})
                .with_margin(Margin{.right = w1280(8)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_custom_background(afterhours::Color{60, 60, 65, 255})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(afterhours::ui::FontSize::Small)
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
    int oldNumber = oldLine, newNumber = newLine;
    for (size_t i = 0; i < hunk.lines.size(); ++i) {
        char sign = hunk.lines[i].empty() ? ' ' : hunk.lines[i].front();
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
        // Same id-per-row discipline as the inline path so a row keeps its
        // entity across frames whether or not it was built.
        int rowId = nextId++;
        if (sel && sel->findNavigate && vp &&
            ((!lNum.empty() && diff_sel::found_line(sel, fileDiff.filePath, std::stoi(lNum),
                                                   lKind == SbsKind::Del ? '-' : ' ')) ||
             (!rNum.empty() && diff_sel::found_line(sel, fileDiff.filePath, std::stoi(rNum),
                                                   rKind == SbsKind::Add ? '+' : ' '))))
            vp->reveal();
        if (culling) {
            if (!vp->visible(diff_detail::LINE_HEIGHT)) {
                vp->skipped(diff_detail::LINE_HEIGHT);
                return;
            }
            vp->flush(ctx, parent, nextId);
            vp->built(diff_detail::LINE_HEIGHT);
        }
        auto rowDiv = div(ctx, mk(parent, rowId),
            ComponentConfig{}
                .with_size(ComponentSize{w, h720(diff_detail::LINE_HEIGHT)})
                .with_flex_direction(FlexDirection::Row)
                .with_roundness(0.0f)
                .with_debug_name("sbs_row"));
        std::pair<code_highlight::Range, code_highlight::Range> changes;
        if (lKind == SbsKind::Del && rKind == SbsKind::Add)
            changes = code_highlight::changed_ranges(lContent, rContent);
        diff_detail::render_sbs_cell(ctx, rowDiv.ent(), 0, lNum, lContent, lKind, true, fileDiff.filePath, sel, changes.first,
                                     lNum.empty() || !oldNoNewline.contains(std::stoi(lNum)));
        diff_detail::render_sbs_cell(ctx, rowDiv.ent(), 1, rNum, rContent, rKind, false, fileDiff.filePath, sel, changes.second,
                                     rNum.empty() || !newNoNewline.contains(std::stoi(rNum)));
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
inline void render_diff(UIContext<InputAction>& ctx,
                        Entity& parent,
                        const std::vector<ecs::FileDiff>& diffs,
                        float contentWidth, float contentHeight,
                        bool embedInParentScroll = false,
                        bool resetScroll = false,
                        bool sideBySide = false,
                        const std::string& repoPath = "",
                        ecs::ReviewComponent* review = nullptr,
                        const std::string& reviewScope = "wt") {
    int nextId = diff_detail::BASE_ID;

    diff_sel::Session sess;
    // Working-tree diffs always open in the approve-chunk flow (Approve/Comment
    // per hunk). Committed diffs are read-only unless you've embarked a review,
    // in which case they become comment-only (comments become fixups).
    sess.reviewActions =
        (review != nullptr) && (reviewScope == "wt" || review->reviewing);
    sess.embedded = embedInParentScroll;
    sess.repoPath = repoPath;
    sess.review = review;
    sess.reviewScope = reviewScope;
    auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
    sess.visibleWhitespace = layout && layout->visibleWhitespace;
    float findHeight = 0.f;
    if (layout && layout->diffFindOpen) {
        findHeight = 34.f;
        auto bar = div(ctx, mk(parent, 580001), ComponentConfig{}
            .with_size(ComponentSize{pixels(contentWidth), pixels(findHeight)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_debug_name("diff_find_bar"));
        auto previous = layout->diffFindQuery;
        auto input = afterhours::text_input::text_input(ctx, mk(bar.ent(), 0), layout->diffFindQuery,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(std::max(80.f, contentWidth - 240.f)), pixels(28)})
                .with_debug_name("diff_find_input"));
        if (layout->diffFindFocus) {
            ctx.set_focus(input.ent().id);
            layout->diffFindFocus = false;
        }
        if (previous != layout->diffFindQuery) {
            layout->diffFindIndex = 0;
            layout->diffFindNavigate = 3;
        }
        auto matches = ecs::find_diff_matches(diffs, layout->diffFindQuery);
        int count = static_cast<int>(matches.size());
        int step = 0;
        if (button(ctx, mk(bar.ent(), 1), preset::Button("Previous")
                .with_size(ComponentSize{pixels(70), pixels(28)}).with_debug_name("diff_find_previous"))) step = -1;
        if (button(ctx, mk(bar.ent(), 2), preset::Button("Next")
                .with_size(ComponentSize{pixels(48), pixels(28)}).with_debug_name("diff_find_next"))) step = 1;
        if (afterhours::input::is_key_pressed(257))
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
        div(ctx, mk(bar.ent(), 3), ComponentConfig{}
            .with_label(count == 0 ? "No matches" : std::to_string(layout->diffFindIndex + 1) + "/" + std::to_string(count))
            .with_size(ComponentSize{pixels(80), pixels(28)})
            .with_font_size(FontSize::Small).with_debug_name("diff_find_count"));
        if (button(ctx, mk(bar.ent(), 4), preset::Button("x")
                .with_size(ComponentSize{pixels(28), pixels(28)}).with_debug_name("diff_find_close")))
            layout->diffFindOpen = false;
    }
    bool selEnabled = true;
    if (selEnabled) {
        std::string context = repoPath + "\n" + reviewScope + (sideBySide ? "\nsplit" : "\ninline");
        for (const auto& diff : diffs) context += "\n" + diff.filePath + diff_signature(diff);
        if (diff_sel::state().context != context) {
            diff_sel::reset();
            diff_sel::state().context = std::move(context);
        }
        sess.enabled = true;
        sess.tmc = &EntityHelper::get_singleton_cmp_enforce<
            afterhours::ui::TextMeasureCache>();
        float screenH = (float)afterhours::graphics::get_screen_height();
        float screenW = (float)afterhours::graphics::get_screen_width();
        sess.fontSize = resolve_to_pixels(h720(theme::layout::FONT_CODE), screenH);
        sess.padLeftPx =
            resolve_to_pixels(w1280(diff_detail::CODE_PAD_LEFT), screenW);
        diff_sel::handle_mouse(ctx, sess); // update selection from prior frame

        // Cmd+C copies the current selection (keyboard path; the header button
        // is the mouse path). 343/347 = L/R Super, 67 = 'C' (GLFW keycodes).
        bool superDown = afterhours::input::is_key_down(343) ||
                         afterhours::input::is_key_down(347) ||
                         afterhours::input::is_key_down(341);
        if (superDown && afterhours::input::is_key_pressed(67) &&
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
    if (!embedInParentScroll) {
        auto h = contentHeight > 0
                     ? pixels(contentHeight - diff_detail::DIFF_HEADER_H - findHeight)
                     : (findHeight > 0 ? pixels(std::max(40.f, parent.get<afterhours::ui::UIComponent>().rect().height - findHeight))
                                       : percent(1.0f));
        auto scrollContainer = div(ctx, mk(parent, nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{w, h})
                .with_overflow(Overflow::Scroll)
                .with_flex_direction(FlexDirection::Column)
                .with_no_wrap()  // scroll list stacks; never wrap into a 2nd column
                .with_custom_background(theme::PANEL_BG)
                .with_roundness(0.0f)
                .with_debug_name("diff_scroll"));
        if (resetScroll && scrollContainer.ent().has<afterhours::ui::HasScrollView>()) {
            scrollContainer.ent().get<afterhours::ui::HasScrollView>().scroll_offset = {0, 0};
        }
        contentParent = &scrollContainer.ent();
    }
    sess.scrollParent = contentParent;
    float codeWidth = contentWidth;
    for (const auto& file : diffs)
        for (const auto& hunk : file.hunks)
            for (const auto& line : hunk.lines) {
                float width = diff_sel::mw(sess, code_highlight::display_text(line, sess.visibleWhitespace, true) +
                                                std::string(sideBySide ? 10 : 16, ' ')) + (sess.visibleWhitespace ? 90.f : 24.f);
                codeWidth = std::max(codeWidth, sideBySide ? width * 2.f : width);
            }

    // Virtualize the diff: only build rows in the visible scroll window
    // (+1 screen overscan). Read the prior frame's scroll offset/viewport from
    // the persistent scroll container. Embedded (commit-detail) diffs build
    // fully since they scroll with their parent.
    diff_detail::DiffViewport vp;
    if (!embedInParentScroll) {
        vp.active = true;
        vp.screenH = (float)afterhours::graphics::get_screen_height();
        vp.contentWidth = codeWidth;
        float scrollY = 0.f, viewportH = 0.f;
        if (contentParent->has<afterhours::ui::HasScrollView>()) {
            auto& sv = contentParent->get<afterhours::ui::HasScrollView>();
            vp.scroll = &sv;
            scrollY = sv.scroll_offset.y;
            viewportH = sv.viewport_or_zero().y;
        }
        if (viewportH <= 0.f)
            viewportH = contentHeight > 0
                            ? (contentHeight - diff_detail::DIFF_HEADER_H)
                            : vp.screenH;
        float overscan = viewportH; // one screen of pre-built rows each side
        vp.top = scrollY - overscan;
        vp.bottom = scrollY + viewportH + overscan;
    }

    // Stats summary header inside scroll. Suppressed when embedded in the
    // commit-detail view, which already renders its own "FILES CHANGED" summary.
    if (!embedInParentScroll) {
        int totalAdditions = 0, totalDeletions = 0;
        for (auto& d : diffs) {
            totalAdditions += d.additions;
            totalDeletions += d.deletions;
        }
        std::string stats = std::to_string(diffs.size()) + " file"
            + (diffs.size() != 1 ? "s" : "") + " changed  +"
            + std::to_string(totalAdditions) + "  -"
            + std::to_string(totalDeletions);

        // Header row: stats label on the left, Inline/Side-by-Side segmented
        // toggle on the right. The toggle is only shown in the main diff view
        // (not the embedded commit-detail diff).
        vp.flush(ctx, *contentParent, nextId);
        auto statsRow = div(ctx, mk(*contentParent, nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(diff_detail::DIFF_HEADER_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_align_items(AlignItems::Center)
                .with_custom_background(afterhours::Color{35, 35, 38, 255})
                .with_roundness(0.0f)
                .with_debug_name("diff_stats_header"));
        vp.built(diff_detail::DIFF_HEADER_H);

        div(ctx, mk(statsRow.ent(), 0),
            ComponentConfig{}
                .with_size(ComponentSize{children(), percent(1.0f)})
                .with_padding(Padding{
                    .top = h720(6), .right = w1280(12),
                    .bottom = h720(4), .left = w1280(12)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_label(stats)
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("diff_stats_label"));

        if (!embedInParentScroll) {
            auto toggle = div(ctx, mk(statsRow.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{children(), h720(diff_detail::DIFF_HEADER_H - 6)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_margin(Margin{.right = w1280(10)})
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("diff_mode_toggle"));

            auto segBtn = [&](int id, const char* label, bool active) -> bool {
                afterhours::Color bg = active ? theme::BUTTON_PRIMARY
                                              : theme::BUTTON_SECONDARY;
                afterhours::Color fg = active ? afterhours::Color{255, 255, 255, 255}
                                              : theme::TEXT_SECONDARY;
                return (bool)button(ctx, mk(toggle.ent(), id),
                    preset::Button(label)
                        .with_size(ComponentSize{children(), percent(1.0f)})
                        .with_padding(Padding{
                            .top = h720(2), .right = w1280(12),
                            .bottom = h720(2), .left = w1280(12)})
                        .with_custom_background(bg)
                        .with_custom_text_color(fg)
                        .with_font_size(afterhours::ui::FontSize::Small)
                        .with_roundness(0.0f)
                        .with_debug_name(active ? "diff_mode_active"
                                                : "diff_mode_inactive"));
            };

            bool inlineClicked = segBtn(0, "Inline", !sideBySide);
            bool sbsClicked = segBtn(1, "Side-by-Side", sideBySide);
            if (inlineClicked || sbsClicked) {
                if (auto* l = ecs::find_singleton<ecs::LayoutComponent>()) {
                    l->diffViewMode = inlineClicked
                        ? ecs::LayoutComponent::DiffViewMode::Inline
                        : ecs::LayoutComponent::DiffViewMode::SideBySide;
                }
            }

            // Feedback basket show/hide toggle (only when comments are queued).
            if (review && !review->comments.empty()) {
                bool open = review->basketOpen;
                auto fbBtn = button(ctx, mk(toggle.ent(), 2),
                    preset::Button("Feedback (" +
                        std::to_string(review->comments.size()) + ")")
                        .with_size(ComponentSize{children(), percent(1.0f)})
                        .with_margin(Margin{.left = w1280(8)})
                        .with_padding(Padding{
                            .top = h720(2), .right = w1280(12),
                            .bottom = h720(2), .left = w1280(12)})
                        .with_custom_background(open
                            ? afterhours::Color{51, 42, 24, 255}
                            : theme::BUTTON_SECONDARY)
                        .with_custom_text_color(theme::STATUS_MODIFIED)
                        .with_font_size(afterhours::ui::FontSize::Small)
                        .with_roundness(0.0f)
                        .with_debug_name("basket_toggle_btn"));
                if (fbBtn) review->basketOpen = !review->basketOpen;
            }
        }
    }

    for (auto& fileDiff : diffs) {
        // File header bar
        std::string fileLabel = fileDiff.filePath;
        if (fileDiff.isRenamed && !fileDiff.oldPath.empty()) {
            fileLabel = fileDiff.oldPath + " -> " + fileDiff.filePath;
        }

        std::string statsLabel;
        if (fileDiff.additions > 0) {
            statsLabel += "+" + std::to_string(fileDiff.additions);
        }
        if (fileDiff.deletions > 0) {
            if (!statsLabel.empty()) statsLabel += " ";
            statsLabel += "-" + std::to_string(fileDiff.deletions);
        }
        if (!statsLabel.empty()) {
            fileLabel += "  " + statsLabel;
        }

        if (fileDiff.isNew) {
            fileLabel += "  (new file)";
        } else if (fileDiff.isDeleted) {
            fileLabel += "  (deleted)";
        } else if (fileDiff.isBinary) {
            fileLabel += "  (binary)";
        }

        vp.flush(ctx, *contentParent, nextId);
        int fileHeaderRowId = nextId++;
        auto fileHeaderRow = div(ctx, mk(*contentParent, fileHeaderRowId),
            ComponentConfig{}
                .with_size(ComponentSize{w, h720(diff_detail::FILE_HEADER_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_justify_content(JustifyContent::SpaceBetween)
                .with_align_items(AlignItems::Center)
                .with_custom_background(theme::SIDEBAR_BG)
                .with_border_bottom(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("file_header_row"));
        vp.built(diff_detail::FILE_HEADER_H);

        // Working-tree file header gets an "Approve file" button (stages the
        // whole file). No reserve to compute: the label expands into whatever
        // the action cluster leaves, however many buttons it ends up holding.
        bool showApproveFile = sess.reviewActions && sess.reviewScope == "wt";
        div(ctx, mk(fileHeaderRow.ent(), 0),
            ComponentConfig{}
                .with_label(fileLabel)
                .with_size(ComponentSize{afterhours::ui::expand(), percent(1.0f)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(0),
                    .bottom = h720(8), .left = w1280(16)})
                .with_debug_name("file_header_label"));

        // Right-aligned action cluster: [Approve file] [Copy Diff].
        auto fileBtns = div(ctx, mk(fileHeaderRow.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{children(), percent(1.0f)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_no_wrap()
                .with_gap(pixels(6))
                .with_margin(Margin{.right = w1280(12)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("file_header_btns"));

        if (showApproveFile) {
            auto approveFileBtn = button(ctx, mk(fileBtns.ent(), 0),
                preset::Button("Approve file")
                    .with_size(ComponentSize{children(), h720(18)})
                    .with_padding(Padding{
                        .top = h720(2), .right = w1280(8),
                        .bottom = h720(2), .left = w1280(8)})
                    .with_custom_background(theme::BUTTON_SECONDARY)
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_debug_name("approve_file_btn"));
            if (approveFileBtn) {
                auto res = git::stage_file(repoPath, fileDiff.filePath);
                if (res.success()) {
                    auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
                    if (r) r->refreshRequested = true;
                    afterhours::toast::send_info(ctx, "Approved file (staged)", 1.5f);
                } else {
                    afterhours::toast::send_info(
                        ctx, "Approve failed: " + diff_detail::git_err(res), 2.5f);
                }
            }
        }

        {
            std::string diffText = diff_detail::file_diff_to_text(fileDiff);
            auto fileCopyBtn = button(ctx, mk(fileBtns.ent(), 1),
                preset::Button("Copy Diff")
                    .with_size(ComponentSize{children(), h720(18)})
                    .with_padding(Padding{
                        .top = h720(2), .right = w1280(8),
                        .bottom = h720(2), .left = w1280(8)})
                    .with_custom_background(afterhours::Color{92, 92, 100, 255})
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_debug_name("copy_file_diff_btn"));
            if (fileCopyBtn) {
                afterhours::clipboard::set_text(diffText);
                afterhours::toast::send_info(ctx, "Copied diff to clipboard", 1.5f);
            }
        }

        // Binary files: just show the header, no hunks
        if (fileDiff.isBinary) {
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{w, h720(24)})
                    .with_custom_background(theme::PANEL_BG)
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_label("Binary file not shown")
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Center)
                    .with_padding(Padding{
                        .top = h720(4), .right = w1280(8),
                        .bottom = h720(4), .left = w1280(8)})
                    .with_roundness(0.0f)
                    .with_debug_name("binary_notice"));
            vp.built(24.0f);
            continue;
        }

        // Render each hunk (passing contentWidth for proper sizing)
        for (auto& hunk : fileDiff.hunks) {
            render_hunk(ctx, *contentParent, fileDiff, hunk, nextId,
                        contentWidth, &sess, &vp, sideBySide, codeWidth);
        }

        // Spacer between files
        if (&fileDiff != &diffs.back()) {
            vp.flush(ctx, *contentParent, nextId);
            div(ctx, mk(*contentParent, nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{w, h720(8)})
                    .with_custom_background(theme::PANEL_BG)
                    .with_roundness(0.0f)
                    .with_debug_name("file_spacer"));
            vp.built(8.0f);
        }
    }

    // Flush any trailing skipped rows so the content_size (scrollbar extent)
    // reflects the full diff height, not just what was built.
    vp.flush(ctx, *contentParent, nextId);

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
