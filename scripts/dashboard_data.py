#!/usr/bin/env python3
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
# pyre-strict
"""
Dashboard Data Layer — Pure logic, fully unit-testable.

This module contains all pure data processing, status detection,
decision logic, and message formatting used by the Claw Town dashboard.

ZERO imports of: curses, termios, tty, select, subprocess, or tmux calls.
Every function takes structured inputs and returns structured outputs.
"""
# @noautodeps

from __future__ import annotations

import re
from typing import Any


# ═══════════════════════════════════════════════════════════════════════════════
# Status Constants
# ═══════════════════════════════════════════════════════════════════════════════


class AgentStatus:
    """Agent status constants."""

    WORKING = "working"
    COMPLETED = "completed"
    NEEDS_INPUT = "needs_input"
    NEEDS_HUMAN = "needs_human"
    NEEDS_ORCHESTRATOR = "needs_orchestrator"
    NEEDS_AGENT = "needs_agent"
    SLEEPING = "sleeping"
    FREE = "free"
    UNKNOWN = "unknown"


class OrchestratorStatus:
    """Orchestrator status constants with detailed waiting states."""

    WORKING = "working"
    WAITING_FOR_HUMAN = "waiting_for_human"
    WAITING_FOR_AGENT = "waiting_for_agent"
    WAITING_FOR_SUB_ORCH = "waiting_for_sub_orch"
    IDLE = "idle"
    UNKNOWN = "unknown"


class TaskStatus:
    """Task status constants."""

    OPEN = "open"
    BLOCKED = "blocked"
    IN_PROGRESS = "in_progress"
    COMPLETED = "completed"
    FAILED = "failed"


# Infrastructure windows that are never treated as agents.
IGNORED_WINDOWS: set[str] = {"control", "zsh", "clone-ops", "dashboard"}


def is_ignored_window(name: str) -> bool:
    """Return True if *name* belongs to an infrastructure window."""
    if name in IGNORED_WINDOWS:
        return True
    if name.startswith("init-"):
        return True
    return False


# ═══════════════════════════════════════════════════════════════════════════════
# Agent Status Detection (Step 1)
# ═══════════════════════════════════════════════════════════════════════════════


def detect_agent_status(pane_output: str) -> tuple[str, str]:
    """Detect agent status from tmux pane output.

    Returns: (status, detail) where detail explains WHO input is needed from.

    Priority order:
    1. Completion/blocked signals (highest — definitive state)
    2. Human input prompts (questions, options)
    3. Orchestrator waiting patterns
    4. Sleeping/polling patterns
    5. Inbox/daemon detection
    6. Busy indicators (lowest — spinners, "Running:")
    """
    if not pane_output:
        return AgentStatus.UNKNOWN, ""

    lower_output = pane_output.lower()

    # Get recent output for context detection — used for completion signals too,
    # because the full buffer includes the agent's prompt instructions which
    # contain "TASK_COMPLETE" as instructional text, causing false positives.
    recent = pane_output[-800:]
    recent_lower = recent.lower()

    # 1. Check for completion/blocked signals in RECENT output only
    if any(
        signal in recent_lower
        for signal in ["task_complete", "task complete", "taskcomplete"]
    ):
        return AgentStatus.COMPLETED, "completed"
    if "TASK_BLOCKED" in recent:
        match = re.search(r"TASK_BLOCKED:\s*(.+?)(?:\n|$)", recent)
        reason = match.group(1).strip()[:30] if match else "blocked"
        return AgentStatus.NEEDS_AGENT, reason

    # 2. Check for human input prompts (questions, numbered options)
    human_patterns = [
        ("1.", "choose option"),
        ("2.", "choose option"),
        ("(y/n)", "confirm"),
        ("[y/N]", "confirm"),
        ("[Y/n]", "confirm"),
        ("Which", "question"),
        ("What would you like", "question"),
        ("Please select", "choose"),
        ("Choose", "choose"),
        ("Enter your", "input needed"),
        ("Type your", "input needed"),
        ("?", "question"),
    ]
    for pattern, detail in human_patterns:
        if pattern in recent[-200:]:
            return AgentStatus.NEEDS_HUMAN, detail

    # 3. Check for orchestrator waiting patterns
    orchestrator_patterns = [
        ("waiting for task", "next task"),
        ("waiting for assignment", "assignment"),
        ("ready for next", "next task"),
        ("what should i", "direction"),
        ("awaiting instructions", "instructions"),
        ("TASK_NEEDS_CLARIFICATION", "clarification"),
    ]
    for pattern, detail in orchestrator_patterns:
        if pattern.lower() in lower_output:
            return AgentStatus.NEEDS_ORCHESTRATOR, detail

    # 4. Check for sleeping/polling patterns
    sleep_patterns = [
        ("sleeping", "polling"),
        ("waiting for", "waiting"),
        ("polling", "polling"),
        ("watching", "monitoring"),
        ("monitoring", "monitoring"),
    ]
    for pattern, detail in sleep_patterns:
        if pattern.lower() in lower_output[-300:]:
            return AgentStatus.SLEEPING, detail

    # 5. Check if it's an inbox/status daemon (special case)
    if "inbox" in lower_output or "status watcher" in lower_output:
        return AgentStatus.SLEEPING, "daemon"

    # 6. Check for busy indicators (lowest priority)
    busy_patterns = [
        "esc to interrupt",
        "press esc to interrupt",
        "✻",
        "⠋",
        "⠙",
        "⠹",
        "⠸",
        "Thinking...",
        "Running:",
    ]
    for pattern in busy_patterns:
        if pattern.lower() in lower_output:
            return AgentStatus.WORKING, ""

    # Default: has prompt visible, needs orchestrator direction
    if "❯" in recent[-100:]:
        return AgentStatus.NEEDS_ORCHESTRATOR, "idle"

    return AgentStatus.NEEDS_INPUT, ""


