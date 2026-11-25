# Capy + companion-mode research (2026-11-25)

How Capy (https://capy.ai/) runs human-plus-agent-teams, what the Autumn
redesign proves about that loop, and what FloatingHotel's ACP sidebar should
steal. Research-only: no product code changed.

## 1. What Capy is (verified)

- Cloud-first multi-agent platform. One **thread** = one durable conversation
  with one agent about one piece of work, holding its machines, subagents and
  PRs. Threads live on Capy's servers, keep working after the laptop closes,
  and wake on CI results, reviews, or merges.
  (https://capy.ai/llms.txt, https://docs.capy.ai/threads; accessed 2026-11-25)
- **Subagents**: full child agents with their own conversation/tools, up to 3
  levels deep. Start as editable drafts (title + full prompt), do nothing until
  started. Placement is explicit: **shared** machine for read-only work
  (sees parent's working tree incl. uncommitted changes), **fresh** machine
  (clean checkout) for writers. Parallel subagents must touch disjoint files
  and ship separate PRs; **stacked** subagents build on each other's PR
  branches where files overlap ("when in doubt, stack").
  (https://docs.capy.ai/subagents)
- **Captain**: long-lived orchestrator over a crew of full threads. Hears from
  each crew thread every turn, keeps a rewritten **digest** (Needs-you / new /
  in-progress), tags people only for decisions. Rules live in its Drive:
  `AGENTS.md` (captain-only), `CREW.md` (read by every crew thread every turn),
  `NOTES.md`, `DIGEST.md`, `skills/`, `reports/`, `renders/`, `handoffs/`.
  One project per Captain; crew threads inherit its sharing; Captain always
  acts with the owner's GitHub access and model connections.
  (https://docs.capy.ai/captains)
- **Review agent**: separate agent reads each PR diff in a real checkout at
  the exact head, posts inline GitHub findings with category (bug/risk/
  maintainability/refactor), severity, confidence. Triage states
  Open/Resolved/Irrelevant sync back to GitHub threads; re-reviews are
  incremental (only new head in scope, irrelevants never re-flagged).
  Repository `AGENTS.md` rules are enforced as findings. Cost: 1/5/10 review
  credits (Low/Med/High); a Medium review is advertised at $0.40.
  (https://docs.capy.ai/review, https://capy.ai/compare/capy-vs-coderabbit)
- **PR ownership + wake events**: the thread that last pushed owns the PR and
  gets CI/human-feedback/conflict/merge events. Per-event setting: React
  (wake) / Keep as context / Ignore; bot feedback defaults to Ignore.
  Capy never merges unless asked (or repo-root `AGENTS.md` grants it).
  (https://docs.capy.ai/pull-requests)
- **Feedback in**: web/desktop apps, `@Capy` in Slack (with `captain=<name>`
  routing, `!queue` / `!steer` / `!web` / `mute` / `stop` / `archive`
  directives, `aside` prefix for human-only chatter), Linear delegation
  (issue becomes first prompt, progress posted back, Linear stop button
  interrupts), API, automations (schedule/webhook/event triggers; each run is
  a normal thread). (https://docs.capy.ai/integrations/slack,
  https://docs.capy.ai/automations, llms-full.txt §Linear)
- **Live product previews**: Vercel integration = automatic preview
  deployments per change; every cloud machine has a Linux desktop + Chrome
  the agent drives (screenshots/video attached to the PR, watchable via
  Desktop/Browser tabs); Pro+ attaches cloud iPhones/Android phones, builds
  native apps on Revyl runners and taps through them. Standing rule pattern:
  "before opening a PR for a UI change, start the app, open each changed page,
  attach a screenshot" in `AGENTS.md`. (llms-full.txt §browser-testing,
  https://docs.capy.ai/cloud-phones)
- **Interruption model**: Steer (default, Enter — reaches agent at next step),
  Interrupt (Cmd+Enter — stops generation), Queue (Opt+Enter — waits for turn
  end). Queued/steered messages sit above the composer, editable/discardable.
  Edit-message re-runs from a changed message with a version switcher; Escape
  cancels. (https://docs.capy.ai/threads §send-and-steer)
- **Status visibility**: Working / Waiting (on CI/subagent/timer) / Failed /
  Idle, set by the system, never the user; **Needs-you** is a separate flag
  (answer, decision, env vars, MCP approval, credits). Sidebar: bold = unread,
  badges count mentions, idle rows show CI/PR state ("Checks", "PR failing",
  "PR ready"). (https://docs.capy.ai/threads §statuses)
- **Context system**: repo root `AGENTS.md` (every thread), nested `AGENTS.md`
  (lazy-injected on first file touch in that subtree, deeper wins, sticky),
  glob rules (`.capy/rules/`, `.cursor/rules/`), skills (name+description
  advertised, loaded on demand), Drive (cross-thread files outside git, scoped
  org/personal/project/automation, versioned with history+restore).
  Explicit anti-bloat guidance: lean root, narrowest scope, no secrets.
  (https://docs.capy.ai/instructions, https://docs.capy.ai/drive,
  https://docs.capy.ai/skills)
- **Billing**: per-org plans, unlimited members, no seats. Free ($0, own
  machines + own model keys, 100 review credits/mo); Lite $20/mo; Pro $100/mo;
  Max $200/mo; Enterprise custom. Machine minutes + review credits + model
  usage included monthly; BYO ChatGPT/Copilot/SuperGrok/API keys make tokens
  free, machines/reviews still metered. Annual −20%. SOC 2 Type II; cloud
  machines hold no GitHub token (server-side proxy); OIDC instead of stored
  keys; Tailscale ephemeral nodes for private APIs; zero-retention on request.
  (https://capy.ai/llms.txt, https://capy.ai/security)
- Founded 2024, YC F24, San Francisco. (https://capy.ai/llms.txt)

## 2. The Autumn redesign loop (verified, published 2026-11-24)

Source: https://useautumn.com/blog/redesigning-autumn-in-3-days (founder's
own write-up; treat quality claims as his, workflow mechanics as verified).

- One captain ("Lando") over many Opus 5.5 threads, each owning one part of
  the site. Founder talks only to Lando; Lando routes each note to the
  part-owning thread. ~5 threads at once, 71 Paper files, landing page + full
  blog redesign in 3 days without reading a line of code.
- **Native design context beats screenshots**: Paper Snapshot (Chrome ext.)
  grabs a site's actual HTML/CSS into Paper as editable elements; 20+ reference
  sites snapshotted, annotated, referenced per-part. Screenshots force the
  model to re-derive structure; native format skips that loss.
- Each thread **designs in Paper, ships to its own preview** (Capy cloud
  pages). Reviewer goes Paper → live preview in minutes.
- Feedback arrives two ways and both route to Lando: prompting in Slack, or
  **Paper comments forwarded to Lando**, reflected in revised designs minutes
  later. Teammate (Ayush) never touches Capy directly.
- Taste checkpoints are the load-bearing wall: "design is subjective, there's
  no way to give the agent something to verify, so it was vital I was
  constantly reinforcing what felt good and what didn't." Humanizer-skill pass
  on copy, agent fuzz QA (dead links), Remotion teaser at the end.
- Setup quality decided output quality: design system + reference digest built
  first, per-part context handed out, Emil Kowalski animation skills referenced
  for the motion pass — i.e. skills-as-taste-injection.

## 3. Verified vs marketing vs unknown

- **Verified** (docs + first-party how-tos + dated third-party table): thread/
  subagent/captain mechanics, digest + Needs-you, CREW.md/Drive, review-agent
  triage, PR wake events, Slack directives, steer/interrupt/queue, per-org
  no-seat pricing, SOC 2, machine/phone specs, Paper remote MCP existing in
  early access (Paper founder post 2026-11-16 via
  https://news.sulat.com/2026/11/17/paper-remote-mcp/; design-canvas comparison
  table checked 2026-11-06..19 at
  https://brilliant.design/blog/design-canvases-for-coding-agents — note
  author sells a competitor).
- **Marketing-weight** (plausible, single-source): "thousands of agents",
  "5x more PRs" (Goblins customer story), Autumn quality claims, $0.40/review
  comparisons. Don't quote without the source page.
- **Unknown / couldn't verify**: captain quality head-to-head (GPT-5.5 "most
  Captain-ready" is Capy's own eval), real per-seat savings math (the April
  efficienist "$20 gone before lunch" critique predates current $20/$100/$200
  tiers — stale, do not cite as current), Paper-integration internals beyond
  "designs in your Paper files from a cloud machine, canvas live in thread",
  Vercel-preview failure modes, review-agent false-positive rates.

## 4. Steal map for FloatingHotel

Conventions: "Exists" cites `research/capy-companion-mode` base
(`origin/acp-agents`); verdicts are Do (small, fits), Park (needs a decision
or a dependency), Reject (doesn't fit). Success signals are observable, not
vibes.

### S1. Steer / interrupt / queue send modes — DO
- Job: human watches a tab's agent work and redirects mid-turn without
  killing the process.
- Exists: `docs/acp-agents.md` — Send becomes Stop while prompting
  (`session/cancel`); panel Stop kills the process. One mode only.
- Smallest: three send paths — Steer (send prompt, let turn continue),
  Interrupt (cancel turn, then send), Queue (hold until turn ends; show above
  composer, editable/discardable). `session/cancel` already wired; queue is
  client-side state.
- Risks: agents that don't support mid-turn prompts (check per-agent; fall
  back to queue). Cost: panel-only, ~1 day.
- Success: steering a running turn without process restart; queued prompt
  lands after turn end intact.

### S2. Per-tab Needs-you + live status line — DO
- Job: human with N repo tabs sees which agent needs them without opening
  each transcript (Capy's digest pattern, local edition).
- Exists: transcript streams + permission banner per tab
  (`src/ecs/agent_panel.h`); tab bar exists (`src/ecs/tab_bar_system.h`).
  No tab-level status aggregation.
- Smallest: derive per-tab state the Capy way (working/waiting-on-
  permission/idle + needs-you flag while a permission banner is unanswered);
  show a dot + "needs you" marker on the tab and Agent tab icon. No digest
  prose — the transcript is the digest at our scale.
- Risks: near-zero (read-only derivation). Success: unanswered permission
  visible from any tab within a glance.

### S3. Agent brief: repo conventions auto-attached — DO
- Job: agent starts with the rules that matter (Capy's root-AGENTS.md-always-
  loaded pattern) instead of only the open file.
- Exists: File ✓ attaches open file as embedded resource
  (`docs/acp-agents.md`); repo has `CLAUDE.md`, `CORRECT.md` (evidence-backed
  rules). Nothing attaches them today.
- Smallest: on first prompt of a session, prepend repo root `AGENTS.md`/
  `CLAUDE.md`/`CORRECT.md` (whichever exist, capped lines) + open file.
  Keep lean per Capy's own anti-bloat guidance.
- Risks: context bloat on big files — cap and note truncation. Success: agent
  follows palette/theme/test-registry rules unprompted on first try.

### S4. One-click "Review my diff → feedback basket" prompt — DO
- Job: Capy's review-agent triage loop (findings with severity → human keeps/
  edits → agent applies) using our existing basket instead of GitHub comments.
- Exists: feedback basket + "Send review feedback (N)" handoff to agent
  (`src/ecs/agent_panel.h:544`, `src/review_store.cpp`); no starter prompt.
- Smallest: prompt template button that asks the agent for findings as
  basket-shaped Markdown (file:line + severity + one-line why), human triages
  in basket, existing Send applies. Fuzz-QA variant ("build, click through,
  report dead links" — Autumn's QA pass) as a second template using client
  terminals.
- Risks: finding quality varies by model; basket triage is the backstop.
  Success: agent findings land directly sendable; zero paste-reformatting.

### S5. Agent-per-worktree isolation option — DO proposal, PARK build
- Job: parallel tabs' agents can't clobber each other's trees (Capy's
  fresh-machine-per-writer rule, local edition).
- Exists: one agent process per repo tab sharing one checkout
  (`docs/acp-agents.md`); worktree listing/open decided Do (triage CP-041/
  CP-042), creation deferred (CP-044). No per-tab isolation today.
- Smallest useful: offer "start agent in a new worktree of this branch" at
  agent Start (reuses CP-041/042/044 machinery when built); label tab with
  worktree (CP-043 territory).
- Risks/costs: checkout disk + refresh load; conflicts still possible at
  merge — Capy's "when in doubt, stack" advice applies to humans too.
- Deps: CP-041/042/044. Success: two agents edit different worktrees, zero
  cross-tab file fights. Park until worktree browsing lands.

### P1. Structured snapshots over screenshots (principle) — ADOPT, no build
- Autumn proof: native HTML/CSS context >> screenshots for design fidelity.
- Our edition: the panel already serves live edit buffers over ACP
  (`fs/read_text_file` prefers buffer) and attaches file text, not screen
  grabs — that is the same principle. Keep it: when tempted to screenshot UI
  for the agent, attach DOM/diff/file text instead. Image prompts stay for
  genuinely visual input (mock parity). No todo needed.

### P2. Human taste checkpoints stay human — AFFIRM, no build
- Autumn: no objective verifier for design ⇒ constant human reinforcement.
- Our edition already matches: permission banner gates tool calls, agent edits
  to open files land as unsaved changes (Cmd+S stays human), nothing
  stages/commits (`docs/acp-agents.md` safety properties). Don't erode this
  for "flow". No todo needed.

### P3. Captain digest / multi-thread orchestration UI — REJECT for now
- One Captain per area with digests makes sense at team scale (many people,
  many threads, async cloud). FH is one human, N tabs, local processes; S2's
  status dots cover the actual need. Revisit only if FH ever drives unattended
  agent fleets. (Also note Capy's real limits: one project per Captain, one
  crew per thread, Captain acts as owner — orchestration has sharp edges.)

### P4. Hosted-PR ownership + CI wake events — PARK behind existing defers
- Gated on triage CP-051/052 (PR inbox/reader), both deferred. If those ever
  land, Capy's event-preference pattern (React/Keep-as-context/Ignore per
  event class) is the design to copy. No new todo.

### P5. Skills-as-taste-injection (animation/design-system skills) — PARK
- Autumn's motion pass worked because Emil's skills encoded taste the agent
  lacks. FH analogue: MCP settings + `acp_mcp_servers` exist; what doesn't
  is a skills directory/convention for repo-local agent procedures. Small but
  needs a home decision (repo `skills/`? settings?). Park until an agent
  workflow (e.g. S4 QA pass) proves the need.

### Speculation (not verified, do not build on)
- Whether Paper-comment→Lando forwarding is webhook, polling, or MCP
  resource subscription — mechanism unknown; the *pattern* (comments on the
  artifact become agent tasks with the artifact as context) is what's portable,
  and our basket already does it for diffs.
- Whether per-thread Vercel previews were load-bearing in Autumn vs. nice —
  the post stresses cloud pages for review speed; Vercel auto-previews are
  from Capy's integration page (marketing surface, couldn't render full text).
- Capy "verify from own reads, never the thread's claim" (migration guide) —
  our diff refreshes from disk on next refresh already; fine as-is.

## 5. Top 5 steals (priority order)
1. S1 steer/interrupt/queue send modes.
2. S2 per-tab needs-you + status dots.
3. S3 agent brief (conventions auto-attached to first prompt).
4. S4 review-to-basket + fuzz-QA prompt templates.
5. S5 agent-per-worktree isolation (proposal now, build after CP-041/042/044).

## 6. Read first
1. Autumn post §Building up high quality context + feedback paragraphs
   (the whole loop in 2 screens).
2. Capy docs: Subagents (shared-vs-fresh, parallel-vs-stacked) then Captains
   (digest, CREW.md) — both short.
3. This doc §4 S1–S5 when scoping.

## Sources (all accessed 2026-11-25)
- https://capy.ai/llms.txt, https://capy.ai/llms-full.txt (primary index +
  full article texts)
- https://useautumn.com/blog/redesigning-autumn-in-3-days (publ. 2026-11-24)
- https://docs.capy.ai/captains, /threads, /subagents, /drive, /instructions,
  /skills, /pull-requests, /review, /automations, /desktop, /dock,
  /cloud-phones, /machines, /environment, /models-and-pricing,
  /integrations/slack (.md variants)
- https://capy.ai/features/captains, https://capy.ai/index.md,
  https://capy.ai/llm-info
- https://news.sulat.com/2026/11/17/paper-remote-mcp/ (Paper remote MCP
  launch, via founder X post 2026-11-16)
- https://brilliant.design/blog/design-canvases-for-coding-agents (7-canvas
  comparison; vendor-authored, dated cells)
- https://efficienist.com/capy-ai-launches-a-multi-agent-cloud-ide-but-does-20-get-you-anywhere/
  (2026-04-02 — stale pricing, do not cite as current)
