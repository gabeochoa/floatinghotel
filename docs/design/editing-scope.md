# Editing features

Reassessment, for decision. Product scope has not changed: the app is a reader and reviewer with a
narrow working-tree edit mode (`ui/source_edit.h`: a working-tree document can switch from
read-only to an editable buffer and save). Recommendation: none of the four requested features
until the scope changes; if it does, build them in the order below, because each depends on the
one before.

| feature | needs | depends on | recommendation |
|---|---|---|---|
| multi-cursor and block selection | a selection model of several ranges; today `diff_sel` holds one anchor and head | the edit buffer | first, if editing grows |
| completion | a language server (or a word index) and a popup that owns keys while open | semantic navigation's server discovery (triage, "Later: semantic navigation") | after language servers exist |
| safe rename | `textDocument/rename` from a language server, a preview of every edit across files, and an atomic multi-file write with rollback | language servers and multi-file apply | after completion |
| project-wide replace | repository search results (exists, `repo_search.h`) turned into edits, a per-match preview, and the same atomic multi-file write | multi-file apply | if editing grows; it reuses search |

Multi-file apply is the shared prerequisite: write every file through the atomic writer, keep the
originals until all writes succeed, restore them if any fails, and refresh once. It does not exist
today.

## Decision needed

Confirm editing stays at the current working-tree edit mode, which closes this triage item; or name
the first feature to build.