def detect_human_input_prompt(pane_output: str) -> bool:
    """Detect if the pane is showing a prompt for human input.

    Returns True if:
    - AskUserQuestion prompt with numbered options
    - Direct input prompt at end of output
    - Question asking for user response
    """
    if not pane_output:
        return False

    recent = pane_output[-3000:]
    lines = recent.strip().split("\n")
    last_line = lines[-1].strip() if lines else ""

    has_numbered_options = "1." in recent and "2." in recent
    has_prompt = any(
        line.strip() == "❯" or line.strip().endswith("❯") for line in lines[-10:]
    )

    if has_numbered_options and has_prompt:
        return True

    if last_line == "❯" or last_line.endswith("❯"):
        very_recent = pane_output[-200:]
        if not any(spin in very_recent for spin in ["⠋", "⠙", "⠹", "⠸"]):
            return True

    if has_numbered_options:
        return True

    explicit_prompts = [
        "What would you like",
        "Please select",
        "Choose an option",
        "Enter your",
        "Type your",
        "(y/n)",
        "[y/N]",
        "[Y/n]",
        "Press Enter",
        "confirm?",
        "proceed?",
    ]

    for pattern in explicit_prompts:
        if pattern.lower() in recent.lower():
            return True

    return False


def detect_waiting_context(pane_output: str) -> tuple[bool, list[str]]:
    """Detect if output mentions waiting for agents or sub-orchestrators.

    Returns (is_waiting, entity_names)
    """
    if not pane_output:
        return False, []

    waiting_patterns = [
        r"waiting (?:for|on) (?:agent|agents)",
        r"spawned (?:agent|agents)",
        r"sub-orchestrator",
        r"agent \w+ is working",
        r"agents? running",
    ]

    entities: list[str] = []
    for pattern in waiting_patterns:
        if re.search(pattern, pane_output.lower()):
            match = re.search(r"(?:agent|sub-orchestrator)[:\s]+([^\s,]+)", pane_output)
            if match:
                entities.append(match.group(1))
            return True, entities

    return False, []


# ═══════════════════════════════════════════════════════════════════════════════
# Nudge Decision Logic (Step 2)
# ═══════════════════════════════════════════════════════════════════════════════


def should_nudge(
    idle_time: float,
    nudge_interval: float,
    last_nudge_time: float,
    nudge_cooldown: float,
    nudge_count: int,
    now: float,
    agents: list[dict[str, Any]],
    tasks: list[dict[str, Any]],
    orchestrator_status: str = "",
) -> bool:
    """Determine if orchestrator needs a nudge.

    Pure decision function — does not execute the nudge.

    When *orchestrator_status* is ``WAITING_FOR_HUMAN`` the orchestrator
    is blocked on user input and should never be nudged.
    """
    # Never nudge when waiting for human input
    if orchestrator_status == OrchestratorStatus.WAITING_FOR_HUMAN:
        return False

    cooldown_elapsed = now - last_nudge_time > nudge_cooldown

    if idle_time < nudge_interval:
        return False

    if not cooldown_elapsed:
        return False

    # Check if there's something actionable
    needs_input = [a for a in agents if a.get("status") == AgentStatus.NEEDS_INPUT]
    ready_tasks = [t for t in tasks if is_task_ready(t, tasks) and not t.get("owner")]

    return bool(needs_input or ready_tasks)


