# Paseo repo inventory (concepts/mechanics only, no code or assets copied)

- HEAD: e114e13de2bcefcc88d0fea816f03aac71c8834a, dated 2026-11-26 ("Let plugins run agents, terminals, and worktree commands behind a sandbox launcher (#10786)")
- Release version: package.json 0.17.4; CHANGELOG.md top entry 0.17.4 - 2026-11-23; no git tags in this clone (`git describe` fails)
- Repo tagline (package.json): "voice-controlled development environment for local AI coding agents"; homepage https://paseo.sh; Apache-2.0

## 1. Top-level structure (one line each)

packages/* (npm workspaces; each a package under packages/<name>):
- app — Expo/React-Native mobile+web client (has eas.json, Gemfile/fastlane bits; iOS+Android+iPad/tablet layouts)
- server — backend daemon (agent lifecycle, providers, worktrees, terminals, schedules, relay, MCP)
- client — client SDK package
- cli — command-line client for controlling agents/daemons remotely
- protocol — shared protocol schemas / wire types (forge-manifest, check-traits)
- relay — relay bridging daemon↔client connections (cloudflare-adapter, e2ee, cutover-proxy)
- desktop — Electron wrapper desktop app
- plugin — plugin SDK package
- website — marketing/docs site bundle
- expo-two-way-audio — native module for two-way audio streaming (voice)
- highlight — syntax-highlighting package (Terraform/HCL added 0.16.0)
- react-native-enriched-markdown — Rich-text markdown renderer for RN

plugins/* (each dir has paseo-plugin.json with id + paseo version floor):
- antigravity-provider (>=0.16.2), fx-provider, goose-provider (>=0.17.0), hermes-provider (>=0.17.3), hoplite-provider, muse-provider — agent-provider plugins (server adapter in index.server.ts + server/)
- claude-usage-source, codex-usage-source, copilot-usage-source, cursor-usage-source, grok-usage-source, kimi-usage-source, minimax-usage-source, opencode-go-usage-source, openrouter-host-usage-source, zai-usage-source — sidebar Usage-card cost/quota data sources
- worktree-include (>=0.14.0-beta.1) — worktree scan scoping helper

docs/* (contributor/internal docs): providers.md (adapter-authoring rules), forge-providers.md (3-registration forge recipe), custom-providers.md, architecture/product/data-model/permissions/orchestration-adjacent notes, mobile/terminal/markdown/renderer notes.

public-docs/* (user docs site): providers, supported-providers/<agent>.md pages, orchestration.md, orchestration-workflows.md, parallel-development.md, schedules-*, mcp.md, cli.md, hub/, plugins/.

skills/* (agent skill definitions): paseo (MCP/CLI reference: hosts, projects, workspaces, scripts, agents, schedules, heartbeats), paseo-advisor, paseo-committee, paseo-handoff, paseo-help, paseo-plugin.

plugin-examples/* (sample plugins): agent-configuration, agent-tools, buttons, catppuccin (theme), composer-controls, diff-watch, hosts, inline-thinking, lifecycle-actions, lifecycle-logger, linear, local-plugin, markdown-panel, media-stream, modal-ui, new-workspace-issues, pdf-panel, provider-acp-transformer, provider-direct, settings, sidebar-views, timeline-items, translations, workspace-row-badges, worktree-placement, worktrunk.

Other roots: fastlane/ (mobile store metadata — android dir present), docker/, scripts/, nix/flake, patches/, assets/, cli-client-id, public-docs/.

## 2. Agent/provider support

Built-in providers (docs/providers.md "current built-ins", allProviders array): claude, codex, copilot, opencode, pi, omp — 6.
- claude — direct provider, adapter at packages/server/src/server/agent/providers/claude/agent.ts; first-party model manifest model-manifest.ts; Bedrock/Vertex permission-mode caveats; Claude Auto permission mode.
- codex — direct provider, codex-app-server-agent.ts; approval-policy/sandbox fixed at first prompt; fast_mode legacy config converts.
- copilot — only built-in ACP provider, copilot-acp-agent.ts over `copilot --acp`; custom agents via ACP session config; no version check (CLI handles auth).
- opencode — adapters target v1.14.46 + v2.0.10 (v2 floor 2.0.10); v1 plugin-file vs v2 package-dir bridge install via OPENCODE_CONFIG_CONTENT; per-location shared server, never stops reused service; execution-event turn completion.
- pi — process-backed, requires user-installed `pi` binary, talks `pi --mode rpc`; extension adapters under providers/pi/extensions/<extension>/ with registry.ts + contract.ts; JSONL session-file import discovery; image-input capability gating.
- omp (Oh My Pi) — first-class built-in, disabled by default; launches `omp --mode rpc-ui`; own providers/omp/ tree; imports ~/.omp/agent/sessions; native host-tool catalog registration.

In-server ACP adapters (shipped, named files under providers/): cursor-acp-agent.ts (Cursor; uses cursor/list_available_models RPC; versions lacking it must update), kimi-acp-agent.ts (Kimi), kiro-acp-agent.ts (Kiro), trae-acp-agent.ts (TRAE), generic-acp-agent.ts (user-defined `extends: "acp"` custom providers), mock-load-test-agent.ts (dev-only mock).

Provider plugins (shipped, version-floored): antigravity-provider (ACP over installed `agy acp` CLI; approvals+history needs Antigravity CLI ≥1.5.0), fx-provider (Vercel Labs fx; Settings catalog add), goose-provider (Block goose; multi-configured-provider model listing), hermes-provider (Nous Hermes agent; per-terminal-call command display), hoplite-provider (cloud agent; Settings add), muse-provider (Meta Muse Code; Settings catalog add).

ACP catalog breadth (public-docs/supported-providers/index.md "More agents through ACP", version-pinned in-app catalog is source of truth): Agoragentic, Amp, Auggie CLI, Autohand Code, Cline, Codebuddy Code, CodeWhale, Cortex Code, Corust Agent, crow-cli, Cursor, DeepAgents, Devin CLI, DimCode, Dirac, Factory Droid, fast-agent, Gajae Code, Gemini CLI, GLM Agent, goose, Grok, Hermes Agent, Junie, Kilo Code, Kimi Code CLI, Kiro CLI, MiniMax Code, Minion Code, Mistral Vibe, Nova, Poolside, Qoder, Qwen Code, siGit Code, Stakpak, TRAE CLI, VT Code. Evidence level: catalog listing = add-via-Settings supported; only catalog-listed Cursor/Kimi/Kiro/TRAE/Goose/Hermes also have dedicated adapters/plugins (deeper integration). Gemini/Grok/MiniMax/Zai/OpenCode-Go/OpenRouter appear as usage-source plugins (cost tracking) rather than agent adapters.

Custom providers: `extends: "codex"|"acp"|"pi"|"omp"` override pattern; OMP profiles extend `omp`, other Pi-compatible forks extend `pi` with overridden command + sessionDir.

Forges (adjacent provider-like registry, forge-registry.ts + protocol forge-manifest.ts): github, gitlab, gitea, forgejo, codeberg (+ GitHub compat shim since v0.2.0-beta.1, cleanup target 2027-01-17).

## 3. Feature mechanics with file pointers

- Parallel sessions/orchestration — agents launch/delegate/coordinate worker agents across providers; cross-agent prompts by ID, background tasks above composer, steer/interrupt/queue active turns; public-docs/orchestration.md + orchestration-workflows.md; server agent-manager.ts, background-work/, provider-subagents/.
- Machines/daemons/relay/self-host — daemon per host (daemon-instance.ts, daemon-config-store.ts, daemon-keypair.ts, bootstrap.ts), connected-hosts registry + CLI host add, relay package (cloudflare-adapter.ts, e2ee.ts, cutover-proxy.ts) + relay-transport.ts / relay-runtime.ts, Hub (server/hub/, public-docs/hub) for trigger validation + workspace sharing; docker/ + docs/docker.md for self-host.
- Worktrees — workspace-in-existing-worktree or from branch/PR; paseo-worktree-service.ts, resolve-worktree-creation-intent.ts, server/worktree/commands.ts, worktree-include plugin for large-repo scan scoping; detached-worktree archive semantics.
- Terminals — full terminal sessions (terminal/terminal.ts, terminal-session-controller.ts), Hermes Shell row rendering, per-call command display for Hermes, terminal-activity routes, agent-hooks (provider-registry.ts + claude/claude.ts).
- Editor/files — file-explorer (tree.ts, directory-listings.ts, preview-target.ts), file-pane/pane.tsx, Files tab in command-center tabs, Download button for preview-less files.
- Diffs/PRs — checkout-diff-manager.ts + checkout-diff-cache.ts; Changes pane with Push for never-pushed branches, Change-base picker; forge adapters github/gitlab/gitea/forgejo/codeberg services; PR/MR watch (checks/reviews/comments) + auto-archive-on-merge/.
- Permissions/approvals — permission-response.ts, voice-permission-policy.ts, permissions.md; per-provider modes (Claude Auto incl. Bedrock/Vertex fix; Copilot allow-all; Antigravity permission modes); OMP rpc-ui approval bridging; Pi extension-UI dialog bridging; toolPolicy.preapproved grants.
- Session resume/history — resumeSession/createSession adapter contract, describePersistence() handle rule, agent-history-search.ts, plugin-session-history.ts, provider-history-timestamps.ts; import/reopen native sessions (Pi JSONL discovery, OMP ~/.omp imports, OpenCode archive semantics); archive-discovery + archived-workspaces.
- Voice — expo-two-way-audio native module, voice-config.ts / voice-types.ts / voice-permission-policy.ts, session.voice-mcp-config, voice-roundtrip + voice-local-agent e2e; New-workspace dictation submit; voice-settings spec.
- Automations/schedules — server/schedule/ (service.ts, store.ts, cron.ts), schedule-run-lifecycle e2e; recurring schedules + heartbeats (Background-work heartbeat list); schedules-chat + schedules-cli user docs.
- Plugins/sidebar/themes — plugin SDK (@getpaseo/plugin) + gallery page (browse/search/install/settings/updates in-app); plugin composer controls, sidebar views (per-device setting), agent instructions, badges, sandbox launcher for plugin agent/terminal/worktree commands (HEAD commit); appearance/ (theme-picker.tsx, apply.ts) + catppuccin example; translations example + i18n resources (en/zh-CN/ja/ko + more).
- MCP/CLI/SDK — MCP servers mcp-server.ts / mcp-client-connection.ts / runtime-mcp-config.ts; Paseo tool injection toggle (Settings→host→Agents, off by default); @getpaseo/cli (workspace/agent/host/schedule commands); skills/paseo/SKILL.md reference.
- Cost/usage tracking — turn/session stats (timing/tokens/cache/cost) for Claude/OpenCode-v2/Pi/OMP/plugin providers; sidebar Usage summary + Usage cards ordering/toggles; provider quota forecasts; usage-source plugins (Claude/Codex/Copilot/Cursor/Grok/Kimi/MiniMax/OpenCode-Go/OpenRouter/Zai); Copilot premium-request quota.
- Notifications/push — app/src/push-notifications/ (index.native/index.web, subscriptions.ts, types.ts), websocket-server notifications/terminal-notifications tests, background-agent exclusion from tabs/notifications/lists, notifyOnFinish on send_agent_prompt.
- Mobile app surface — Expo app (app.config.js, eas.json), fastlane/ metadata (android present), iPhone Duo fold layout, mobile sheets/keyboard transitions, rebuilt mobile chat (expanding summaries/todos/tool groups), touch-sized tablet controls, mobile keyboard shortcuts, Archive workspace in mobile actions menu.
- Onboarding/settings/sync — Settings → Hosts → Providers catalog (+Provider flow), provider install/update in-app with diagnostics + model-list refresh, agent profiles (specialty presets), directory-sync/, bootstrap-auth/bootstrap-web-ui.
- Collaboration/sharing — Hub relationship-controller.ts + execution-controller.ts + daemon-executions.ts (cross-host supervision, Hub-sent workspace dir for trigger validation), connection-offer.ts, change-request-watches + change-request-router.ts (watch PRs/MRs), agent handoff skill (paseo-handoff) + committee/advisor skills.

## 4. Releases (latest 5)

- 0.17.4 (2026-11-23) — fix-only: Android lists/keyboard-exclusion/terminal input, case-insensitive-dir conversation loss, archived-agent steer queueing, OpenCode-Go logins, MCP 2026-07-28 tool absence, Codex MCP warning logs, Claude Auto on Bedrock/Vertex, Changes-pane Push, Hub model-catalog mismatch.
- 0.17.3 (2026-11-22) — fix-only: iPhone sheet swipe gestures, Antigravity thinking-level/Ochestrator-chat block, slash/file picker reload loss.
- 0.17.2 (2026-11-22) — turn/session stats + 30-min model refresh + sidebar-usage-by-default + agent steer-turns; mobile Archive action, provider diagnostics/docs links; fix batch (hosts reconnect, workspace retry, Antigravity schedules, plugin zombie processes).
- 0.17.1 (2026-11-21) — Antigravity approvals/history; existing-worktree workspaces; in-app provider updates; quota forecasts; PR/MR watches; heartbeat visibility; agent-message queueing; goose multi-provider models; plugin agent instructions.
- 0.16.2 (2026-11-18) — fix-only batch (Collapse-turns/background notifications, detached-worktree archive loss, Desktop/daemon update staleness, Hub reconnect, saved-hosts loss, .worktreeinclude timeouts, Claude compaction steer, double-Stop stuck turns, MCP background-work omission, OMP thinking selector).

## 5. Planned/roadmap vs shipped

- No ROADMAP file or roadmap section found in README/docs/product/index docs (grep for roadmap/planned/coming-soon/experimental in index/product docs: no hits).
- Shipped markers: paseo-plugin.json `requirements.paseo` floors per plugin (floor = shipped-since version); public-docs state version-gated requirements inline (e.g. "Requires Antigravity CLI 1.5.0 or later"); CHANGELOG entries are the shipped record.
- Provisional markers to treat as not-fully-shipped: forge compat shim cleanup target 2027-01-17 (explicit future date); OMP "disabled by default" (shipped but opt-in); in-app ACP catalog "version-pinned source of truth" (membership can change without repo change); 0.16.x items phrased as fixes to recently added features (plugin gallery, sidebar views, fx provider) indicate those shipped in 0.16.0, not planned.
- No TODO-roadmap labels surfaced in the inventoried surfaces; anything beyond CHANGELOG + version-floored plugin manifests should be verified against GitHub issues, not assumed planned.
