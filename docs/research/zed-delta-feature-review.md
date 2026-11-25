# Zed + Delta feature review (2026-11-25)

Research-only lane on `research/zed-delta-features` from `origin/acp-agents`
(`6ef619d`). No product code touched. Goal: every Zed / Delta
(dandavison/delta) mechanic that could fit FloatingHotel, classified against
what FloatingHotel already has, with only net-new fitting gaps filed in
[todo.md](../todo.md) (section "Zed/Delta follow-up (filed 2026-11-25)").

**Product/version basis**

- **Zed**: stable **1.30.2**, released Nov 25, 2026 (docs read same day).
  Sources: docs index <https://zed.dev/docs/llms.txt>,
  <https://zed.dev/features>, <https://zed.dev/releases/stable>,
  per-feature pages under <https://zed.dev/docs/> (links inline below),
  repo <https://github.com/zed-industries/zed>.
- **Delta**: **dandavison/delta** (`git-delta` pager). Resolved as the
  referent because the branch context is diff rendering/review UI and it is
  the only "Delta" with a diff-review meaning (Delta Lake/Chat/etc. do not
  fit). Sources: README
  <https://raw.githubusercontent.com/dandavison/delta/main/README.md>,
  manual <https://dandavison.github.io/delta/print.html> (embeds full
  `delta --help`; docs.rs snapshot `0.16.5`). No local `delta` binary on the
  research Mac, so the option surface is from the manual dump + README.
- **FloatingHotel**: worktree at `6ef619d` + `docs/acp-agents.md`, `todo.md`
  (2026-11-15 Zed comparison), `triage.md`.

**How to read the matrices.** Status: **have** (no work) / **partial**
(delta named) / **missing+fits** (filed as `Z1…`/`D1…` in todo.md) /
**missing+reject** (rationale here, no todo) / **decision** (needs Gabe's
product call, no todo). "Borrow the mechanic/feel, not the surface" throughout:
nothing here proposes copying Zed/Delta code, assets, or licensed themes.

**Corrections to the Nov-15 todo in light of current trees** (not duplicates):

- Side-by-side diff is **have**, not missing: `DiffViewMode::Inline` vs
  side-by-side (`diff_renderer.h:2006,2698,3635`), toggle Cmd+Shift+D
  (`menu_setup.h:285`). No todo item needed.
- Moved-code detection is **have** (own engine `util/moved_code.h` →
  `hunk.movedLines`, rendered `diff_renderer.h:1508`); what is missing is
  only distinct *styling* (filed `D6`).
- Within-line emphasis is **have** for the common case
  (`code_highlight.h:36-66` single prefix/suffix span, positional pairing);
  missing is multi-edit-per-line + unequal-pairing (filed `D5`).
- File history is **have** (`ui/file_history.h`: `--follow`, 200-commit
  paging, click opens the commit review); missing is only open-filtered-to-file
  (filed `Z5`).
- Call hierarchy is **have** (Incoming/Outgoing via LSP, `semantic.cpp:59-60`,
  `menu_setup.h:162`); Zed's re-rootable picker UI is the only delta — verify
  in app before filing, so no todo.
- Worktree *grouping/display* is **have** (`repo_picker.h:190`, `util/repo_picker.h`);
  worktree *creation/isolation for agents* is missing (filed `Z9`).
- Bookmarks are data-model-only (`util/code_bookmark.h`, one include in
  `components.h`): no picker UI found → filed `Z4`.