def should_nudge_agent(
    agent: dict[str, Any],
    nudge_state: dict[str, Any],
    agent_max_nudges: int,
    agent_nudge_interval: float,
    agent_nudge_cooldown: float,
    completed_agents: set[str],
    now: float,
) -> bool:
    """Check if a specific agent needs nudging.

    Pure decision function — does not execute the nudge.
    """
    agent_name = agent.get("name", "")
    status = agent.get("status", AgentStatus.UNKNOWN)

    # Never nudge completed agents
    if agent.get("status_detail") == "completed" or agent_name in completed_agents:
        return False

    # Only nudge idle or needs_input agents
    if status not in [AgentStatus.FREE, AgentStatus.NEEDS_INPUT]:
        return False
    if status == AgentStatus.COMPLETED:
        return False

    # Check if max nudges exceeded
    if nudge_state.get("nudge_count", 0) >= agent_max_nudges:
        return False

    idle_time = now - nudge_state.get("last_activity", now)
    cooldown_elapsed = now - nudge_state.get("last_nudge", 0) > agent_nudge_cooldown

    return idle_time >= agent_nudge_interval and cooldown_elapsed


def build_nudge_message(
    agents: list[dict[str, Any]], tasks: list[dict[str, Any]]
) -> str:
    """Build the nudge message for the orchestrator."""
    needs_input = [a for a in agents if a.get("status") == AgentStatus.NEEDS_INPUT]
    ready_tasks = [t for t in tasks if is_task_ready(t, tasks) and not t.get("owner")]

    return f"""\U0001f6a8 **WHY ARE YOU STALLED? KEEP GOING!** \U0001f6a8

You've been idle. The orchestrator must ALWAYS be making progress.

**Current State:**
- Agents needing input: {len(needs_input)}
- Ready tasks without owner: {len(ready_tasks)}
- Total agents: {len(agents)}

**What you should be doing RIGHT NOW:**
1. Check if any agents completed \u2192 collect their outputs
2. Spawn agents for ready tasks \u2192 don't let work pile up
3. Nudge stuck agents \u2192 send them recovery prompts
4. Check task dependencies \u2192 unblock what you can

**CARDINAL RULES:**
\u26d4 ALL work is spawned as new Claude sessions - are you spawning or doing work directly?
\u26d4 Keep orchestrating - don't wait for input, use your judgment!

**Stop waiting. Start orchestrating. NOW.**"""


_AGENT_NUDGE_MESSAGES = [
    "If you're waiting for clarification, use your best judgment and continue. Make reasonable assumptions and proceed.",
    "Don't wait for input - you have autonomy to make decisions. Keep working and adjust if needed later.",
    "Continue with your task. If you're stuck, try a different approach rather than waiting.",
    "Make a decision and proceed. You can always course-correct, but don't block on missing info.",
    "Finish your task autonomously. Signal TASK_COMPLETE when done - don't wait for confirmation.",
]


def build_agent_nudge_message(nudge_count: int, task_context: str = "") -> str:
    """Build a nudge message for a worker agent."""
    message = _AGENT_NUDGE_MESSAGES[nudge_count % len(_AGENT_NUDGE_MESSAGES)]
    if task_context:
        message = f"{message} - {task_context}"
    return message


_SUB_ORCH_NUDGE_MESSAGES = [
    "Use your best judgment and continue orchestrating. Don't wait for input - make decisions autonomously.",
    "Keep your subtree moving. If you need clarification, make reasonable assumptions and proceed.",
    "Continue coordinating your work. You have autonomy to make decisions - adjust later if needed.",
    "Don't block on missing information. Use your judgment and keep the workflow progressing.",
]


def build_sub_orch_nudge_message(nudge_count: int) -> str:
    """Build a nudge message for a sub-orchestrator."""
    return _SUB_ORCH_NUDGE_MESSAGES[nudge_count % len(_SUB_ORCH_NUDGE_MESSAGES)]


