# Paseo feature review — for FloatingHotel

Researched 2026-11-25 (UTC). "Add them all" here means: every Paseo feature
FloatingHotel lacks becomes a todo item (fit ones actionable, low-fit ones
captured with reasons). Nothing is implemented in this lane.

## Source map

- Website/docs: https://paseo.sh/ + ~80 pages under `/docs/*`, `/plugins/category/*`,
  `/alternatives/*`, `/hub`, `/changelog`, `/download` (fetched 2026-11-25).
- Repo: https://github.com/getpaseo/paseo, shallow clone HEAD `e114e13`
  (commit date 2026-11-26 — recorded as-is from the clone; likely TZ-skewed),
  release version **0.17.4** (package.json + CHANGELOG top entry 2026-11-23).
  Full repo inventory (packages/providers/plugins/mechanics):
  [paseo-repo-inventory.md](paseo-repo-inventory.md).
- Stores: App Store id6758887924 "Paseo – Remote Coding Agents" v0.17.4
  (2026-11-24); Play Store `sh.paseo` ("Manage Claude Code and Codex agents
  from your phone"). Desktop: macOS 13+/Win/Linux(AppImage/DEB/RPM),
  Homebrew cask, npm `@getpaseo/cli`, Nix. Server: Docker `ghcr.io/getpaseo/paseo`.
- Positioning (lead only, verified below): open-source (Apache 2.0) app to run
  Claude Code, Codex, Copilot, OpenCode, Pi, Muse and 30+ ACP agents in
  parallel on your machines, with editor/terminals/diffs/PRs/browser in one
  desktop/mobile window. Maintainer: Mohamed Boudra Ziani (solo, sponsor-funded,
  no telemetry, no account required for local use).

Docs nav used as checklist (all opened): agent-profiles, browser(+tools/when),
cli, community, configuration, connectivity, custom-providers, docker, hub(+11
subpages), mcp, metadata-generation, orchestration(+workflows), parallel-development,
plugins(+hooks/migration/providers/publishing/reference/sidebar-views),
providers(+supported-providers incl. muse-code), schedules(+chat/cli), sdk(+8),
security, skills, troubleshooting, updates, voice, web-ui, why, workspaces,
worktrees. Alternatives/comparison pages used only as secondary hints.

## Coverage counts (reconciled)

64 distinct Paseo features inventoried in the matrix: **5 already-have** ·
**1 already tracked** in todo.md ACP follow-ups · **21 partial** (delta → todo) ·
**11 missing-fit** (→ todo) · **14 low-fit** and **9 platform** (captured, not
built) · **3 unknown-needs-decision**. Todos added: **21** grouped items
(T1–T21 in todo.md); several feature rows intentionally share one
implementation delta. The 23 low-fit/platform rows are grouped into L1–L17.
U1–U3 are the matrix's unknown rows; U4 is an extra renderer audit question,
not counted as a feature row. 5+1+21+11+14+9+3 = 64. ✓

## What Paseo is (verified mechanics, deep notes)

**Architecture.** Client–server like Docker: a per-machine daemon owns agent
lifecycles; clients (desktop/Electron bundling the daemon, native mobile,
bundled/self-hosted web UI, CLI, TS SDK) talk to it over WebSocket/API.
Desktop is one-click (daemon bundled); headless daemon + any client also works.
Paseo wraps installed agent CLIs as subprocesses — never modifies them, never
touches their auth (each provider keeps its own credentials/subscriptions).

**Highest-leverage mechanics for us:**

1. **Workspace = stable container, sessions = tabs inside it.** A workspace has
   a directory; agents, terminals, browsers, diffs live in it as tabs. Our repo
   tabs are the cousin (one agent per tab), but we have no multi-session,
   no background agents, no subagent parentage/detach.
2. **Orchestration patterns that work today:** agent-spawns-agent via MCP tools
   (`create_agent`, `send_agent_prompt` w/ queue/steer/interrupt) or via CLI
   (`paseo run/wait/logs`) with caller recognition (`PASEO_AGENT_ID`); agent
   profiles with When-to-use notes guide delegation; skills (`/paseo-handoff`
   briefing transfer, `/paseo-committee` two independent analyses,
   `/paseo-advisor` second opinion, implement→review with different providers).
3. **Worktree lifecycle done right:** branch-off / checkout-branch / checkout-PR
   (PR default = check out, branch default = branch off), slug directories under
   hashed source path, `paseo.json` setup/teardown + named scripts/services
   with dynamic ports (`$PASEO_PORT`, reverse-proxy hostnames, service-to-service
   env), `.worktreeinclude` for ignored files (`.env`), auto terminals,
   adoption of existing (even git-made) worktrees, archive-undo semantics.
4. **Review-adjacent automation:** metadata LLM (titles, branch slugs, commit
   messages, PR title/body; Automatic cheap-model cascade vs Manual list;
   per-project wording in `paseo.json`); Changes pane with Push-for-unpushed +
   Change-base picker; PR/MR watches (checks/reviews/comments) with
   auto-archive-on-merge; Open-in-Paseo deep links from Hub replies.
5. **Trust model worth copying:** Paseo-tools injection OFF by default with
   per-provider `disabledTools`; browser tools double-gated (host switch +
   injection) and workspace-scoped; relay E2EE (Noise NK, QR pairing link as
   trust anchor, untrusted-relay design); `hub.execute` opt-in per machine;
   trigger sender allowlists that explicitly do NOT claim prompt-injection safety.

## Feature matrix

Class key: HAVE = parity (no todo) · TRACKED = already in todo.md · PARTIAL =
have core, delta → todo · MISSING-FIT = actionable todo · LOW-FIT =
capture-list only · PLATFORM = not transferable as product code · UNKNOWN.

### A. Providers & lifecycle

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| A1 | 10 native integrations (Claude, Codex, OpenCode, Pi, OMP, Copilot, fx, Hoplite, Antigravity, Muse) | /docs/supported-providers, repo `providers/`, 6 provider plugins | PARTIAL | `docs/acp-agents.md` (3 ACP launchers) → T1 |
| A2 | ~38-agent ACP catalog + generic `extends:"acp"` custom add | supported-providers "More agents through ACP", Settings→Hosts→Providers | PARTIAL | hardcoded launchers → T1 |
| A3 | Custom provider profiles (alternate endpoints e.g. Z.AI/Qwen, custom binary, multi-account, disable) | /docs/custom-providers | PARTIAL | `FH_ACP_*_CMD` overrides only → T1 |
| A4 | Agent profiles: saved provider/model/mode/thinking + When-to-use delegation notes | /docs/agent-profiles | MISSING-FIT | none → T13 |
| A5 | Named execution/permission modes (Default/Ask/Strict/Full-access/bypass/plan) + provider-options passthrough | muse-code page, CLI `agent mode`, Hub provider-native options | PARTIAL | generic `set_config_option` cycling → T3 |
| A6 | Provider diagnostics (binary, PATH, version) + in-app install/update + 30-min model refresh | troubleshooting, 0.17.1/0.17.2 changelog | PARTIAL | hover-text disable only → T4 |
| A7 | Session resume + cross-session history search + native-session import (Pi JSONL, OMP, OpenCode) | repo `agent-history-search.ts`, changelogs | PARTIAL | `session/list+load` only → T5 |
| A8 | Rewind latest turn; collapse turns; copy reply as plaintext | 0.17.1 changelog, fix refs | PARTIAL | New/Stop only → T10 |
| A9 | Turn/session stats (time, tokens, cache, cost) + Usage panel w/ quota forecasts | 0.17.2 changelog, 10 usage-source plugins | LOW-FIT | no accounts/quotas concept; capture list |
| A10 | Image prompt attachments | App Store listing, CLI `send --image` | HAVE | `docs/acp-agents.md` Image row |
| A11 | @-file tagging + /commands + /skills in composer | homepage mock, skills docs | PARTIAL | File ✓ attach only → T2 |
| A12 | Audio prompt attachments | FH todo ACP follow-ups | TRACKED | todo.md:849-863, no new item |
| A13 | Voice: local-first dictation + voice mode (ONNX STT/TTS) + voice-driven agents | /docs/voice, `expo-two-way-audio` | LOW-FIT | desktop review client; capture list |
| A14 | Structured JSON output schema (`--output-schema`) | CLI ref | LOW-FIT | capture list |

### B. Parallel work & orchestration

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| B1 | Multiple sessions per workspace incl. review-only second agent | /docs/parallel-development, workflows | MISSING-FIT | one process per repo tab → T14 |
| B2 | Agent-spawns-agent (MCP + CLI, cross-provider/host, queue/steer/interrupt) | /docs/orchestration, MCP ref, CLI multi-agent | MISSING-FIT | none → T14 |
| B3 | Background agents (hidden, finish-notify) + detach-to-top-level | MCP `background`, CLI, 0.17.x fixes | MISSING-FIT | none → T14/T15 |
| B4 | Delegation skills: handoff briefing, committee, advisor, implement→review | /docs/skills, `skills/` | MISSING-FIT | feedback-basket→agent is manual → T20 |
| B5 | Workspace labels/pins/archive-undo; run-workspace retention switch | MCP `archiveOnFinish`, changelog | PARTIAL | pinned repos only → T7 |
| B6 | Metadata auto-gen (titles, branch slugs, commit msgs, PR text; auto/manual; per-project wording) | /docs/metadata-generation | MISSING-FIT | none → T18 |

### C. Projects / worktrees

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| C1 | Project registry (dirs, GitHub incl. Clone-from-GitHub) grouping workspaces | /docs/workspaces, CLI `project`, changelog | PARTIAL | tabs+pinned+recent, no registry/clone → T8 |
| C2 | Worktree workspaces: branch-off/checkout-branch/checkout-PR, slugs, setup/teardown, `.worktreeinclude`, adopt-existing, auto-terminals | /docs/worktrees, repo `paseo-worktree-service.ts` | MISSING-FIT | read-only worktree browse → T16 |
| C3 | Repo-local config (`paseo.json`: scripts/services/terminals/metadata wording) | /docs/worktrees | MISSING-FIT | none → T17 |
| C4 | Supervised services w/ dynamic ports + reverse proxy + service-to-service env | /docs/worktrees | LOW-FIT | beyond review scope; capture list |

### D. Editor / terminals / browser / layout

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| D1 | File browse+edit w/ agent edits landing in place | providers page, parallel-development | HAVE | `src/ui/source_edit.h`, acp `fs/` through edit buffers |
| D2 | Managed interactive terminals (UI + agent-driven, capture/send-keys) | CLI `terminal`, MCP terminal tools | PARTIAL | agent-only `terminal/*`, no terminal UI → T6 |
| D3 | In-app browser + agent automation (a11y tree, screenshot, console/net logs, trusted input, logged-in sessions) | /docs/browser* | LOW-FIT | heavy; capture list |
| D4 | Split panes (agent beside code) + content-width presets | parallel-development, 0.17.1 changelog | PARTIAL | fixed sidebar+main → T11 |
| D5 | Command Center incl. agent settings before first message | 0.17.1 changelog | PARTIAL | palette has no agent settings → fold T2 |
| D6 | Syntax grammars (shell, Dockerfile, Terraform/HCL) | changelogs, `highlight` pkg | UNKNOWN | check `src/ui/code_highlight.h` coverage → U3 |

### E. Diffs / review / PR / forge

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| E1 | Diff review (unified/split, selection, folding) + push/pull/fetch sync | homepage mock, CLI/Changes fixes | HAVE | diff renderer + sync pill (todo §3) |
| E2 | Commits from Changes; generated commit messages | worktrees/services, metadata-gen | PARTIAL | commit box exists; gen-msg → T18 |
| E3 | Open-PR flow (title/body drafted from diff) | metadata-gen pullRequest | MISSING-FIT | no PR flow → T19 |
| E4 | PR/MR watch (checks/reviews/comments) + auto-archive-on-merge | 0.17.1, repo `change-request-*` | LOW-FIT | needs forge layer; capture list |
| E5 | Forge integrations (GitHub/GitLab/Gitea/Forgejo/Codeberg) + SSH-alias/fork-aware issue links | repo `forge-registry.ts`, changelog | LOW-FIT | no network forge layer; capture list |
| E6 | File Download button for preview-less files | changelog | UNKNOWN | check preview coverage → U1 |
| E7 | Structured question cards (options, Next/Submit gate) | changelog | UNKNOWN | vs permission banner → U2 |

### F. Permissions & safety

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| F1 | Tool permission banner (approve/deny, blocking) | orchestration, MCP ref | HAVE | `docs/acp-agents.md` permission banner |
| F2 | Provider-native modes/options (sandbox, approval_policy, disallowedTools) + per-profile tool policy | muse-code page, MCP `paseoTools`, Hub options | PARTIAL | generic passthrough → T3 |
| F3 | Headless approvals (`permit ls/allow/deny`) | CLI ref | LOW-FIT | no CLI; capture w/ T-side note |

### G. Automations (schedules / heartbeats / Hub)

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| G1 | Cron schedules: new agent/run, pause/resume/run-once/update, chat/CLI creation | /docs/schedules* | LOW-FIT | no daemon/background; capture list |
| G2 | Heartbeats: recurring prompt into same agent, expiry, minimal API | schedules overview | LOW-FIT | capture list |
| G3 | Hub: GitHub/Linear/Slack/Discord/schedule/webhook triggers → agents on team daemons, allowlists, activity, shared history, deep links | /docs/hub*, hub.txt, pricing Free/$15 | PLATFORM | team SaaS; capture list |

### H. Clients & distribution

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| H1 | Native mobile (iOS/Android, full parity, push on finish, sheets, Duo fold) | App Store/Play listings, changelog | PLATFORM | macOS-only; capture list |
| H2 | Desktop finish/push notifications; dock-badge discipline | `push-notifications/`, changelog | MISSING-FIT | toasts only → T15 |
| H3 | Web UI (bundled/self-hosted/hosted, same-origin auto-connect, proxy/tunnel guides) | /docs/web-ui, docker | PLATFORM | no daemon; capture list |
| H4 | Staged stable rollout + beta channel (TestFlight/APK/CLI) + in-app provider updates | /docs/updates, download | PLATFORM | source-built; capture list |
| H5 | i18n (EN/zh-CN/ja/ko+) | README.*.md, translations example | LOW-FIT | capture list |

### I. Machines / connectivity / self-host / security

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| I1 | Multi-host: saved connections (TCP/socket/SSH/relay-QR), SSH tunneling, Tailscale guide | /docs/connectivity, CLI `host` | PLATFORM | single-machine app; capture list |
| I2 | E2EE relay (Noise NK, pairing link = trust anchor, open-source relay, untrusted-relay design) | /docs/security | PLATFORM | capture list |
| I3 | Docker image/compose + reverse-proxy (Caddy/Nginx) + non-root user | /docs/docker | PLATFORM | capture list |
| I4 | Password auth (bcrypt), host allowlist (DNS-rebinding), socket isolation, `hub.execute` opt-in, scoped mounts | /docs/security | PLATFORM | posture note only; capture list |

### J. Plugins / themes / SDK / API

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| J1 | Plugin system (TS client+server, 652 gallery, install/update/logs, hooks incl. worktree placement, sandbox launcher) | /docs/plugins*, homepage | LOW-FIT | huge surface; capture list |
| J2 | Custom sidebar views (replace sidebar, per-device setting, ref queries) | sidebar-views doc | LOW-FIT | capture w/ J1 |
| J3 | Theme gallery (121) + picker grid; beyond dark/light | plugin cats, changelog | PARTIAL | `src/ui/theme.h` dark/light only → T12 |
| J4 | MCP server: full Paseo-tool catalog for external agents (agents, workspaces, terminals, schedules, permissions, browser, speak) | MCP ref | MISSING-FIT | client-only MCP → T21 |
| J5 | User CLI (`run/ls/attach/send/logs/wait/usage/...`) + JSON/YAML output | CLI ref | LOW-FIT | e2e drivers cover automation; capture list |
| J6 | TS SDK + event streaming; Hub public API; trigger-YAML-as-code | /docs/sdk*, hub api | PLATFORM | no API surface; capture list |

### K. Onboarding / settings / performance

| # | Paseo feature | Evidence | Class | FH pointer / delta → |
|---|---|---|---|---|
| K1 | Guided first-run (loading → setup, provider status/install links) | changelog 0.16.x, providers settings | PARTIAL | welcome picker only → T9 |
| K2 | Rich settings UI w/ per-section docs links (vs raw JSON) | changelog, MCP editor note | PARTIAL | raw-JSON MCP edit (already todo) → note in T4 |
| K3 | Login-shell PATH resolution + diagnostic logs (agents find user tools) | troubleshooting | PARTIAL | same failure class (bundled-adapter PATH miss) → T4 |
| K4 | Send-queue while turn runs (queue default agent→agent, interrupt default human→agent) | workflows, MCP `activeTurnBehavior` | PARTIAL | Send/Stop only → T10 |
| K5 | Long-session scale: paginated history import, reconnect state, lazy loads | changelogs | PARTIAL | session restore exists → T10 |
| K6 | Local-first + agent-auth passthrough + no telemetry | FAQ, privacy (2026-08-29), security | HAVE | same posture (`CLAUDE_CODE_EXECUTABLE`/`CODEX_PATH` note) |

### Unknowns needing a decision (U1–U3, plus the U4 renderer audit)

- U1: preview-less file Download button — check preview coverage; trivial if missing.
- U2: structured question cards — is the ACP permission banner enough, or do we
  want option-prompt UI? Product call.
- U3: syntax grammar gaps (shell/Dockerfile/Terraform/HCL) — audit
  `src/ui/code_highlight.h` + token cache; small if missing.
- U4: transcript markdown extras (Mermaid, copy-as-plaintext) — audit renderer;
  small if missing.

## Shipped vs beta vs planned (verified)

Shipped record = CHANGELOG.md + version-floored plugin manifests
(`requirements.paseo`, e.g. antigravity ≥0.16.2). Channels: Stable (default,
36 h staged rollout) vs Beta (TestFlight/APK/desktop toggle/CLI `@beta`).
OMP ships **disabled by default** (opt-in). No ROADMAP file or planned markers
found in repo (inventory §5); the only future-dated marker is a forge-shim
cleanup target 2027-01-17. ACP catalog membership is version-pinned in-app and
can change without a repo change — treat counts as dated, not fixed.
Above "35+ more" homepage claim verified: 10 native + ~38 catalog ACP entries.
