# Language-server discovery and installation

Design, for decision. Find servers on `PATH` from a fixed table, start one per language and
repository on first semantic request, and never install anything without an explicit click that
shows the exact command. Semantic results are offered only for working-tree documents; every
revision, index and diff document keeps today's text navigation. Outline (`util/outline.h`),
occurrences, repository search and Markdown links never wait on a server.

## Table

One entry per server, checked in order per file extension; the first found on `PATH` wins. The
list follows px0's registry (`lspservers.go` at `5743e84`), trimmed to what the app reads today.

| server | languages | command | found here |
|---|---|---|---|
| clangd | C, C++, Objective-C | `clangd --background-index` | `/usr/bin/clangd` |
| sourcekit-lsp | Swift | `sourcekit-lsp` | `/usr/bin/sourcekit-lsp` |
| pyright | Python | `pyright-langserver --stdio` | no |
| pylsp | Python | `pylsp` | no |
| typescript-language-server | TypeScript, JavaScript | `typescript-language-server --stdio` | no |
| gopls | Go | `gopls` | no |
| rust-analyzer | Rust | `rust-analyzer` | no |

A per-repository override, `"language_servers": {".cpp": ["clangd", "--compile-commands-dir=build"]}`
in the existing per-repository settings, replaces the table entry for that extension.

## Lifecycle

1. Start lazily: the first go-to-definition, references, hover or call-hierarchy request in a
   working-tree document of that language. Opening files never starts a server.
2. Root: the repository root. clangd additionally needs `compile_commands.json`; without one the
   status says "no compile database" and requests run anyway (clangd guesses flags).
3. One process per (repository tab, server). Closing the tab shuts it down (`shutdown`, `exit`,
   then SIGTERM after 2 s, SIGKILL after 5 s, through the process-group kill `util/process.cpp`
   already uses).
4. A crash restarts at most three times per tab with 1, 4 and 16 s backoff, then stays down with
   "Language server stopped; Retry".
5. Documents are synced with `didOpen` / `didChange` (full text) / `didClose` for working-tree
   documents only. The edit buffer (`source_edit.h`) sends its unsaved text.

## Results and staleness

Each request carries the document's version and the navigation request stamp
(`navigation::stamp`). A response is used only if both still match; otherwise it is dropped, and
an in-flight request is cancelled with `$/cancelRequest` when the active document changes. A
destination is opened through `navigation::open(reading::source(path, {}, line, origin))` so Back
and Forward include it. A result pointing outside the repository opens read-only with the path
shown in full. A result whose file changed after the response arrived is marked stale in the
results list instead of being followed silently.

## Installation

The app never installs on its own. A missing server for the active language shows one line,
"C++ navigation needs clangd", with "Show install command". The command comes from the table
(Homebrew on macOS where available) and runs only when the user presses "Run"; output goes to
the command log; nothing runs with `sudo`. After it finishes, the table is rescanned. Users who
manage tools another way ignore the line.

## Out of scope

Applying edits (`workspace/applyEdit`, rename, code actions): the reader stays read-only, see
`editing-scope.md`. Diagnostics: not requested and would need a display decision.

## Decisions needed

1. Approve working-tree-only semantics (recommended) versus checking out revisions into a temporary
   worktree so historical documents get servers.
2. Approve the "Run" install button, or limit it to copying the command.