# ═══════════════════════════════════════════════════════════════════════════════
# Task / Event Logic (Step 3)
# ═══════════════════════════════════════════════════════════════════════════════


def find_newly_completed_tasks(
    tasks: list[dict[str, Any]],
    previous_statuses: dict[str, str],
    learning_emitted_tasks: set[str],
) -> list[dict[str, Any]]:
    """Find tasks that just transitioned to 'completed'.

    Returns list of newly completed task dicts.
    """
    newly_completed: list[dict[str, Any]] = []

    for task in tasks:
        tid = task.get("id", "")
        current_status = task.get("status", "")
        previous_status = previous_statuses.get(tid, "")

        if current_status == "completed" and previous_status != "completed":
            if tid not in learning_emitted_tasks:
                newly_completed.append(task)

    return newly_completed


def find_new_task_ids(
    current_ids: set[str],
    previous_ids: set[str],
    recently_created_ids: set[str] | None = None,
) -> set[str]:
    """Find task IDs that were added since the last check.

    Returns set of new task IDs (excludes 'orchestrator').
    *recently_created_ids*, if provided, are also excluded to avoid
    self-notifications when the orchestrator creates tasks itself.
    """
    if not previous_ids:
        return set()
    new_ids = current_ids - previous_ids
    if recently_created_ids:
        new_ids -= recently_created_ids
    return new_ids


def build_new_task_notification(new_tasks: list[dict[str, Any]], timestamp: str) -> str:
    """Build notification message for externally added tasks."""
    lines = [
        f"NEW TASKS DETECTED at {timestamp} — "
        f"{len(new_tasks)} task(s) added externally:",
        "",
    ]
    for t in new_tasks:
        tid = t.get("id", "?")
        title = t.get("title") or t.get("name") or "Untitled"
        status = t.get("status", "open")
        tn = t.get("task_number", "")
        tn_str = f" ({tn})" if tn else ""
        lines.append(f"  - Task {tid}{tn_str}: {title} [{status}]")

    lines.extend(
        [
            "",
            "Please review these tasks and spawn agents for any that "
            "need work. Check their blocked_by dependencies before starting.",
        ]
    )

    return "\n".join(lines)


def detect_agent_completion_signal(last_output: str) -> bool:
    """Check if agent output contains a task completion signal.

    Checks case-insensitively for TASK_COMPLETE variants.
    """
    if not last_output:
        return False
    last_output_lower = last_output.lower()
    return any(
        signal in last_output_lower
        for signal in ["task_complete", "task complete", "taskcomplete"]
    )


def build_completion_notification(
    agent_name: str, task_id: str, task_title: str, project: str
) -> str:
    """Build the close-out notification for a completed agent task."""
    tasklib = "python3 ~/.claude/skills/claw-town/scripts/claw_town_tasklib.py"
    tjson = "python3 ~/.claude/skills/claw-town/scripts/claw_town_tasks_json.py"
    return (
        f"\U0001f4cb **Agent '{agent_name}' completed task {task_id}**"
        f"{f': {task_title}' if task_title else ''}\n\n"
        f"Run the close-out sequence:\n"
        f"1. Read findings: `{tasklib} comments --task {task_id} --prefix FINDINGS`\n"
        f"2. Close remote task: `{tasklib} close --task {task_id}`\n"
        f"3. Update tasks.json: `{tjson} {project} update {task_id} --status completed`\n"
        f"4. Check for unblocked downstream tasks and spawn them\n"
    )


def build_learning_event_message(
    task_id: str,
    task_name: str,
    title: str,
    owner: str,
    learning_content: str | None,
    timestamp: str,
) -> str:
    """Build the learning event message for a completed task."""
    lines = [
        f"LEARNING EVENT \u2014 Task {task_id} ({task_name}) completed at {timestamp}",
        f"Title: {title}",
        f"Owner: {owner}",
    ]
    if learning_content:
        lines.append("")
        lines.append("Findings:")
        lines.append(learning_content)
    else:
        lines.append("")
        lines.append(
            "No findings found in local files or task comments. "
            "Agent should post findings via: python3 "
            "~/.claude/skills/claw-town/scripts/claw_town_tasklib.py comment "
            f"--task {task_id} --prefix FINDINGS --content '...'"
        )

    return "\n".join(lines)


