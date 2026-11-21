# ACP agents in the sidebar

The **Agent** sidebar tab runs a coding agent over the
[Agent Client Protocol](https://agentclientprotocol.com) (ACP v1) and lets
it work in the repository the tab is showing. Floating Hotel is the ACP
*client*: the agent streams its replies, thoughts, plans and tool calls
into the transcript, asks before running tools (permission banner), and
reads/writes files **through the editor** — `fs/read_text_file` prefers a
live edit buffer and `fs/write_text_file` replaces one, so agent edits to
a file you have open for editing appear in place as unsaved changes
(Cmd+S still belongs to you). Writes to files that are not open in an edit
buffer land on disk and show up in the diff on the next refresh, ready for
the usual review flow.

## Agents

| Agent | How it is launched | Override |
| --- | --- | --- |
| Muse | `muse acp` (native ACP) | `FH_ACP_MUSE_CMD` |
| Claude | `claude-agent-acp`, else `claude-code-acp`, else `npx -y @agentclientprotocol/claude-agent-acp` | `FH_ACP_CLAUDE_CMD` |
| Codex | `codex-acp`, else `npx -y @agentclientprotocol/codex-acp` | `FH_ACP_CODEX_CMD` |

The adapters are pointed at the installed `claude` / `codex` binaries
(`CLAUDE_CODE_EXECUTABLE` / `CODEX_PATH`) when those are on `PATH` and you
have not set them yourself: the adapters' bundled binaries miss the
installed CLI's auth and network setup (on a Meta Mac their model calls
hang without this). The picker disables agents whose launcher is not on
`PATH`; hovering says how each would be launched. The selected agent persists as `acp_agent` in
settings.json. An override is whitespace-split (quotes honoured), e.g.
`FH_ACP_MUSE_CMD="muse acp --provider echo"` for a deterministic test
agent. One agent process runs per repository tab, spawned with
cwd = that repository; changing a tab's repository stops its agent.

## Using it

- Open the **Agent** tab in the sidebar (or View ▸ Agent Panel, Cmd+Shift+A),
  pick an agent, **Start**, type a prompt, Enter or **Send**. Sending with
  no agent running starts the selected one and queues the prompt.
- **File ✓** attaches the open source file to each prompt (an ACP embedded
  resource when the agent supports it), so "fix this" needs no path.
- **Send review feedback (N)** appears when the feedback basket has
  unresolved comments: it sends the same Markdown the basket exports as a
  prompt — review, click, watch the agent apply it, review the result.
- Mode/permission behaviour is the agent's: the panel cycles whatever the
  session offers (`session/set_config_option` selects like Muse's mode
  picker, or legacy `session/set_mode`). Tool-call permission requests
  block on the banner's buttons; unanswered replacements are declined.
- **New** starts a fresh conversation on the same process; **Stop** (Send
  becomes Stop while prompting) sends `session/cancel` for the turn, the
  controls-row **Stop** kills the process.

## Safety properties

- Filesystem access is confined to the tab's repository root: absolute or
  relative paths that escape it (including through a symlink that points
  out) are refused with an ACP error.
- `terminal: false` is advertised; agents use their own shell tools under
  their own permission model, gated by the permission banner.
- Nothing is staged or committed by the panel. Agent edits are ordinary
  working-tree (or unsaved-buffer) changes in the review flow.

## Implementation map

- `src/acp/acp_client.{h,cpp}` — transport and session state machine.
  Newline-delimited JSON-RPC over stdio, spawned like `src/lsp/`, but
  owner-thread pumped: the reader thread queues lines, `pump()` (called
  every frame by `ecs::AgentPollSystem`, visible panel or not) turns them
  into events, so fs/permission requests are answered from editor state.
  Non-JSON stdout lines are treated as launcher noise, stderr as logs.
- `src/ecs/agent_panel.h` — runtime glue (start/stop, prompt building,
  fs serving against edit buffers via `navigation::apply_external_edit`)
  and the sidebar rendering. `ecs::AgentComponent` is per-tab state.
- Tests: `tests/unit/test_acp_client.cpp` (framing, transcript, fs
  confinement, scripted fake agent end to end) and `tests/acp_agent.py`
  (the whole app against the fake agent and against `muse acp --provider
  echo`). E2E drivers: `acp_start` / `acp_prompt` / `acp_answer` /
  `acp_stop`, properties `acp_state`, `acp_permission`, `acp_transcript`,
  `acp_agent`.

## Not yet

- Client-side terminals (`terminal/*`), MCP server passthrough
  (`session/new` sends no servers), session load/resume lists, model and
  non-select config options, image prompt blocks. The capability fields
  are parsed so these can slot in without protocol rework.