- Merge-conflict ours/theirs buttons confirmed missing (banner + "Mark
  resolved" + "Complete merge" only) → filed `Z6`.

## 1. Zed matrix (navigation/search/pickers/buffers/editing)

| # | Zed feature (source) | FH status |
|---|---|---|
| Z-file-finder | File finder `cmd-p` ([docs](https://zed.dev/docs/finding-navigating.md)) | have (Quick Open, todo §1) |
| Z-proj-search | Project search → editable multibuffer excerpts ([docs](https://zed.dev/docs/multibuffers.md)) | have read-only analogue (excerpt expansion, todo §1); editable excerpts **reject** (read-only client, triage.md) |
| Z-search-opts | Case/regex/word toggles; `cmd-enter` forks extra results tab | partial → filed `Z8` (fork/second tab only; toggles exist in repo search) |
| Z-symbols | Project symbols `cmd-t`, outline modal/panel ([docs](https://zed.dev/docs/outline-panel.md)) | have (Cmd+Shift+R project symbols, outline panel, breadcrumbs, sticky scopes; todo §1) |
| Z-call-hier | Call-hierarchy picker, re-rootable | partial (have incoming/outgoing; re-root UI unverified — verify in app, no todo) |
| Z-palette | Command palette, MRU tab switcher, nav history | have (todo §1–2) |
| Z-bookmarks | Project bookmarks: labeled, searchable, previewable | partial (struct only) → filed `Z4` |
| Z-permalink | Permalinks/hosting links for selection/file | missing+fits → filed `Z7` |
| Z-multibuf | Multibuffer: editable excerpts across files | **reject** as surface (read-only), borrow feel only (pinned review-excerpt set = decision, §4) |
| Z-panes/dock | Panes/splits, dock, multi-project window, multi-root | partial: multi-repo tabs have; splits/dock **reject** (single-pane reader by design) |
| Z-worktree | Worktree picker/create/isolate, per-thread isolation ([docs](https://zed.dev/docs/git.md)) | partial (grouping have; create/isolate missing) → filed `Z9` |
| Z-folds | Syntax folds + fold-at-indent (1.30.2) | have (file/hunk/source folds + defaults; todo §3-adjacent) |
| Z-crumb/sticky | Breadcrumbs + sticky scroll | have (sticky scopes, breadcrumbs; todo §1) |
| Z-multicursor | Multi-cursor editing, snippets, inlay hints, Vim/Helix | **reject** (editing is working-tree/ACP-owned; single-caret only) |
| Z-diag | Scrollbar diagnostics, inline error lens, diagnostics multibuffer ([docs](https://zed.dev/docs/diagnostics.md)) | partial/unknown: LSP hover tooltip + excerpt view have; severity scrollbar + live diagnostics multibuffer unverified → verify in app, no todo |
| Z-tasks/debug | Tasks JSON, integrated terminal, DAP debugger, REPL | partial: ACP client terminals have (`docs/acp-agents.md`); JSON task runner + debugger **reject** (not a dev environment) |

## 2. Zed matrix (Git/diff/review/blame)

| # | Zed feature (source) | FH status |
|---|---|---|
| Z-proj-diff | Project diff as multibuffer, per-hunk stage, word-diff toggle, split/unified ([docs](https://zed.dev/docs/git.md)) | have (all-changes review, side-by-side, intraline); per-hunk stage exists via toolbar; word-diff *toggle* missing → folded into `D5`/`D6` styling, no separate todo |
| Z-agent-review | Review-changes pane: change summary + per-hunk accept/reject + inline single-file review ([docs](https://zed.dev/docs/ai/agent-panel.md)) | partial (feedback basket + "Send review feedback (N)" have) → filed `Z1` (per-turn summary + per-hunk disposition) |
| Z-file-hist | File history → per-commit filtered diff | partial (history have; filtered-diff missing) → filed `Z5` |
| Z-blame | Blame + inline current-line blame | partial (gutter have, todo §1) → filed `Z3` (inline line blame + clickable commit) |
| Z-hunk-keys | Hunk expand/collapse/navigate keys, escape-to-overview | have (`j/n/k` nav, hunk context, fold toggles; Escape-first per triage) |
| Z-conflict | Conflict blocks with Use-ours/theirs/Both | partial (banner + explicit resolve have, todo §3) → filed `Z6` |
| Z-stash | Stash picker with diff view, apply/pop/drop | have (stash as reviewable document, todo §3) |
| Z-commit-ai | AI commit message + per-feature model picker + instructions (1.30.2) | missing+fits via ACP → filed `Z10` |
| Z-branches/tags | Branch/switch/delete pickers, tags, hosting-link commits | have (todo §3); hosting-link commits covered by `Z7` |
| Z-git-graph | Git Graph | **decision** (§4; commit rows exist, graph is a new surface) |

## 3. Zed matrix (AI/agent panel/ACP/permissions — compounds with `docs/acp-agents.md`)

| # | Zed feature (source) | FH status |
|---|---|---|
| Z-threads | Threads sidebar: grouped/switchable/archivable/cross-project, `ctrl-tab` switcher ([docs](https://zed.dev/docs/ai/parallel-agents.md)) | partial (Resume list have) → filed `Z2` (in-panel conversation switcher + usage readout) |
| Z-context | `@`-mentions (files/symbols/diagnostics/branch diffs), pasted selections as mentions, image attach | partial (File✓ + image have) → filed `Z11` |
| Z-perms | Tool-permission allow/confirm/deny + per-tool regex ([docs](https://zed.dev/docs/ai/tool-permissions.md)) | partial (banner allow/deny have) → filed `Z12` |
| Z-steer | Queued messages + Steer, stop/send-now, editable+resubmittable messages, token/compaction readout | partial (Stop/cancel + New have; steer/usage readout missing) → covered by `Z2`, no separate todo |
| Z-slash | Slash commands (`/compact`, agent-native, `/skill`), MCP + Skills + AGENTS.md instructions | partial (MCP editor + settings have; slash/skill invocation missing) → decision (§4; agent-native surface, not ours to invent) |
| Z-inline | Inline assistant / edit prediction (Zeta/Copilot) | **reject** (prediction/completion engine in a review client; agent edits flow through ACP + working tree) |
| Z-sandbox | Sandboxing, profiles, terminal threads | partial (repo-root confinement have; OS sandbox profiles = agent's own, **reject** duplicating) |
| Z-collab | Channels, shared editing, follow mode, notes | **reject** (no multi-user surface; review-room model noted for future) |

## 4. Needs-a-product-decision (no todos filed)

- **D-A — pinned review-excerpt set ("read-only multibuffer")**: collect hunks across files into one sheet for a review pass. Zed's multibuffer is the feel; editing stays out. Worth a mock before any todo.
- **D-B — Git Graph surface**: commit list + graph rendering vs current rows. New surface; needs a mock + perf check on large repos.
- **D-C — slash-command/skill invocation in the Agent panel**: ACP-native affordance; depends on which agents Gabe actually runs. Defer until usage evidence.
- **D-D — named review presets** (Delta `features=` analogue): one-click reviewer toggles (sbs/line-numbers/whitespace/moved-styling) over `presets.h`. Cheap, but preset sprawl needs Gabe's taste call.

## 5. Delta matrix (diff rendering — primary fit area)

| # | Delta feature (manual sections) | FH status |
|---|---|---|
| D-sbs | Side-by-side variants + narrow fallback, per-panel number formats | have (DiffViewMode + Cmd+Shift+D); narrow-fallback behavior unverified, no todo |
| D-linenos | Two-column old/new numbers, formattable minus/zero/plus styles | have (`code_gutter.h` prefix(old,new,sign), View ▸ Line Numbers) |
| D-headers | Stylable file/hunk headers, `file-header=stat` | partial (sticky headers + hunk context have) → stat-header density folded into `D1`, no separate todo |
| D-conflict | `zdiff3` two-diff conflict rendering | covered by `Z6` |
| D-emph | Within-line Levenshtein multi-edit emphasis + non-emphasis dimming | partial (single-span have) → filed `D5` |
| D-moved | `--color-moved` + `map-styles` zebra remap | partial (detection have, styling missing) → filed `D6` |
| D-ws | `whitespace-error-style`, no-newline chip | partial (visible-whitespace toggle have) → filed `D3` |
| D-copy | Copy strips `-/+` by default; `keep-plus-minus-markers` toggle | partial (code-bytes clipboard have) → filed `D4` |
| D-blame-view | Blame pager: palette, formats, commit links | partial (gutter have) → covered by `Z3`, no separate todo |
| D-grep-view | Grep decoration: match counts, function context, clickable numbers | partial (panel + highlight have) → filed `D2` |
| D-nav | `n/N` file jumps, same-level `alt+j/k`, fold-all `tab`, context `U/+/-/W`, pathspec `&`, `:` command edit | partial (`j/n/k` hunk nav have) → filed `D1` (file-stop + same-level + fold-all keys) |
| D-hyper | OSC-8 hyperlinks (commits→host, paths→editor) | **reject** as protocol (GUI app navigates in-app); host-open half covered by `Z7` |
| D-config | gitconfig `[delta]` + feature presets + `DELTA_FEATURES` | **reject** as system (app has settings.json + `presets.h`); one-click preset idea = D-D decision |
| D-pager | less chain, `--paging`, width handling, `max-input-bytes` cap | n/a by design (native windowing via `FilePageRequest`); large-input cap already a perf-harness concern |
| D-raw | `--color-only`, `--raw`, `--parse-ansi` interop | **reject** (FH is the viewer, not a pipe filter) |

## 6. Explicit near-miss / reject list (considered, not filed)

Zed: editable multibuffers, panes/splits/dock, multi-cursor/snippets/inlay/Vim-modes,
edit prediction + inline assistant, JSON task runner, debugger/REPL, collab
channels/shared editing, sandbox profiles, settings deep-links/keymap editor.
Delta: OSC-8 hyperlink protocol, gitconfig feature-stacking, pager/less chain,
`--color-only`/`--raw` interop modes. Prior-todo dupes avoided: picker threading,
fuzzy ranking, live preview, symbols, excerpt search, palette/history, blame
gutter, minimap, repo picker/MRU, stash-as-document, conflict banner, ACP
follow-ups (§ACP in todo.md).

## 7. Recommendation rationale (prioritised by review-first leverage × cost)

1. **Agent-review loop (`Z1`, `Z2`, `Z11`, `Z12`, `Z10`)** — highest leverage:
   every item compounds with the ACP sidebar Gabe just built, turning "agent
   edits → working tree → re-review" into a tight loop. `Z12` (permission
   regexes) is the cheapest trust win; `Z1` (per-hunk disposition) is the
   biggest review-model win.
2. **Diff legibility (`D5`, `D6`, `D3`, `D4`)** — agent diffs are large rewrites;
   multi-edit emphasis + moved styling + whitespace errors directly cut
   misread risk. All small, renderer-local.
3. **Review navigation (`D1`, `Z4`, `Z5`, `Z8`)** — keyboard file-stop jumps and
   bookmarks pay off on every large review; all additive keybindings/views.
4. **Sharing + isolation (`Z7`, `Z9`, `Z6`, `Z3`)** — permalinks unblock async
   review sharing; worktree isolation unblocks parallel agents; conflict
   buttons + blame links are small completions of shipped surfaces.