def build_inbox_message(
    text: str,
    sender: str,
    target: str = "orchestrator",
    priority: str = "normal",
    source: str = "system",
    **extra_fields: Any,
) -> dict[str, Any]:
    """Build a structured inbox message dict."""
    msg: dict[str, Any] = {
        "text": text,
        "sender": sender,
        "target": target,
        "priority": priority,
        "source": source,
    }
    msg.update(extra_fields)
    return msg


# ═══════════════════════════════════════════════════════════════════════════════
# Sanitization and Utility Functions (Step 4)
# ═══════════════════════════════════════════════════════════════════════════════


def sanitize_content(content: str) -> str:
    """Sanitize content to prevent parsing errors.

    Replaces smart quotes and problematic Unicode characters.
    """
    content = content.replace("\u201c", '"').replace("\u201d", '"')
    content = content.replace("\u2018", "'").replace("\u2019", "'")
    content = content.replace("\u2014", "-").replace("\u2013", "-")
    return content


def normalize_output_for_comparison(output: str) -> str:
    """Strip dynamic content (timers, spinners, token counts) from output.

    This ensures we only detect REAL activity changes, not just timer updates.
    """
    normalized = output

    # Strip time durations (e.g., "6m 7s", "1h 23m 45s", "45s")
    normalized = re.sub(r"\d+[hms]\s*\d*[ms]?\s*\d*[s]?", "", normalized)

    # Strip token counts (e.g., "↓ 1.6k tokens", "↑ 500 tokens")
    normalized = re.sub(r"[↓↑]\s*[\d.]+k?\s*tokens?", "", normalized)
    normalized = re.sub(r"in:\d+k?\s*out:\d+k?", "", normalized)

    # Strip context percentages (e.g., "ctx:31%", "10%")
    normalized = re.sub(r"ctx:\d+%", "", normalized)
    normalized = re.sub(r"\d+%", "", normalized)

    # Strip spinner states
    normalized = re.sub(r"thinking", "", normalized)
    normalized = re.sub(r"✻", "", normalized)

    # Strip "Context left until auto-compact: X%"
    normalized = re.sub(r"Context left until auto-compact:\s*\d+%", "", normalized)

    # Strip sz: size indicators
    normalized = re.sub(r"sz:\d+k?", "", normalized)

    return normalized


def check_stuck_orchestrator(
    orch_output: str,
    stuck_threshold: int = 180,
) -> tuple[bool, str]:
    """Detect if orchestrator is stuck in 'thinking' state with background tasks.

    Returns (is_stuck, stuck_type) where stuck_type describes the pattern.
    """
    is_thinking = "thinking" in orch_output.lower()
    has_background_tasks = "background task" in orch_output.lower()

    # Extract elapsed time if present (e.g., "6m 7s")
    elapsed_match = re.search(r"(\d+)m\s*(\d+)?s?", orch_output)
    elapsed_seconds = 0
    if elapsed_match:
        minutes = int(elapsed_match.group(1))
        seconds = int(elapsed_match.group(2)) if elapsed_match.group(2) else 0
        elapsed_seconds = minutes * 60 + seconds

    # Check for hour format too (e.g., "1h 23m")
    hour_match = re.search(r"(\d+)h\s*(\d+)?m?", orch_output)
    if hour_match:
        hours = int(hour_match.group(1))
        minutes = int(hour_match.group(2)) if hour_match.group(2) else 0
        elapsed_seconds = hours * 3600 + minutes * 60

    if (is_thinking or has_background_tasks) and elapsed_seconds > stuck_threshold:
        stuck_type = (
            "thinking_with_bg_tasks" if has_background_tasks else "long_thinking"
        )
        return True, stuck_type

    return False, ""


