# floatinghotel — 100 Questions to Define Purpose & Feature Set

Answer inline (put your answer after each `>` or just reply in chat with the
numbers). Skip any that don't matter. Terse is fine — even one word.

---

## A. Vision & Purpose (why this exists)
1. In one sentence, what is floatinghotel *for* — what makes you open it?
2. Personal tool for you only, or something others might use one day?
3. What git tool(s) do you use today (CLI, lazygit, magit, Fork, GitKraken, VS Code, gitui, Tower)?
4. What specifically annoys you about those that this should fix?
5. If it could do only ONE thing excellently, what is it?
6. What *feeling* do you want using it — calm/minimal, dense/powerful, playful, pro?
7. Is instant launch/speed a core value, or is richness more important?
8. Should it replace the git CLI for you, or complement it?
9. What does "this is a success" look like concretely?
10. Does the "floating hotel" name/metaphor carry any intent for the design?

## B. Who & When (user + context of use)
11. Solo repos, team repos, or large monorepos?
12. Typical repo scale you'll point it at (files / commits / branches)?
13. Do you juggle many repos at once, or mostly one at a time?
14. How often do you commit — many tiny commits or fewer big ones?
15. How heavily do you branch / rebase / stash / cherry-pick?
16. Do you review others' changes in it, or only manage your own work?
17. Keyboard-primary or mouse-primary?
18. Always-open window, or summoned when needed?
19. Single or multi-monitor; big window or compact?
20. macOS-only forever, or do Linux / Windows / web actually matter?

## C. Core Workflows (the daily loop)
21. Walk me through your #1 workflow start-to-finish (e.g. "see changes → stage → commit → push").
22. What's the 2nd and 3rd most common workflow?
23. Which workflow is currently *painful* anywhere (CLI or GUI)?
24. Do you stage whole files, hunks, or individual lines?
25. Do you write commit messages inline here, or in an editor?
26. Do you amend / reword / squash often?
27. How do you resolve merge conflicts today — should this help?
28. Do you use interactive rebase? Should the app expose it visually?
29. Do you use `git stash` as a workflow, or rarely?
30. Do you ever need the raw git CLI escape hatch from inside the app?

## D. Status / Changes View
31. Flat file list, tree by directory, or full repo tree — default?
32. Should untracked/ignored files show by default?
33. Do you want inline stage/unstage toggles per file (checkboxes) or keyboard?
34. Group by staged/unstaged/untracked (current), or unified with badges?
35. Show file counts / +/- line stats per file?
36. Do you want a "discard changes" action, and how guarded should it be?
37. Should selecting a file always open its diff, or require Enter?
38. Do you need per-file context menus (stage, discard, open, reveal, history)?
39. How important is showing rename/copy detection (R/C status)?
40. Do you want to see the diff of the *staged* version vs working separately?

## E. Diff Viewer
41. Inline vs side-by-side — which is your default, and do you switch often?
42. Syntax highlighting for code diffs — must-have or nice-to-have?
43. Word-level (intra-line) diff highlighting — wanted?
44. Whitespace toggle (ignore whitespace changes)?
45. Wrap long lines or horizontal scroll?
46. Stage/discard individual hunks or lines from the diff pane?
47. Image diffs (before/after) — needed?
48. Large-file / binary handling — collapse, warn, skip?
49. Show file header (path, +/-, mode changes, rename arrows)?
50. Jump between hunks with keyboard?

## F. History / Commit Log / Graph
51. Linear list (current) or full branch graph with lanes?
52. How many commits to load — recent N, infinite scroll, all?
53. What matters most per commit row: subject, author, date, hash, refs?
54. Show relative time ("2h ago") or absolute dates?
55. Branch/tag badges on rows — how prominent? (they currently overflow)
56. Do you want to search/filter commits (by message, author, file, path)?
57. Commit detail pane: full message, diff, changed-file list, parents?
58. Do you need to compare arbitrary commits (A vs B)?
59. Do you want blame / "who changed this line" anywhere?
60. Reflog access — ever needed?

## G. Branches / Refs / Remotes
61. Refs panel: what do you need — local branches, remotes, tags, stashes?
62. Create / rename / delete / checkout branches from the UI?
63. Show ahead/behind counts vs upstream?
64. Merge / rebase branch onto branch via UI, or CLI only?
65. Push/pull/fetch: one-click, or with options (force, tags, set-upstream)?
66. Multiple remotes — do you use them?
67. Do you want a visual "sync" state (up to date / diverged)?
68. Worktrees — do you use them?
69. Submodules — do you need them surfaced? (this repo has one)
70. Tag creation / management — needed?

## H. UI / Visual Design
71. Keep the current dark theme, or want light + theming?
72. Density preference — comfortable (current) or compact/dense?
73. Font: current mono for diffs + proportional for chrome — right call?
74. How much chrome do you want — menus, toolbar, status bar all stay?
75. Do you want the big empty diff pane to show something useful when idle (recent activity, repo summary)?
76. Rounded/soft (Bear) vs sharp/flat vs something else for cards/rows?
77. Color-coding: keep A=green/M=yellow/D=red/U=gray badge scheme?
78. Animations — subtle only, or richer transitions?
79. Icons — none (current, mostly text), or add a tasteful icon set?
80. Accessibility: is the WCAG contrast / min-font enforcement worth keeping strict?

## I. Interaction Model
81. Keyboard-first (vim-style j/k, hjkl), command palette, or mouse menus?
82. Do you want a command palette (⌘K) for all actions?
83. Global shortcuts to summon/hide the window?
84. Right-click context menus everywhere, or minimal?
85. Drag-and-drop (reorder, stage by dragging) — wanted or gimmick?
86. Multi-select files/commits for batch actions?
87. Undo for destructive actions (discard, delete branch)?
88. Confirmations: how aggressive vs get-out-of-my-way?
89. Toasts/notifications for git op results — keep, or quieter?
90. Should it watch the filesystem and auto-refresh (current), or manual refresh?

## J. Scope, Non-Goals & Technical
91. What is explicitly OUT of scope (things you never want it to do)?
92. Is this a "viewer/committer" or a full git client (rebase, conflict resolution, everything)?
93. Any feature you've seen elsewhere you definitely want stolen?
94. Any feature you've seen elsewhere you definitely want to avoid?
95. Performance bar: what repo size must stay smooth (commits/files)?
96. Offline-only, or any network features (GitHub PRs, CI status)?
97. Config/settings: minimal, or lots of knobs?
98. Distribution: just for you, .app bundle, or eventual release?
99. How much do you want to invest here vs it being a playground for afterhours?
100. If we ship ONE improvement this week, what should it be?