def known_tasks_to_list(data: dict[str, Any]) -> list[dict[str, Any]]:
    """Convert a known_tasks dict to a tasks list.

    The new schema stores tasks as ``known_tasks`` keyed by T-number.
    Many dashboard methods expect a flat ``tasks`` list with an ``id``
    field.  This helper bridges the two representations.

    The ``status`` field holds the agent lifecycle state (e.g. "working",
    "completed", "pending"). We map it to dashboard display categories
    (e.g. "in_progress", "completed", "open").
    """
    _STATUS_TO_DISPLAY = {
        "working": "in_progress",
        "completed": "completed",
        "pending": "pending",
        "orchestrator": "open",
    }

    known = data.get("known_tasks")
    if not known or not isinstance(known, dict):
        return data.get("tasks", [])
    tasks: list[dict[str, Any]] = []
    root_task_id = data.get("root_task", "")
    for t_num, t_data in known.items():
        entry: dict[str, Any] = {"id": t_num}
        entry.update(t_data)
        # Identify the orchestrator task: match root_task, or legacy name/status fields
        if (
            t_num == root_task_id
            or t_data.get("name") == "orchestrator"
            or t_data.get("status") == "orchestrator"
        ):
            entry.setdefault("type", "orchestrator")
        # Map lifecycle status to dashboard display status
        raw_status = entry.get("status") or ""
        # Also check legacy agent_state for backward compat with old data
        if not raw_status:
            raw_status = entry.get("agent_state", "")
        entry["status"] = _STATUS_TO_DISPLAY.get(raw_status, raw_status or "open")
        # Override to "blocked" if the task has unresolved blockers
        if entry["status"] in ("pending", "open"):
            blocked_by = (
                entry.get("blocked_by")
                or entry.get("blockedBy")
                or entry.get("depends_on")
                or []
            )
            if blocked_by:
                has_unresolved = any(
                    known.get(b, {}).get("status") != "completed" for b in blocked_by
                )
                if has_unresolved:
                    entry["status"] = "blocked"
        tasks.append(entry)
    return tasks


def get_task_stats(tasks: list[dict[str, Any]]) -> dict[str, int]:
    """Compute aggregate status counts from a tasks list.

    Returns a dict with keys: total, completed, in_progress, open, pending,
    blocked, failed.
    """
    stats: dict[str, int] = {
        "total": len(tasks),
        "completed": 0,
        "in_progress": 0,
        "open": 0,
        "pending": 0,
        "blocked": 0,
        "failed": 0,
    }
    for task in tasks:
        status = task.get("status", "open")
        if status in stats:
            stats[status] += 1
    return stats


def is_task_ready(task: dict[str, Any], all_tasks: list[dict[str, Any]]) -> bool:
    """Check if a task is ready (all blockers completed)."""
    if task.get("status") not in ["open", "pending"]:
        return False

    blocked_by = (
        task.get("blocked_by") or task.get("blockedBy") or task.get("depends_on") or []
    )
    if not blocked_by:
        return True

    task_statuses = {t.get("id"): t.get("status") for t in all_tasks}
    return all(task_statuses.get(b) == "completed" for b in blocked_by)


def format_relative_time(seconds: float) -> str:
    """Format seconds into a compact relative-time string like '2m ago'."""
    secs = int(seconds)
    if secs < 60:
        return f"{secs}s ago"
    elif secs < 3600:
        return f"{secs // 60}m ago"
    else:
        return f"{secs // 3600}h ago"


def format_duration(seconds: float) -> str:
    """Format seconds into 'Xm YYs' or 'Xs'."""
    secs = int(seconds)
    if secs < 60:
        return f"{secs}s"
    mins = secs // 60
    remaining = secs % 60
    return f"{mins}m {remaining:02d}s"


def create_activity_summary(lines: list[str]) -> str:
    """Create a condensed summary of activity log entries."""
    agent_changes = 0
    task_updates = 0
    sub_orch_updates = 0
    agents_seen: set[str] = set()
    tasks_seen: set[str] = set()

    for line in lines:
        line = line.strip()
        if not line:
            continue

        if "AGENT " in line:
            agent_changes += 1
            parts = line.split("AGENT ")
            if len(parts) > 1:
                agent_name = parts[1].split(":")[0].strip()
                agents_seen.add(agent_name)

        elif "TASK " in line and "SUB-ORCH" not in line:
            task_updates += 1
            parts = line.split("TASK ")
            if len(parts) > 1:
                task_id = parts[1].split(":")[0].strip()
                tasks_seen.add(task_id)

        elif "SUB-ORCH" in line:
            sub_orch_updates += 1

    summary_parts = []

    if agent_changes:
        agents_list = ", ".join(list(agents_seen)[:5])
        if len(agents_seen) > 5:
            agents_list += "..."
        summary_parts.append(
            f"Agent activity: {agent_changes} state changes across "
            f"{len(agents_seen)} agents ({agents_list})"
        )

    if task_updates:
        summary_parts.append(
            f"Task updates: {task_updates} updates across {len(tasks_seen)} tasks"
        )

    if sub_orch_updates:
        summary_parts.append(f"Sub-orchestrator updates: {sub_orch_updates}")

    if not summary_parts:
        return "No significant activity in compacted period."

    return "\n".join(summary_parts)
