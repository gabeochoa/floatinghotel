#!/usr/bin/env python3
"""
Claw Town Dashboard - Interactive Multi-Agent Orchestration Dashboard

Features:
- Visual panels with box drawing characters
- Real-time agent status detection from tmux pane output
- Interactive: Click agent names to switch to their pane
- Auto-nudge when orchestrator is idle
- Task DAG visualization with dependencies
- Keyboard shortcuts for common actions
"""

from __future__ import annotations

import json
import os
import re
import signal
import subprocess
import sys
import termios
import time
import tty
from datetime import datetime
from pathlib import Path
from typing import Any

# Import data layer (pure logic, no terminal deps)
from dashboard_data import (
    AgentStatus,
    create_activity_summary,
    detect_agent_status,
    detect_human_input_prompt,
    detect_waiting_context,
    format_duration,
    format_relative_time,
    get_task_stats,
    is_task_ready,
    known_tasks_to_list,
    normalize_output_for_comparison,
    OrchestratorStatus,
    sanitize_content,
)

# Import view layer (TUI rendering, colors, layout)
from dashboard_view import (
    ansi_pad,
    cleanup_terminal,
    draw_box,
    draw_progress_bar,
    get_status_display,
    query_cursor_row,
    setup_terminal,
    strip_ansi,
    T,
    truncate,
)

# Import ack tracking functions
try:
    from claw_town_message_queue import (
        get_ack_status,
        record_delivery_ack,
        record_delivery_failure,
        write_sender_feedback,
    )

    ACK_TRACKING_AVAILABLE = True
except ImportError:
    ACK_TRACKING_AVAILABLE = False

# Import unified event log
try:
    from claw_town_events import last_event_of_type, log_event

    EVENT_LOG_AVAILABLE = True
except ImportError:
    EVENT_LOG_AVAILABLE = False


# ═══════════════════════════════════════════════════════════════════════════════
# Tmux Integration
# ═══════════════════════════════════════════════════════════════════════════════


def tmux_capture_pane(target: str, lines: int = 50) -> str:
    """Capture output from a tmux pane."""
    try:
        result = subprocess.run(
            ["tmux", "capture-pane", "-t", target, "-p", "-S", f"-{lines}"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        return result.stdout if result.returncode == 0 else ""
    except Exception:
        return ""


def tmux_list_windows(session: str) -> list[str]:
    """List windows in a tmux session."""
    try:
        result = subprocess.run(
            ["tmux", "list-windows", "-t", session, "-F", "#{window_name}"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode == 0:
            return [w.strip() for w in result.stdout.strip().split("\n") if w.strip()]
    except Exception:
        pass
    return []


def tmux_send_keys(target: str, keys: str) -> bool:
    """Send keys to a tmux pane.

    Uses the 3-step pattern (load-buffer, paste-buffer, send Enter)
    to reliably send long or multi-line prompts. The broken pattern
    of `send-keys "long text" Enter` causes text to sit in the input
    buffer for long prompts.
    """
    import tempfile

    try:
        # Step 1: Write prompt to temp file
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write(keys)
            prompt_file = f.name

        # Step 2: Load into tmux buffer
        result = subprocess.run(
            ["tmux", "load-buffer", prompt_file],
            capture_output=True,
            timeout=5,
        )
        if result.returncode != 0:
            return False

        # Step 3: Paste buffer to target pane
        result = subprocess.run(
            ["tmux", "paste-buffer", "-t", target],
            capture_output=True,
            timeout=5,
        )
        if result.returncode != 0:
            return False

        # Step 4: Send Enter separately to submit
        result = subprocess.run(
            ["tmux", "send-keys", "-t", target, "Enter"],
            capture_output=True,
            timeout=5,
        )

        # Cleanup temp file
        try:
            import os

            os.unlink(prompt_file)
        except Exception:
            pass

        return result.returncode == 0
    except Exception:
        return False


# ═══════════════════════════════════════════════════════════════════════════════
# Interactive Dashboard
# ═══════════════════════════════════════════════════════════════════════════════


class ClawTownDashboard:
    """Interactive multi-agent orchestration dashboard."""

    def __init__(self, project: str):
        self.project = project
        self.tmux_session = f"claw-town-{project}"
        self.orchestrator_pane = (
            f"{self.tmux_session}:control.1"  # Orchestrator pane (center)
        )
        try:
            from claw_town_paths import get_project_dir

            self.project_dir = get_project_dir(project)
        except ImportError:
            self.project_dir = Path.home() / "projects" / project
        self.claw_town_state = self.project_dir / ".claw_town"
        self.tasks_file = self.claw_town_state / "tasks.json"

        # Terminal state
        self.term_width = 80
        self.term_height = 24

        # Monitoring state
        self.last_activity_time = time.time()
        self.last_nudge_time = 0
        self.nudge_interval = 900  # seconds before nudging (15 min)
        self.nudge_count = 0
        self.max_nudges = 10
        self.loop_status = "RUNNING"

        # Per-agent nudge tracking: {agent_name: {last_activity, last_nudge, nudge_count}}
        self.agent_nudge_state: dict[str, dict[str, Any]] = {}
        self.agent_nudge_interval = 900  # same as nudge_interval
        self.agent_max_nudges = 10

        # Load configurable nudge settings from config.json
        self._load_nudge_config()

        # Orchestrator restart tracking
        self.restart_count: int = 0
        self.max_restarts: int = (
            3  # After this many restarts with no progress, stop retrying
        )

        # Per-agent restart tracking: {agent_name: restart_count}
        self.agent_restart_count: dict[str, int] = {}
        self.agent_max_restarts: int = 3

        # Completion tracking
        self.completed_times: dict[str, float] = {}
        self.previous_task_statuses: dict[str, str] = {}  # Track status changes
        self.previous_task_ids: set[str] = set()  # Track new task additions

        # Sticky agent completion state: agents that have signaled TASK_COMPLETE
        # persist here so status stays FREE/completed even after the signal
        # scrolls off the tmux capture buffer.
        self.completed_agents: set[str] = set()

        # Selected agent for keyboard navigation
        self.selected_agent_idx = 0

        # Session tracking
        self.session_id = None

        # Checkpoint tracking
        self.last_checkpoint_time = time.time()
        self.checkpoint_interval = 60  # Create checkpoint every 1 minute
        self.checkpoint_script = Path(__file__).parent / "claw_town_checkpoint.py"
        self.last_task_state_hash = ""

        # Activity log for continuous note-taking
        self.activity_log_file = self.claw_town_state / "activity_log.md"
        self.last_agent_states: dict[str, dict[str, Any]] = {}
        self.last_activity_log_time = time.time()
        self.activity_log_interval = 5  # Log activity every 5 seconds minimum

        # Centralized message broker (outbox)
        self.outbox_dir = self.claw_town_state / "outbox"
        self.outbox_pending = self.outbox_dir / "pending"
        self.outbox_sent = self.outbox_dir / "sent"
        self.outbox_expired = self.outbox_dir / "expired"
        self.outbox_pending.mkdir(parents=True, exist_ok=True)
        self.outbox_sent.mkdir(parents=True, exist_ok=True)
        self.outbox_expired.mkdir(parents=True, exist_ok=True)
        self.outbox_message_ttl = 300  # Expire stuck messages after 5 minutes

        # Clear stale messages from previous session on startup.
        # Old outbox/pending messages would be delivered to the orchestrator
        # immediately, causing a flood of tmux send-keys on restart.
        self._clear_stale_messages_on_startup()

        # Message Queue inbox (for display in inbox daemon)
        self.inbox_dir = self.claw_town_state / "inbox"
        self.inbox_pending = self.inbox_dir / "pending"
        self.inbox_pending.mkdir(parents=True, exist_ok=True)
        self.last_outbox_process_time = time.time()
        self.outbox_process_interval = 0.5  # Process outbox every 0.5 seconds
        self.outbox_lock_file = self.outbox_dir / ".lock"

        # Event-driven learning dedup state (survives dashboard restarts)
        self.learnings_processed: set[str] = set()  # Track processed learning files
        self._learning_dedup_file = self.claw_town_state / "learning_processed.json"
        self._learning_emitted_tasks: set[str] = set()  # Task IDs with emitted events
        self._load_learning_dedup_state()

        # Self-learning (mistake analysis) - runs less frequently
        self.last_self_learn_time = time.time()
        self.self_learn_interval = 300  # Analyze for mistakes every 5 minutes
        self.self_learn_script = Path(__file__).parent / "claw_town_learn.py"

        # Task list scrolling
        self.task_scroll_offset = 0
        self.task_scroll_total = 0  # Updated during render

        # Viewport scrolling (scroll the entire panel view)
        self.viewport_offset = 0
        self._total_content_lines = 0  # Set during render

        # Task sync (runs claw_town_sync.py periodically)
        self.last_task_sync_time = time.time()
        self.task_sync_interval = 30  # Sync every 30 seconds

        # Message navigation state
        self.msg_selected_idx: int = 0  # Index of currently highlighted message
        self.msg_expanded: set = (
            set()
        )  # Set of message indices that are expanded to show full text
        self.focused_panel: str = (
            "tasks"  # Which panel has keyboard focus ('tasks' or 'messages')
        )
        self._needs_rerender: bool = (
            False  # Flag to trigger immediate re-render on keypress
        )
        self.msg_queue_tab: str = (
            "inbox"  # Active subtab: "inbox", "pending", "expired"
        )

        # Task expand/collapse state (click-to-expand task names)
        self.task_expanded: set = set()  # Set of task IDs that are expanded
        self._task_row_map: dict = {}  # Maps screen row -> task_id for click handling

        # Ensure state directory exists
        self.claw_town_state.mkdir(parents=True, exist_ok=True)

    def get_terminal_size(self) -> tuple[int, int]:
        """Get current terminal size."""
        try:
            size = os.get_terminal_size()
            self.term_width = size.columns
            self.term_height = size.lines
        except OSError:
            pass
        return self.term_width, self.term_height

    # _known_tasks_to_list is now known_tasks_to_list() imported from dashboard_data

    def load_state(self) -> dict[str, Any]:
        """Load project state from tasks.json."""
        if self.tasks_file.exists():
            try:
                with open(self.tasks_file) as f:
                    data = json.load(f)
                # Ensure working_dir is always populated
                if "working_dir" not in data:
                    data["working_dir"] = str(self.project_dir)
                # Convert known_tasks dict → tasks list for compat
                if "known_tasks" in data:
                    data["tasks"] = known_tasks_to_list(data)
                return data
            except (json.JSONDecodeError, IOError):
                pass

        return {
            "project": self.project,
            "working_dir": str(self.project_dir),
            "created_at": datetime.now().isoformat(),
            "tasks": [],
            "agents": [],
        }

    def get_mode_status(self) -> tuple[bool, bool, bool]:
        """Check whether skip-permissions, YOLO mode, and parallel mode are active.

        Returns (skip_permissions, yolo_mode, parallel) booleans.
        Reads from config.json which is written by claw_town.sh before
        the dashboard is launched.
        """
        skip_perms = False
        yolo = False
        parallel = False
        config_file = self.claw_town_state / "config.json"
        if config_file.exists():
            try:
                config = json.loads(config_file.read_text())
                skip_perms = config.get("skip_permissions", False)
                yolo = config.get("yolo_mode", False)
                parallel = config.get("parallel", False)
            except Exception:
                pass

        return skip_perms, yolo, parallel

    def _load_nudge_config(self) -> None:
        """Load nudge timing settings from .claw_town/config.json.

        Supported config keys:
          - nudge_interval_seconds: seconds before nudging (applies to both
            orchestrator and agent nudges). Also used as cooldown between
            consecutive nudges. Default: 900 (15 min).
        """
        config_file = self.claw_town_state / "config.json"
        if not config_file.exists():
            return
        try:
            config = json.loads(config_file.read_text())
        except Exception:
            return

        if "nudge_interval_seconds" in config:
            interval = int(config["nudge_interval_seconds"])
            self.nudge_interval = interval
            self.agent_nudge_interval = interval

    def get_task_numbers(self) -> dict[str, str]:
        """Get the task_id -> T-number mapping.

        In the new schema, known_tasks keys are T-numbers directly.
        For backwards compatibility, also checks task_number fields on tasks.
        """
        result: dict[str, str] = {}

        tasks_data = self.load_state()

        # New schema: known_tasks keys are T-numbers
        for t_number in tasks_data.get("known_tasks", {}):
            result[t_number] = t_number

        # Legacy fallback: task_number field on tasks array
        for task in tasks_data.get("tasks", []):
            task_id = task.get("id", "")
            if not task_id:
                continue
            tn = task.get("task_number")
            if tn:
                result[task_id] = tn

        return result

    def get_live_agents(self) -> list[dict[str, Any]]:
        """Get live agent info by reading tmux panes."""
        agents = []
        windows = tmux_list_windows(self.tmux_session)

        # Filter out control window
        agent_windows = [w for w in windows if w != "control"]

        for window in agent_windows:
            output = tmux_capture_pane(f"{self.tmux_session}:{window}", 100)
            status, status_detail = detect_agent_status(output)

            # Record newly completed agents for sticky state
            if status == AgentStatus.COMPLETED and status_detail == "completed":
                self.completed_agents.add(window)

            # Sticky completion: if this agent previously signaled TASK_COMPLETE,
            # keep it as COMPLETED regardless of what the buffer shows now.
            if window in self.completed_agents:
                status = AgentStatus.COMPLETED
                status_detail = "completed"

            agents.append(
                {
                    "name": window,
                    "window": window,
                    "status": status,
                    "status_detail": status_detail,
                    "last_output": output[-500:] if output else "",
                }
            )

        return agents

    # get_task_stats and is_task_ready are now imported from dashboard_data

    # ─────────────────────────────────────────────────────────────────────────
    # Panel Renderers
    # ─────────────────────────────────────────────────────────────────────────

    def render_mission_panel(
        self, tasks: list[dict], agents: list[dict], width: int
    ) -> list[str]:
        """Render the Mission panel."""
        content = []
        stats = get_task_stats(tasks)

        # Progress bar
        progress = draw_progress_bar(stats["completed"], stats["total"], width - 15)
        content.append(f" {progress}")
        content.append("")

        # Legend with counts (always show all states)
        legend_parts = [
            f"{T.YELLOW}{T.CIRCLE_FILLED}{T.RESET} Active {stats['in_progress']}",
            f"{T.GREEN}{T.CHECK}{T.RESET} Done {stats['completed']}",
            f"{T.CYAN}{T.CIRCLE_DOT}{T.RESET} Ready {stats['open'] + stats['pending']}",
            f"{T.RED}{T.CIRCLE}{T.RESET} Blocked {stats['blocked']}",
            f"{T.RED}{T.CROSS}{T.RESET} Failed {stats.get('failed', 0)}",
        ]

        if legend_parts:
            content.append(" " + "  ".join(legend_parts))
        content.append("")

        # Get task numbers mapping for T-number display
        task_numbers = self.get_task_numbers()

        # Active tasks (in_progress)
        in_progress = [t for t in tasks if t.get("status") == "in_progress"]
        if in_progress:
            content.append(f" {T.BOLD}Active ({len(in_progress)}){T.RESET}")
            for task in in_progress:
                owner = task.get("owner", task.get("assigned_to", "unassigned"))
                task_id = task.get("id", "")
                t_number = task_numbers.get(task_id, "")
                t_prefix = f"{T.MAGENTA}{t_number}{T.RESET} " if t_number else ""
                title = truncate(
                    task.get("title") or task.get("name") or task_id,
                    width - 8 - len(t_number),
                )
                content.append(
                    f"   {T.YELLOW}{T.CIRCLE_FILLED}{T.RESET} {t_prefix}{title}"
                )
                content.append(f"     {T.DIM}{T.ARROW_R} {owner}{T.RESET}")
            content.append("")

        # Recently completed
        completed = [t for t in tasks if t.get("status") == "completed"]
        for t in completed:
            tid = t.get("id", "")
            if tid and tid not in self.completed_times:
                self.completed_times[tid] = time.time()

        if completed:
            recent = sorted(
                completed,
                key=lambda t: self.completed_times.get(t.get("id", ""), 0),
                reverse=True,
            )[:3]
            content.append(f" {T.BOLD}Recently Completed{T.RESET}")
            for task in recent:
                task_id = task.get("id", "")
                t_number = task_numbers.get(task_id, "")
                t_prefix = f"{T.MAGENTA}{t_number}{T.RESET} " if t_number else ""
                title = truncate(
                    task.get("title") or task.get("name") or task_id,
                    width - 10 - len(t_number),
                )
                ago = int(time.time() - self.completed_times.get(task_id, time.time()))
                ago_str = f"{ago}s" if ago < 60 else f"{ago // 60}m"
                content.append(
                    f"   {T.GREEN}{T.CHECK}{T.RESET} {t_prefix}{title} {T.DIM}({ago_str}){T.RESET}"
                )
            content.append("")

        # Ready tasks (unclaimed)
        ready = [
            t
            for t in tasks
            if is_task_ready(t, tasks)
            and not t.get("owner")
            and not t.get("assigned_to")
        ]
        if ready:
            content.append(f" {T.BOLD}Unclaimed Ready ({len(ready)}){T.RESET}")
            for task in ready:
                task_id = task.get("id", "")
                t_number = task_numbers.get(task_id, "")
                t_prefix = f"{T.MAGENTA}{t_number}{T.RESET} " if t_number else ""
                title = truncate(
                    task.get("title") or task.get("name") or task_id,
                    width - 8 - len(t_number),
                )
                content.append(f"   {T.CYAN}{T.CIRCLE_DOT}{T.RESET} {t_prefix}{title}")
            content.append("")

        # Blocked tasks - show what's blocking them
        # Only include tasks with status "blocked" (set by known_tasks_to_list
        # when unresolved blockers exist). Do NOT include "pending" tasks that
        # happen to have blocked_by entries — those may have all blockers
        # resolved and would otherwise appear in BOTH blocked and unclaimed.
        blocked = [t for t in tasks if t.get("status") == "blocked"]
        if blocked:
            content.append(f" {T.BOLD}{T.RED}Blocked ({len(blocked)}){T.RESET}")
            completed_ids = {
                t.get("id") for t in tasks if t.get("status") == "completed"
            }
            for task in blocked:
                task_id = task.get("id", "?")
                title = truncate(
                    task.get("title") or task.get("name") or task_id, width - 10
                )
                deps = task.get("blocked_by", task.get("blockedBy", []))
                # Show only unresolved blockers
                unresolved = [d for d in deps if d not in completed_ids][:2]
                content.append(f"   {T.RED}{T.CIRCLE}{T.RESET} {title}")
                if unresolved:
                    blockers = ", ".join(truncate(b, 12) for b in unresolved)
                    content.append(f"     {T.DIM}{T.ARROW_L} {blockers}{T.RESET}")

        return content

    def render_agents_panel(self, agents: list[dict], width: int) -> list[str]:
        """Render the CLI Agents panel with tree structure."""
        content = []

        if not agents:
            content.append(f" {T.DIM}No agents running{T.RESET}")
            content.append(f" {T.DIM}Spawn agents to start work{T.RESET}")
            return content

        # Count by status - now with differentiated input sources
        working = [a for a in agents if a["status"] == AgentStatus.WORKING]
        needs_human = [a for a in agents if a["status"] == AgentStatus.NEEDS_HUMAN]
        needs_orch = [
            a for a in agents if a["status"] == AgentStatus.NEEDS_ORCHESTRATOR
        ]
        needs_agent = [a for a in agents if a["status"] == AgentStatus.NEEDS_AGENT]
        sleeping = [a for a in agents if a["status"] == AgentStatus.SLEEPING]
        free = [a for a in agents if a["status"] == AgentStatus.FREE]
        completed = [a for a in agents if a["status"] == AgentStatus.COMPLETED]
        # Fallback for generic needs_input
        needs_input = [
            a
            for a in agents
            if a["status"] == AgentStatus.NEEDS_INPUT
            and a not in needs_human + needs_orch + needs_agent
        ]

        content.append(f" {T.BOLD}{len(agents)} agent(s){T.RESET}")
        content.append("")

        def render_agent_list(
            agent_list: list[dict], label: str, color: str, show_detail: bool = True
        ) -> None:
            if not agent_list:
                return
            if content and content[-1] != "":
                content.append("")
            content.append(f" {color}{label} ({len(agent_list)}){T.RESET}")
            for i, agent in enumerate(agent_list):
                prefix = T.TREE_L if i == len(agent_list) - 1 else T.TREE_T
                name = truncate(agent["name"], width - 18)
                icon, _ = get_status_display(agent["status"])
                detail = agent.get("status_detail", "")
                if show_detail and detail:
                    content.append(
                        f"   {prefix}{T.TREE_H} {icon}{name} {T.DIM}({detail}){T.RESET}"
                    )
                else:
                    content.append(f"   {prefix}{T.TREE_H} {icon}{name}")

        # Render each group in priority order
        render_agent_list(working, "Working", T.YELLOW, show_detail=False)
        render_agent_list(needs_human, "Needs Human", T.MAGENTA)
        render_agent_list(needs_orch, "Needs Orchestrator", T.CYAN)
        render_agent_list(needs_agent, "Blocked on Agent", T.RED)
        render_agent_list(needs_input, "Needs Input", T.MAGENTA)
        render_agent_list(sleeping, "Sleeping/Daemon", T.GRAY)
        render_agent_list(free, "Free", T.GREEN, show_detail=False)
        render_agent_list(completed, "Completed", T.GREEN, show_detail=False)

        return content

    def get_orchestrator_status(self) -> tuple[str, str, str, list[str]]:
        """Get orchestrator status by checking its pane output and agent states.

        Returns: (status, status_color, detail, waiting_on_entities)
        """
        output = tmux_capture_pane(self.orchestrator_pane, 50)

        if not output:
            return OrchestratorStatus.UNKNOWN, T.GRAY, "Cannot read pane", []

        # Check for busy indicators (orchestrator is thinking/working)
        busy_patterns = [
            "esc to interrupt",
            "press esc to interrupt",
            "✻",
            "⠋",
            "⠙",
            "⠹",
            "⠸",  # Spinners
            "Thinking...",
            "Running:",
        ]
        lower_output = output.lower()
        for pattern in busy_patterns:
            if pattern.lower() in lower_output:
                return OrchestratorStatus.WORKING, T.GREEN, "Processing...", []

        # Get current agents and sub-orchestrators
        agents = self.get_live_agents()
        state = self.load_state()
        sub_orchestrators = state.get("sub_orchestrators", [])

        # Count working agents
        working_agents = [a for a in agents if a["status"] == AgentStatus.WORKING]
        working_agent_names = [a["name"] for a in working_agents]

        # Check sub-orchestrator states
        working_sub_orchs = []
        for sub in sub_orchestrators:
            sub_id = sub.get("id", "unknown")
            sub_window = f"[{sub_id}] orch"
            sub_output = tmux_capture_pane(f"{self.tmux_session}:{sub_window}", 30)
            if sub_output:
                sub_status = detect_agent_status(sub_output)
                if sub_status == AgentStatus.WORKING:
                    working_sub_orchs.append(sub_id)

        # Determine status based on context
        if working_sub_orchs:
            # Waiting on sub-orchestrators
            detail = f"Sub-orch: {', '.join(working_sub_orchs[:2])}"
            if len(working_sub_orchs) > 2:
                detail += f" +{len(working_sub_orchs) - 2}"
            return (
                OrchestratorStatus.WAITING_FOR_SUB_ORCH,
                T.CYAN,
                detail,
                working_sub_orchs,
            )

        if working_agents:
            # Waiting on agents
            detail = f"Agents: {', '.join(working_agent_names[:2])}"
            if len(working_agents) > 2:
                detail += f" +{len(working_agents) - 2}"
            return (
                OrchestratorStatus.WAITING_FOR_AGENT,
                T.BLUE,
                detail,
                working_agent_names,
            )

        # Check if showing human input prompt
        if detect_human_input_prompt(output):
            return (
                OrchestratorStatus.WAITING_FOR_HUMAN,
                T.YELLOW,
                "Needs your input",
                [],
            )

        # Check for waiting context in output
        is_waiting, entities = detect_waiting_context(output)
        if is_waiting:
            if entities:
                return (
                    OrchestratorStatus.WAITING_FOR_AGENT,
                    T.BLUE,
                    f"Waiting: {', '.join(entities[:2])}",
                    entities,
                )
            return OrchestratorStatus.WAITING_FOR_AGENT, T.BLUE, "Waiting on work", []

        # Default: idle (not actively working, but not specifically waiting for human input)
        # Only show WAITING_FOR_HUMAN when we've positively detected an input prompt
        return OrchestratorStatus.IDLE, T.GRAY, "Idle", []

    # ------------------------------------------------------------------
    # Helpers for render_orchestrator_panel
    # ------------------------------------------------------------------
    # _format_relative_time and _format_duration are now format_relative_time()
    # and format_duration() imported from dashboard_data

    def render_orchestrator_panel(
        self, agents: list[dict], tasks: list[dict], width: int
    ) -> list[str]:
        """Render the unified Orchestrator panel (combines orchestrator status + loop info)."""
        content: list[str] = []

        status, _status_color, _detail, _waiting_on = self.get_orchestrator_status()
        idle_time = int(time.time() - self.last_activity_time)

        # --- Status display mapping ----------------------------------------
        status_display = {
            OrchestratorStatus.WORKING: (T.ICON_WORKING, "WORKING", T.GREEN),
            OrchestratorStatus.WAITING_FOR_HUMAN: (
                T.ICON_WAITING_HUMAN,
                "NEEDS INPUT",
                T.YELLOW,
            ),
            OrchestratorStatus.WAITING_FOR_AGENT: (
                T.ICON_WAITING_AGENT,
                "WAITING",
                T.BLUE,
            ),
            OrchestratorStatus.WAITING_FOR_SUB_ORCH: (
                T.ICON_WAITING_SUB,
                "WAITING",
                T.CYAN,
            ),
            OrchestratorStatus.IDLE: (T.ICON_IDLE, "IDLE", T.GRAY),
            OrchestratorStatus.UNKNOWN: ("? ", "UNKNOWN", T.GRAY),
        }

        icon, label, color = status_display.get(
            status, ("? ", status.upper(), _status_color)
        )

        # --- Line 1: Status · idle time · nudge info ----------------------
        parts = [f" {icon} {color}{T.BOLD}{label}{T.RESET}"]
        parts.append(f"{T.DIM} {T.BULLET} {format_duration(idle_time)}{T.RESET}")

        # Nudge / restart info on same line when non-zero
        if self.nudge_count >= self.max_nudges:
            # Stall state — handled below as a prominent block
            pass
        elif self.nudge_count > 0:
            parts.append(
                f"{T.YELLOW} {T.BULLET} Nudged {self.nudge_count}/{self.max_nudges}{T.RESET}"
            )
        elif self.restart_count > 0:
            parts.append(
                f"{T.YELLOW} {T.BULLET} Restarted {self.restart_count}/{self.max_restarts}{T.RESET}"
            )
        else:
            time_to_nudge = max(0, self.nudge_interval - idle_time)
            if time_to_nudge > 0 and idle_time > 30:
                parts.append(f"{T.DIM} {T.BULLET} Nudge in {time_to_nudge}s{T.RESET}")

        content.append("".join(parts))

        # --- Separator -----------------------------------------------------
        sep_width = max(width - 4, 20)
        content.append(f" {T.DIM}{T.BOX_H * sep_width}{T.RESET}")

        # --- Hook table: type / last-fired / schedule ----------------------
        now = time.time()

        # Gather last-event data from events.jsonl (graceful when unavailable)
        # event_types can be a single string or a list; when a list, the most
        # recent event across all types is used (fixes nudge timing: we need
        # the latest of nudge_orch AND nudge_agent).
        hook_defs: list[tuple[str, str | list[str], str, str]] = [
            # (display_label, event_type(s), schedule_text, icon)
            ("Learn", "learn", "on task completion", T.CHECK),
            ("Nudge", ["nudge_orch", "nudge_agent"], "after 15m idle", T.CHECK),
            ("Task Sync", "task_sync", "every 30s", T.CHECK),
        ]

        for h_label, h_event, h_schedule, h_icon in hook_defs:
            last_event = None
            if EVENT_LOG_AVAILABLE:
                try:
                    event_types = h_event if isinstance(h_event, list) else [h_event]
                    for et in event_types:
                        candidate = last_event_of_type(self.project, et)
                        if candidate and candidate.get("ts"):
                            if last_event is None or candidate["ts"] > last_event["ts"]:
                                last_event = candidate
                except Exception:
                    pass

            if last_event and last_event.get("ts"):
                try:
                    ev_dt = datetime.fromisoformat(last_event["ts"])
                    elapsed = now - ev_dt.timestamp()
                    time_str = (
                        f"{T.GREEN}{h_icon}{T.RESET} {format_relative_time(elapsed)}"
                    )
                except Exception:
                    time_str = f"{T.GREEN}{h_icon}{T.RESET} fired"
            else:
                time_str = f"{T.DIM}-{T.RESET} {T.DIM}{format_relative_time(now - self.last_activity_time)} (no fire){T.RESET}"

            # Pad for alignment: label (14) + time (18 visible) + schedule (rest)
            content.append(
                f" {T.BOLD}{h_label:<14}{T.RESET}"
                f" {ansi_pad(time_str, 18)}"
                f" {T.DIM}({h_schedule}){T.RESET}"
            )

        # --- Stall warning (prominent red block) ---------------------------
        if self.nudge_count >= self.max_nudges:
            content.append("")
            if self.restart_count >= self.max_restarts:
                content.append(f" {T.RED}{T.BOLD}STALLED - UNRESPONSIVE{T.RESET}")
                content.append(f" {T.RED}Max restarts exceeded.{T.RESET}")
                content.append(f" {T.RED}Manual intervention needed.{T.RESET}")
            else:
                content.append(f" {T.RED}{T.BOLD}STALLED - RESTARTING{T.RESET}")
                content.append(f" {T.RED}Nudges exhausted, restart imminent{T.RESET}")

        return content

    def get_sub_orchestrator_details(self) -> list[dict]:
        """Get detailed status for all sub-orchestrators.

        Returns list of dicts with sub-orchestrator info including:
        - id, status, task_counts, working_agents, etc.
        """
        state = self.load_state()
        sub_orchestrators = state.get("sub_orchestrators", [])
        results = []

        for sub in sub_orchestrators:
            sub_id = sub.get("id", "unknown")
            sub_tasks_path = Path(sub.get("tasks_path", ""))
            sub_window = f"[{sub_id}] orch"

            # Check if sub-orchestrator window exists and get its status
            sub_output = tmux_capture_pane(f"{self.tmux_session}:{sub_window}", 30)

            if not sub_output:
                status = "offline"
                status_color = T.RED
            else:
                agent_status = detect_agent_status(sub_output)
                if agent_status == AgentStatus.WORKING:
                    status = "working"
                    status_color = T.GREEN
                elif detect_human_input_prompt(sub_output):
                    status = "needs_input"
                    status_color = T.YELLOW
                else:
                    status = "idle"
                    status_color = T.GRAY

            # Load sub-orchestrator tasks
            sub_tasks = []
            if sub_tasks_path.exists():
                try:
                    with open(sub_tasks_path) as f:
                        sub_state = json.load(f)
                    sub_tasks = known_tasks_to_list(sub_state)
                except (json.JSONDecodeError, IOError):
                    pass

            # Calculate task stats
            completed = sum(1 for t in sub_tasks if t.get("status") == "completed")
            in_progress = sum(1 for t in sub_tasks if t.get("status") == "in_progress")
            total = len(sub_tasks)

            # Find agents belonging to this sub-orchestrator
            sub_agents = []
            windows = tmux_list_windows(self.tmux_session)
            for window in windows:
                if window.startswith(f"[{sub_id}]") and not window.endswith("orch"):
                    agent_output = tmux_capture_pane(
                        f"{self.tmux_session}:{window}", 20
                    )
                    agent_status = (
                        detect_agent_status(agent_output)
                        if agent_output
                        else AgentStatus.UNKNOWN
                    )
                    agent_name = window.replace(f"[{sub_id}] ", "")
                    sub_agents.append({"name": agent_name, "status": agent_status})

            results.append(
                {
                    "id": sub_id,
                    "status": status,
                    "status_color": status_color,
                    "tasks_total": total,
                    "tasks_completed": completed,
                    "tasks_in_progress": in_progress,
                    "agents": sub_agents,
                    "working_dir": sub.get("working_dir", ""),
                    "window": sub_window,
                }
            )

        return results

    def render_sub_orchestrators_panel(self, width: int) -> list[str]:
        """Render the Sub-Orchestrators panel with navigation details."""
        content = []

        sub_orchs = self.get_sub_orchestrator_details()

        if not sub_orchs:
            content.append(f" {T.DIM}No sub-orchestrators{T.RESET}")
            return content

        content.append(f" {T.BOLD}{len(sub_orchs)} sub-orchestrator(s){T.RESET}")
        content.append("")

        for i, sub in enumerate(sub_orchs):
            is_last = i == len(sub_orchs) - 1
            prefix = T.TREE_L if is_last else T.TREE_T

            # Status icon based on state
            if sub["status"] == "working":
                icon = T.ICON_WORKING
            elif sub["status"] == "needs_input":
                icon = T.ICON_WAITING_HUMAN
            elif sub["status"] == "offline":
                icon = T.ICON_ERROR
            else:
                icon = T.ICON_IDLE

            # Sub-orchestrator name and status
            name = truncate(sub["id"], width - 20)
            content.append(
                f" {prefix}{T.TREE_H} {icon} {sub['status_color']}{T.BOLD}{name}{T.RESET}"
            )

            # Progress line
            progress_prefix = "  " if is_last else f" {T.TREE_V}"
            total = sub["tasks_total"]
            completed = sub["tasks_completed"]
            in_prog = sub["tasks_in_progress"]

            if total > 0:
                pct = int((completed / total) * 100)
                content.append(
                    f"{progress_prefix}  {T.DIM}Tasks: {completed}/{total} ({pct}%) "
                    f"| {in_prog} active{T.RESET}"
                )
            else:
                content.append(f"{progress_prefix}  {T.DIM}No tasks yet{T.RESET}")

            # Show agents if any
            if sub["agents"]:
                working_count = sum(
                    1 for a in sub["agents"] if a["status"] == AgentStatus.WORKING
                )
                content.append(
                    f"{progress_prefix}  {T.DIM}Agents: {len(sub['agents'])} "
                    f"({working_count} working){T.RESET}"
                )

            # Navigation hint
            content.append(
                f"{progress_prefix}  {T.CYAN}→ claw-town attach {self.project}:{sub['id']}{T.RESET}"
            )

            if not is_last:
                content.append("")

        return content

    def check_and_run_task_sync(self) -> None:
        """Run task sync periodically to discover new/changed tasks."""
        now = time.time()
        if now - self.last_task_sync_time < self.task_sync_interval:
            return
        self.last_task_sync_time = now

        try:
            sync_script = Path(__file__).parent / "claw_town_sync.py"
            subprocess.run(
                ["python3", str(sync_script), "sync", "--project", self.project],
                capture_output=True,
                text=True,
                timeout=25,  # Must be less than the 30s interval
                cwd=str(self.project_dir),
            )
            # claw_town_sync.py already logs a clean summary event when
            # changes are detected — no need to duplicate here.
        except subprocess.TimeoutExpired:
            pass
        except Exception:
            pass

    def check_and_create_checkpoint(
        self, tasks: list[dict], force: bool = False
    ) -> bool:
        """Check if a checkpoint should be created and create it if needed.

        Creates checkpoints:
        - Every checkpoint_interval seconds (default 1 minute)
        - When task states change significantly
        - When force=True (e.g., at shutdown)

        Also syncs to Task Graph when checkpoint is created.

        Returns True if a checkpoint was created.
        """
        if not self.checkpoint_script.exists():
            return False

        now = time.time()

        # Create a hash of current task states to detect changes
        task_state = "|".join(
            f"{t.get('id')}:{t.get('status')}"
            for t in sorted(tasks, key=lambda x: x.get("id", ""))
        )

        state_changed = task_state != self.last_task_state_hash
        time_elapsed = now - self.last_checkpoint_time >= self.checkpoint_interval

        # Create checkpoint if state changed significantly or time elapsed or forced
        if force or state_changed or time_elapsed:
            trigger = (
                "shutdown"
                if force
                else ("state_change" if state_changed else "periodic")
            )

            try:
                # Run checkpoint script and capture any errors
                result = subprocess.run(
                    [
                        "python3",
                        str(self.checkpoint_script),
                        self.project,
                        "create",
                        "--trigger",
                        trigger,
                    ],
                    capture_output=True,
                    text=True,
                    timeout=30,
                )

                self.last_checkpoint_time = now
                self.last_task_state_hash = task_state

                if result.returncode == 0:
                    # Log successful checkpoint
                    timestamp = datetime.now().strftime("%H:%M:%S")
                    self._log_activity(
                        f"[{timestamp}] Checkpoint created (trigger: {trigger})"
                    )
                    return True
                else:
                    # Log checkpoint failure
                    error_msg = (
                        result.stderr[:100] if result.stderr else "unknown error"
                    )
                    self._log_activity(f"Checkpoint failed: {error_msg}")
                    return False

            except subprocess.TimeoutExpired:
                self._log_activity("Checkpoint timed out")
                return False
            except Exception as e:
                self._log_activity(f"Checkpoint error: {e}")
                return False

        return False

    # ═══════════════════════════════════════════════════════════════════════════
    # Centralized Message Broker (Outbox)
    # ═══════════════════════════════════════════════════════════════════════════

    def queue_message(
        self,
        target: str,
        content: str,
        priority: int = 3,
        source: str = "event",
        target_agent: str | None = None,
    ) -> bool:
        """Queue a message for delivery to a target pane.

        Priority levels:
        1 = Critical (human input, errors)
        2 = High (orchestrator commands)
        3 = Normal (task assignments)
        4 = Low (nudges)
        5 = Background (status updates)

        target_agent: If set, auto_nudge.sh routes to this agent's tmux window.
        """
        try:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
            msg_file = self.outbox_pending / f"{priority}_{timestamp}_{source}.json"

            msg_data = {
                "target": target,
                "content": content,
                "priority": priority,
                "source": source,
                "timestamp": datetime.now().isoformat(),
                "queued_at": time.time(),
            }
            if target_agent:
                msg_data["target_agent"] = target_agent

            msg_file.write_text(json.dumps(msg_data, indent=2))
            return True
        except Exception as e:
            self._log_activity(
                f"[ERROR] Failed to queue message for {target} from {source}: {e}"
            )
            return False

    def process_outbox(self) -> int:
        """Process pending messages in the outbox queue.

        Returns number of messages processed.
        """
        now = time.time()
        if now - self.last_outbox_process_time < self.outbox_process_interval:
            return 0

        self.last_outbox_process_time = now

        # Get pending messages sorted by filename (priority_timestamp_source.json)
        try:
            pending = sorted(self.outbox_pending.glob("*.json"))
        except Exception as e:
            self._log_activity(f"[ERROR] Failed to list pending outbox messages: {e}")
            return 0

        if not pending:
            return 0

        # Expire stuck messages before processing
        self._expire_stuck_messages(pending, now)

        # Re-list after expiration (some may have been moved)
        try:
            pending = sorted(self.outbox_pending.glob("*.json"))
        except Exception:
            return 0

        if not pending:
            return 0

        # Acquire lock to prevent concurrent sends
        if not self._acquire_outbox_lock():
            return 0

        processed = 0
        try:
            # Process one message per tick to avoid blocking
            msg_file = pending[0]
            try:
                msg_data = json.loads(msg_file.read_text())
                target = msg_data.get("target", "")
                content = msg_data.get("content", "")
                source = msg_data.get("source", "unknown")
                queued_at = msg_data.get("queued_at", 0)

                if target and content:
                    # Send the message atomically
                    success = self._send_message_atomic(target, content)

                    if success:
                        # Move to sent directory
                        sent_path = self.outbox_sent / msg_file.name
                        msg_file.rename(sent_path)
                        processed = 1

                        # Log the delivery
                        self._log_activity(
                            f"Message delivered: {source} -> {target} "
                            f"({len(content)} chars)"
                        )

                        # Record delivery ack
                        if ACK_TRACKING_AVAILABLE:
                            record_delivery_ack(
                                project=self.project,
                                msg_filename=msg_file.name,
                                source=source,
                                target=target,
                                priority=msg_data.get("priority", 3),
                                queued_at=queued_at,
                                content_preview=content[:100],
                            )
                            write_sender_feedback(
                                project=self.project,
                                sender=source,
                                msg_preview=content[:80],
                                status="delivered",
                            )
                    else:
                        # Failed to send - log the error with details
                        age = now - queued_at if queued_at else 0
                        self._log_activity(
                            "[ERROR] Message delivery failed: "
                            f"source={source} target={target} "
                            f"priority={msg_data.get('priority', '?')} "
                            f"age={age:.0f}s file={msg_file.name}"
                        )
                else:
                    # Invalid message - move to expired and log
                    self._log_activity(
                        "[WARN] Invalid message removed (empty target/content): "
                        f"file={msg_file.name} source={source}"
                    )
                    expired_path = self.outbox_expired / msg_file.name
                    try:
                        msg_file.rename(expired_path)
                    except Exception:
                        msg_file.unlink(missing_ok=True)
            except json.JSONDecodeError as e:
                # Corrupted JSON - move to expired with logging
                self._log_activity(
                    "[ERROR] Corrupted message (bad JSON), moved to expired: "
                    f"file={msg_file.name} error={e}"
                )
                try:
                    expired_path = self.outbox_expired / msg_file.name
                    msg_file.rename(expired_path)
                except Exception:
                    msg_file.unlink(missing_ok=True)
            except Exception as e:
                # Other read/processing error - log but leave for retry
                self._log_activity(
                    f"[ERROR] Failed to process message: file={msg_file.name} error={e}"
                )
        finally:
            self._release_outbox_lock()

        return processed

    def _expire_stuck_messages(self, pending: list, now: float) -> None:
        """Move messages older than TTL from pending to expired.

        Args:
            pending: List of pending message file paths
            now: Current timestamp
        """
        for msg_file in pending:
            try:
                msg_data = json.loads(msg_file.read_text())
                queued_at = msg_data.get("queued_at", 0)
                age = now - queued_at if queued_at else now - msg_file.stat().st_mtime

                if age > self.outbox_message_ttl:
                    source = msg_data.get("source", "unknown")
                    target = msg_data.get("target", "unknown")
                    priority = msg_data.get("priority", "?")

                    self._log_activity(
                        f"[EXPIRED] Message expired after {age:.0f}s: "
                        f"source={source} target={target} "
                        f"priority={priority} file={msg_file.name}"
                    )

                    expired_path = self.outbox_expired / msg_file.name
                    msg_file.rename(expired_path)

                    # Record expiration in ack tracking
                    if ACK_TRACKING_AVAILABLE:
                        record_delivery_failure(
                            project=self.project,
                            msg_filename=msg_file.name,
                            source=source,
                            target=target,
                            reason=f"expired_after_{int(age)}s",
                        )
                        write_sender_feedback(
                            project=self.project,
                            sender=source,
                            msg_preview=msg_data.get("content", "")[:80],
                            status="expired",
                        )
            except Exception as e:
                self._log_activity(
                    "[ERROR] Failed to check/expire message: "
                    f"file={msg_file.name} error={e}"
                )

    # _sanitize_content is now sanitize_content() imported from dashboard_data

    def _is_pane_idle(
        self, target: str, max_retries: int = 5, retry_delay: float = 2.0
    ) -> bool:
        """Check if a tmux pane's input line is empty (idle prompt).

        Captures the last line of the pane and checks if it looks like
        a bare prompt (e.g., '> ', '$ ', '% ') with no user input after it.
        Retries up to max_retries times if the pane appears busy.

        Returns True if pane is idle (safe to paste), False if still busy.
        """
        for attempt in range(max_retries):
            try:
                result = subprocess.run(
                    ["tmux", "capture-pane", "-t", target, "-p", "-S", "-1"],
                    capture_output=True,
                    timeout=5,
                )
                if result.returncode != 0:
                    return True  # Can't check — proceed anyway

                last_line = result.stdout.decode("utf-8", errors="replace").strip()

                # Idle if empty or ends with a bare prompt
                if not last_line:
                    return True
                if last_line in (">", "$ ", "% ", "❯", "→"):
                    return True
                # Claude Code prompt: just ">" or "> " at end of line
                if last_line.endswith("> ") or last_line == ">":
                    return True
                # Bash prompt patterns
                if last_line.rstrip().endswith("$") or last_line.rstrip().endswith("%"):
                    return True

                if attempt < max_retries - 1:
                    time.sleep(retry_delay)
            except Exception:
                return True  # Can't check — proceed anyway

        self._log_activity(
            f"[WARN] Pane {target} not idle after {max_retries} retries, sending anyway"
        )
        return True  # Send anyway after retries exhausted

    def _send_message_atomic(self, target: str, content: str) -> bool:
        """Send a message to a tmux pane atomically (paste + Enter together).

        Waits for the pane to be idle before pasting to avoid corrupting
        any existing input in the buffer. Uses a unique buffer name to
        prevent conflicts with other senders.
        """
        import tempfile
        import uuid

        prompt_file = None
        try:
            # Sanitize content to prevent hook parsing errors
            content = sanitize_content(content)

            # Wait for idle prompt before pasting
            self._is_pane_idle(target)

            # Use unique buffer name to prevent race conditions
            buffer_name = f"claw_town_{uuid.uuid4().hex[:8]}"

            # Step 1: Write content to temp file
            with tempfile.NamedTemporaryFile(
                mode="w", suffix=".txt", delete=False
            ) as f:
                f.write(content)
                prompt_file = f.name

            # Step 2: Load into named buffer
            result = subprocess.run(
                ["tmux", "load-buffer", "-b", buffer_name, prompt_file],
                capture_output=True,
                timeout=5,
            )
            if result.returncode != 0:
                stderr = result.stderr.decode("utf-8", errors="replace").strip()
                self._log_activity(
                    f"[ERROR] tmux load-buffer failed for {target}: {stderr}"
                )
                return False

            # Step 3: Paste from named buffer
            result = subprocess.run(
                ["tmux", "paste-buffer", "-b", buffer_name, "-t", target],
                capture_output=True,
                timeout=5,
            )
            if result.returncode != 0:
                stderr = result.stderr.decode("utf-8", errors="replace").strip()
                self._log_activity(
                    f"[ERROR] tmux paste-buffer failed for {target}: {stderr}"
                )
                # Clean up buffer on failure
                subprocess.run(
                    ["tmux", "delete-buffer", "-b", buffer_name],
                    capture_output=True,
                    timeout=2,
                )
                return False

            # Step 4: Send Enter immediately (no delay)
            result = subprocess.run(
                ["tmux", "send-keys", "-t", target, "Enter"],
                capture_output=True,
                timeout=5,
            )

            # Clean up buffer and temp file
            subprocess.run(
                ["tmux", "delete-buffer", "-b", buffer_name],
                capture_output=True,
                timeout=2,
            )
            try:
                import os

                os.unlink(prompt_file)
            except Exception:
                pass

            if result.returncode != 0:
                stderr = result.stderr.decode("utf-8", errors="replace").strip()
                self._log_activity(
                    f"[ERROR] tmux send-keys Enter failed for {target}: {stderr}"
                )
            return result.returncode == 0
        except subprocess.TimeoutExpired:
            self._log_activity(f"[ERROR] tmux command timed out sending to {target}")
            return False
        except Exception as e:
            self._log_activity(f"[ERROR] _send_message_atomic failed for {target}: {e}")
            return False
        finally:
            if prompt_file:
                try:
                    import os

                    os.unlink(prompt_file)
                except Exception:
                    pass

    def _acquire_outbox_lock(self) -> bool:
        """Acquire exclusive lock for outbox processing."""
        try:
            if self.outbox_lock_file.exists():
                # Check if lock is stale (older than 10 seconds)
                lock_age = time.time() - self.outbox_lock_file.stat().st_mtime
                if lock_age < 10:
                    return False
                # Stale lock - remove it
                self._log_activity(
                    f"[WARN] Removing stale outbox lock (age={lock_age:.0f}s)"
                )
                self.outbox_lock_file.unlink()

            # Create lock file
            self.outbox_lock_file.write_text(str(os.getpid()))
            return True
        except Exception as e:
            self._log_activity(f"[ERROR] Failed to acquire outbox lock: {e}")
            return False

    def _release_outbox_lock(self) -> None:
        """Release outbox lock."""
        try:
            if self.outbox_lock_file.exists():
                self.outbox_lock_file.unlink()
        except Exception as e:
            self._log_activity(f"[ERROR] Failed to release outbox lock: {e}")

    def _clear_stale_messages_on_startup(self) -> None:
        """Move all pending outbox and inbox messages to expired/processed on startup.

        Old messages from a previous session are never relevant to a new session.
        Without this, restarting a claw-town project causes a flood of stale
        messages being delivered to the orchestrator via tmux paste-buffer.
        """
        cleared = 0
        # Clear stale outbox/pending
        try:
            for msg_file in list(self.outbox_pending.glob("*.json")):
                try:
                    expired_path = self.outbox_expired / msg_file.name
                    msg_file.rename(expired_path)
                    cleared += 1
                except Exception:
                    msg_file.unlink(missing_ok=True)
                    cleared += 1
        except Exception:
            pass

        # Clear stale inbox/pending
        try:
            processed_dir = self.inbox_dir / "processed"
            processed_dir.mkdir(parents=True, exist_ok=True)
            for msg_file in list(self.inbox_pending.glob("*.msg")):
                try:
                    processed_path = processed_dir / msg_file.name
                    msg_file.rename(processed_path)
                    cleared += 1
                except Exception:
                    msg_file.unlink(missing_ok=True)
                    cleared += 1
        except Exception:
            pass

        # Clear stale outbox lock
        try:
            lock_file = self.outbox_dir / ".lock"
            if lock_file.exists():
                lock_file.unlink()
                cleared += 1
        except Exception:
            pass

        if cleared > 0:
            self._log_activity(
                f"[STARTUP] Cleared {cleared} stale messages from previous session"
            )

    def get_outbox_status(self) -> dict:
        """Get current outbox queue status, including delivery ack stats."""
        try:
            pending = list(self.outbox_pending.glob("*.json"))
            expired = list(self.outbox_expired.glob("*.json"))

            result = {
                "pending_count": len(pending),
                "expired_count": len(expired),
                "oldest_age": (
                    time.time() - min(f.stat().st_mtime for f in pending)
                    if pending
                    else 0
                ),
            }

            # Include ack stats if available
            if ACK_TRACKING_AVAILABLE:
                ack_stats = get_ack_status(self.project)
                result["total_delivered"] = ack_stats.get("total_delivered", 0)
                result["total_failed"] = ack_stats.get("total_failed", 0)
                result["avg_latency"] = ack_stats.get("avg_latency", 0)
                result["recent_acks"] = ack_stats.get("recent", [])

            return result
        except Exception as e:
            self._log_activity(f"[ERROR] Failed to get outbox status: {e}")
            return {"pending_count": 0, "expired_count": 0, "oldest_age": 0}

    def _log_activity(self, message: str) -> None:
        """Append a message to the activity log."""
        try:
            with open(self.activity_log_file, "a") as f:
                f.write(f"{message}\n")
        except IOError:
            pass

    # ─────────────────────────────────────────────────────────────────────────
    # Learning Dedup State (supports event-driven learning on task completion)
    # ─────────────────────────────────────────────────────────────────────────

    def _load_learning_dedup_state(self) -> None:
        """Load persisted learning dedup state from disk.

        Restores both the set of task IDs that have already had learning events
        emitted and the set of processed learning file keys, so that dashboard
        restarts don't re-emit events or re-process files.
        """
        try:
            if self._learning_dedup_file.exists():
                data = json.loads(self._learning_dedup_file.read_text())
                self._learning_emitted_tasks = set(data.get("emitted_tasks", []))
                self.learnings_processed = set(data.get("processed_files", []))
                # Pre-populate previous_task_statuses for emitted tasks so they
                # aren't considered "newly completed" on the first loop iteration
                for tid in self._learning_emitted_tasks:
                    self.previous_task_statuses[tid] = "completed"
        except Exception:
            # Corrupted file — start fresh but don't crash
            self._learning_emitted_tasks = set()

    def _save_learning_dedup_state(self) -> None:
        """Persist learning dedup state to disk."""
        try:
            data = {
                "emitted_tasks": sorted(self._learning_emitted_tasks),
                "processed_files": sorted(self.learnings_processed),
            }
            self._learning_dedup_file.write_text(json.dumps(data, indent=2) + "\n")
        except Exception:
            pass  # Best-effort; don't crash the dashboard

    def detect_new_tasks(self, tasks: list[dict[str, Any]]) -> None:
        """Detect tasks added externally and notify the orchestrator.

        Compares current task IDs against the previous snapshot. When new
        tasks appear (e.g. from hook sessions, TG web UI, GSD discovery),
        sends a notification through the inbox so the orchestrator can
        spawn agents for them.

        On the first call (previous_task_ids is empty), populates the
        snapshot without notifying — avoids spam on dashboard startup.
        """
        current_ids = {t.get("id", "") for t in tasks if t.get("id")}

        if not self.previous_task_ids:
            # First run: seed the snapshot, don't notify
            self.previous_task_ids = current_ids
            return

        new_ids = current_ids - self.previous_task_ids
        if not new_ids:
            self.previous_task_ids = current_ids
            return

        # Collect details of genuinely new tasks (skip orchestrator meta-task)
        new_tasks = [
            t for t in tasks if t.get("id") in new_ids and t.get("id") != "orchestrator"
        ]

        if new_tasks:
            timestamp = datetime.now().strftime("%H:%M:%S")
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

            msg_text = "\n".join(lines)
            self._send_new_task_notification(new_ids, msg_text)

            self._log_activity(
                f"[{timestamp}] New task notification: {', '.join(sorted(new_ids))}"
            )

        self.previous_task_ids = current_ids

    def _send_new_task_notification(self, task_ids: set[str], content: str) -> None:
        """Send a new-task notification to the orchestrator.

        Queues directly via the outbox broker for reliable delivery (does
        not depend on the inbox daemon being alive).
        Also writes to the inbox for dashboard panel visibility.
        """
        self.queue_message(
            target=self.orchestrator_pane,
            content=content,
            priority=2,
            source="new_task_detection",
        )
        # Also write to inbox so the message appears in the dashboard panel
        try:
            timestamp = int(time.time())
            msg_file = self.inbox_pending / f"{timestamp}_new_task_detection.msg"
            msg_data = json.dumps(
                {
                    "text": content,
                    "priority": 2,
                    "source": "new_task_detection",
                    "target": "orchestrator",
                }
            )
            msg_file.write_text(msg_data)
        except Exception:
            pass

    def detect_task_completions(self, tasks: list[dict[str, Any]]) -> None:
        """Detect tasks that just transitioned to 'completed' and emit learning events.

        Compares current task statuses against previous snapshot. For each newly
        completed task, gathers any findings/notes and sends a learning event
        to the orchestrator via the outbox broker.

        Tasks that have already had learning events emitted (tracked in the
        persistent dedup set) are skipped to avoid spam on dashboard restart.
        """
        newly_completed: list[dict[str, Any]] = []

        for task in tasks:
            tid = task.get("id", "")
            current_status = task.get("status", "")
            previous_status = self.previous_task_statuses.get(tid, "")

            if current_status == "completed" and previous_status != "completed":
                # Skip if we already emitted a learning event for this task
                if tid not in self._learning_emitted_tasks:
                    newly_completed.append(task)

        # Update snapshot
        self.previous_task_statuses = {
            t.get("id", ""): t.get("status", "") for t in tasks
        }

        # Emit learning event for each newly completed task
        for task in newly_completed:
            tid = task.get("id", "")
            name = task.get("name") or tid
            title = task.get("title") or name
            self.completed_times[tid] = time.time()

            # Build the learning event message — keep it concise to avoid
            # flooding the orchestrator input buffer.  The orchestrator can
            # read the full findings via tasklib when it needs them.
            timestamp = datetime.now().strftime("%H:%M:%S")
            tasklib_path = (
                f"{os.path.expanduser('~')}/.claude/skills/claw-town"
                "/scripts/claw_town_tasklib.py"
            )
            lines = [
                f"Task {tid} ({title}) completed at {timestamp}.",
                f"Read findings: `python3 {tasklib_path} comments {tid} --prefix FINDINGS`",
            ]

            msg_text = "\n".join(lines)

            # Send through inbox so it appears in Message Queue and gets
            # forwarded to the orchestrator
            self._send_learning_to_inbox(tid, name, msg_text)

            # Mark as emitted and persist to disk
            self._learning_emitted_tasks.add(tid)

            self._log_activity(
                f"[{timestamp}] Learning event emitted for task {tid} ({name})"
            )

            if EVENT_LOG_AVAILABLE:
                log_event(
                    self.project,
                    "learn",
                    f"Task {tid} ({name}) completed — learning emitted",
                )

        # Persist dedup state if anything changed
        if newly_completed:
            self._save_learning_dedup_state()

    def _gather_task_learning(self, task_id: str, task_name: str) -> str | None:
        """Gather findings/notes for a specific completed task.

        Checks local files first, then falls back to reading task comments
        via tasklib subprocess call.

        Returns formatted learning content or None.
        """
        candidate_filenames = [
            "findings.md",
            "summary.md",
            "notes.md",
            "learnings.md",
        ]

        # Check both output directories
        search_dirs = [
            self.project_dir / "findings" / task_name,
            self.project_dir / "findings" / task_id,
            self.claw_town_state / "tasks" / task_id,
            self.claw_town_state / "tasks" / task_name,
        ]

        # Also check the learnings directory
        learnings_dir = self.claw_town_state / "learnings"
        if learnings_dir.exists():
            search_dirs.append(learnings_dir)

        parts: list[str] = []
        for search_dir in search_dirs:
            if not search_dir.exists():
                continue

            for filename in candidate_filenames:
                candidate = search_dir / filename
                if not candidate.exists():
                    continue

                try:
                    content = candidate.read_text().strip()
                    if content:
                        if len(content) > 1500:
                            content = content[:1500] + "\n... (truncated)"
                        parts.append(f"[{candidate.name}]\n{content}")
                        self.learnings_processed.add(str(candidate))
                except IOError:
                    pass

        if parts:
            return "\n\n".join(parts)

        # Fallback: read findings from task comments via tasklib
        return self._gather_task_learning_from_comments(task_id)

    def _gather_task_learning_from_comments(self, task_id: str) -> str | None:
        """Read FINDINGS and LEARNINGS comments from a task via tasklib.

        Returns formatted content or None if unavailable.
        """
        all_parts: list[str] = []
        for prefix in ("FINDINGS", "LEARNINGS"):
            try:
                result = subprocess.run(
                    [
                        "python3",
                        str(Path(__file__).parent / "claw_town_tasklib.py"),
                        "comments",
                        task_id,
                        "--prefix",
                        prefix,
                    ],
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                if result.returncode == 0 and result.stdout.strip():
                    data = json.loads(result.stdout)
                    comments = data.get("comments", [])
                    for comment in comments:
                        if isinstance(comment, dict):
                            content = comment.get("content", "")
                        elif isinstance(comment, str):
                            content = comment
                        else:
                            content = str(comment)
                        content = content.strip()
                        if content:
                            if len(content) > 1500:
                                content = content[:1500] + "\n... (truncated)"
                            all_parts.append(f"[task comment — {prefix}]\n{content}")
            except subprocess.TimeoutExpired:
                self._log_activity(
                    f"Tasklib comments read timed out for {task_id} ({prefix})"
                )
            except (json.JSONDecodeError, Exception) as e:
                self._log_activity(
                    f"Tasklib comments read failed for {task_id} ({prefix}): {e}"
                )

        return "\n\n".join(all_parts) if all_parts else None

    def _send_learning_to_inbox(
        self, task_id: str, task_name: str, content: str
    ) -> None:
        """Send a learning event to the orchestrator.

        Queues the message directly via the outbox broker for reliable
        delivery (does not depend on the inbox daemon being alive).
        """
        self.queue_message(
            target=self.orchestrator_pane,
            content=content,
            priority=3,
            source="learning",
        )

    def check_and_run_self_learn(self) -> bool:
        """Run the self-learning script to analyze for mistakes.

        This runs the claw_town_learn.py script which:
        1. Captures orchestrator output
        2. Analyzes for cardinal rule violations and other mistakes
        3. Generates learnings and updates skill files

        Returns True if self-learning ran.
        """
        now = time.time()

        # Only run periodically (every 5 minutes)
        if now - self.last_self_learn_time < self.self_learn_interval:
            return False

        self.last_self_learn_time = now

        try:
            if not self.self_learn_script.exists():
                self._log_activity(
                    f"Self-learn script not found: {self.self_learn_script}"
                )
                return False

            # Get current task count for idle-awareness
            state = self.load_state()
            active_tasks = [
                t
                for t in state.get("tasks", [])
                if t.get("status") not in ("completed", "cancelled")
                and t.get("id") != "orchestrator"
            ]
            task_count = len(active_tasks)

            # Run the learning script with task count
            result = subprocess.run(
                [
                    "python3",
                    str(self.self_learn_script),
                    self.project,
                    str(task_count),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )

            if result.returncode == 0:
                timestamp = datetime.now().strftime("%H:%M:%S")
                # Parse result
                try:
                    data = json.loads(result.stdout)
                    mistakes = data.get("mistakes_found", 0)
                    learnings = data.get("learnings_generated", 0)
                    if mistakes > 0:
                        self._log_activity(
                            f"[{timestamp}] Self-learn: {mistakes} mistakes → {learnings} learnings"
                        )
                    else:
                        self._log_activity(f"[{timestamp}] Self-learn: no issues found")
                except json.JSONDecodeError:
                    self._log_activity(f"[{timestamp}] Self-learn completed")
                return True
            else:
                self._log_activity(f"Self-learn error: {result.stderr[:100]}")
                return False

        except subprocess.TimeoutExpired:
            self._log_activity("Self-learn timed out")
            return False
        except Exception as e:
            self._log_activity(f"Self-learn error: {e}")
            return False

    def log_agent_activity(
        self, agents: list[dict], tasks: list[dict], state: dict
    ) -> None:
        """Continuously log agent activity and task states.

        This captures detailed notes about every agent and task on each refresh,
        providing granular history for resume purposes.
        """
        now = time.time()

        # Debounce to avoid excessive writes
        if now - self.last_activity_log_time < self.activity_log_interval:
            return

        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log_entries: list[str] = []

        # Check for agent state changes
        for agent in agents:
            agent_name = agent.get("name", "unknown")
            agent_status = agent.get("status", "unknown")
            last_output = agent.get("last_output", "")[-200:]

            # Get previous state for this agent
            prev_state = self.last_agent_states.get(agent_name, {})
            prev_status = prev_state.get("status", "")

            # Detect status change
            if agent_status != prev_status:
                log_entries.append(
                    f"[{timestamp}] AGENT {agent_name}: {prev_status or 'NEW'} → "
                    f"{agent_status}"
                )

                # Extract meaningful context from output
                if last_output:
                    # Find last non-empty line
                    lines = [
                        line.strip() for line in last_output.split("\n") if line.strip()
                    ]
                    if lines:
                        context = lines[-1][:100]
                        log_entries.append(f"  └─ Context: {context}")

            # Update last known state
            self.last_agent_states[agent_name] = {
                "status": agent_status,
                "last_output": last_output,
                "timestamp": now,
            }

        # Log task changes (only when status actually changes, skip orchestrator)
        in_progress = [t for t in tasks if t.get("status") == "in_progress"]
        for task in in_progress:
            task_id = task.get("id", "?")
            task_type = task.get("type", "")

            # Skip orchestrator task - it's always in_progress
            if task_type == "orchestrator":
                continue

            title = task.get("title") or task.get("subject") or task_id
            owner = task.get("owner") or task.get("assigned_to") or "unassigned"

            # Find corresponding agent
            agent_match = next((a for a in agents if owner in a.get("name", "")), None)
            agent_status = (
                agent_match.get("status", "unknown") if agent_match else "no agent"
            )

            # Only log if task status changed (track in last_agent_states with task_ prefix)
            task_key = f"task_{task_id}"
            prev_task_state = self.last_agent_states.get(task_key, {})
            prev_owner = prev_task_state.get("owner", "")
            prev_agent_status = prev_task_state.get("agent_status", "")

            if owner != prev_owner or agent_status != prev_agent_status:
                log_entries.append(
                    f"[{timestamp}] TASK {task_id}: {title[:50]} | "
                    f"Owner: {owner} | Agent: {agent_status}"
                )
                self.last_agent_states[task_key] = {
                    "owner": owner,
                    "agent_status": agent_status,
                }

        # Log sub-orchestrators
        sub_orchestrators = state.get("sub_orchestrators", [])
        for sub in sub_orchestrators:
            sub_id = sub.get("id", "unknown")
            sub_tasks_path = Path(sub.get("tasks_path", ""))

            if sub_tasks_path.exists():
                try:
                    with open(sub_tasks_path) as f:
                        sub_state = json.load(f)
                    sub_tasks = known_tasks_to_list(sub_state)

                    # Count statuses
                    completed = sum(
                        1 for t in sub_tasks if t.get("status") == "completed"
                    )
                    in_prog = sum(
                        1 for t in sub_tasks if t.get("status") == "in_progress"
                    )
                    total = len(sub_tasks)

                    log_entries.append(
                        f"[{timestamp}] SUB-ORCH [{sub_id}]: "
                        f"{completed}/{total} done, {in_prog} active"
                    )

                    # Log active sub-tasks
                    for sub_task in sub_tasks:
                        if sub_task.get("status") == "in_progress":
                            st_id = sub_task.get("id", "?")
                            st_title = (
                                sub_task.get("title")
                                or sub_task.get("subject")
                                or st_id
                            )
                            st_owner = (
                                sub_task.get("owner")
                                or sub_task.get("assigned_to")
                                or "unassigned"
                            )
                            log_entries.append(
                                f"  └─ [{sub_id}] TASK {st_id}: {st_title[:40]} | "
                                f"{st_owner}"
                            )
                except (json.JSONDecodeError, IOError):
                    log_entries.append(
                        f"[{timestamp}] SUB-ORCH [{sub_id}]: Error reading state"
                    )

        # Write to activity log if there are entries
        if log_entries:
            try:
                # Append to activity log
                with open(self.activity_log_file, "a") as f:
                    f.write("\n".join(log_entries) + "\n")

                self.last_activity_log_time = now

                # Compact older log entries if log gets large
                self._compact_activity_log()
            except IOError:
                pass

    def _compact_activity_log(self, compact_threshold: int = 500) -> None:
        """Compact older activity log entries to save space while preserving history.

        Instead of deleting old entries, this summarizes older sections into
        condensed summaries, keeping recent entries in full detail.
        """
        try:
            if not self.activity_log_file.exists():
                return

            with open(self.activity_log_file) as f:
                lines = f.readlines()

            # Only compact if we have enough lines
            if len(lines) < compact_threshold * 2:
                return

            # Split into old (to compact) and recent (to keep as-is)
            old_lines = lines[:-compact_threshold]
            recent_lines = lines[-compact_threshold:]

            # Parse old lines to create summary
            summary = create_activity_summary(old_lines)

            # Write compacted log
            header = [
                f"# Claw Town Activity Log: {self.project}\n",
                "# Continuously updated notes on orchestration\n",
                "# Older entries compacted into summary below\n\n",
            ]

            compacted_section = [
                "--- COMPACTED HISTORY ---\n",
                f"{summary}\n",
                "--- END COMPACTED HISTORY ---\n\n",
                "--- RECENT ACTIVITY (FULL DETAIL) ---\n\n",
            ]

            with open(self.activity_log_file, "w") as f:
                f.writelines(header + compacted_section + recent_lines)

        except IOError:
            pass

    # _create_activity_summary is now create_activity_summary()
    # imported from dashboard_data

    def render_task_tracking_panel(self, width: int) -> list[str]:
        """Render task tracking status panel."""
        content = []

        # --- GSD section ---
        gsd_status, gsd_color, gsd_detail, gsd_url = self.get_gsd_sync_status()

        if gsd_status == "SYNCED":
            gsd_icon = T.CHECK
        elif gsd_status == "PENDING":
            gsd_icon = "⏳ "
        elif gsd_status == "NOT CONFIGURED":
            gsd_icon = "- "
        else:
            gsd_icon = T.CIRCLE

        content.append(
            f" {T.BOLD}GSD:{T.RESET}    {gsd_icon}{gsd_color}{gsd_status}{T.RESET} {T.DIM}{gsd_detail}{T.RESET}"
        )

        if gsd_url:
            display_link = truncate(gsd_url, width - 4)
            content.append(f" └─ {T.UNDERLINE}{display_link}{T.RESET}")

        # Root task from tasks.json
        state = self.load_state()
        root_task = state.get("root_task")
        if root_task:
            content.append(f" {T.DIM}Root: {root_task}{T.RESET}")

        return content

    def get_gsd_sync_status(self) -> tuple[str, str, str, str | None]:
        """Get GSD sync status from tasks.json.

        Returns: (status, status_color, detail, gsd_url)
        """
        tasks_data = self.load_state()
        gsd_url = tasks_data.get("gsd_url")
        gsd_project_id = tasks_data.get("gsd_project_id")

        if not gsd_project_id and not gsd_url:
            return "NOT CONFIGURED", T.GRAY, "Use --gsd <url> to enable", None

        root_task = tasks_data.get("root_task")
        last_poll = tasks_data.get("last_gsd_poll")

        if not root_task:
            return "PENDING", T.YELLOW, "Initial import pending...", gsd_url

        if last_poll:
            try:
                poll_time = datetime.fromisoformat(last_poll)
                age_secs = (datetime.now() - poll_time).total_seconds()
                if age_secs < 120:
                    detail = f"{root_task} polled {int(age_secs)}s ago"
                elif age_secs < 3600:
                    detail = f"{root_task} polled {int(age_secs / 60)}m ago"
                else:
                    detail = f"{root_task} polled {int(age_secs / 3600)}h ago"
                return "SYNCED", T.GREEN, detail, gsd_url
            except (ValueError, TypeError):
                pass

        return "SYNCED", T.GREEN, f"{root_task}", gsd_url

    def get_inbox_messages(self, limit: int = 50) -> list[dict]:
        """Read recent messages from inbox (pending + processed) for display.

        Returns a list of message dicts sorted by timestamp (newest last).
        """
        messages = []
        dirs_to_check = [self.inbox_pending]
        processed_dir = self.inbox_dir / "processed"
        if processed_dir.exists():
            dirs_to_check.append(processed_dir)

        for directory in dirs_to_check:
            try:
                for msg_file in directory.glob("*.msg"):
                    try:
                        content = msg_file.read_text().strip()
                        name = msg_file.stem
                        parts = name.split("_", 1)
                        sender = parts[1] if len(parts) > 1 else "system"

                        try:
                            msg_data = json.loads(content)
                        except json.JSONDecodeError:
                            msg_data = {"text": content}

                        is_pending = directory == self.inbox_pending
                        messages.append(
                            {
                                "time": parts[0] if parts else "unknown",
                                "sender": sender,
                                "text": msg_data.get("text", content),
                                "priority": msg_data.get("priority", 3),
                                "source": msg_data.get("source", "system"),
                                "target": msg_data.get("target", "orchestrator"),
                                "status": "pending" if is_pending else "sent",
                            }
                        )
                    except Exception:
                        pass
            except Exception:
                pass

        # Sort by timestamp (filename prefix) and return most recent
        messages.sort(key=lambda m: m.get("time", ""))
        return messages[-limit:]

    def _read_outbox_messages(self, directory: Path, limit: int = 50) -> list[dict]:
        """Read messages from an outbox directory (pending or expired).

        Returns a list of message dicts sorted by timestamp (newest last).
        """
        messages = []
        if not directory.exists():
            return messages

        try:
            for msg_file in directory.glob("*.json"):
                try:
                    msg_data = json.loads(msg_file.read_text())
                    # Parse timestamp from filename: priority_YYYYMMDD_HHMMSS_micros_source.json
                    name = msg_file.stem
                    parts = name.split("_", 1)
                    time_str = parts[1] if len(parts) > 1 else "unknown"
                    # Extract source from filename suffix
                    name_parts = name.rsplit("_", 1)
                    source_from_name = (
                        name_parts[-1] if len(name_parts) > 1 else "unknown"
                    )

                    content = msg_data.get("content", "")
                    target = msg_data.get("target", "unknown")
                    source = msg_data.get("source", source_from_name)
                    priority = msg_data.get("priority", 3)
                    queued_at = msg_data.get("queued_at", 0)
                    age = time.time() - queued_at if queued_at else 0

                    messages.append(
                        {
                            "time": time_str,
                            "sender": source,
                            "text": content,
                            "priority": priority,
                            "source": source,
                            "target": target,
                            "status": "pending",
                            "queued_at": queued_at,
                            "age": age,
                            "filename": msg_file.name,
                        }
                    )
                except (json.JSONDecodeError, Exception):
                    pass
        except Exception:
            pass

        messages.sort(key=lambda m: m.get("time", ""))
        return messages[-limit:]

    def get_outbox_pending_messages(self, limit: int = 50) -> list[dict]:
        """Read pending outbox messages (waiting to be delivered)."""
        return self._read_outbox_messages(self.outbox_pending, limit)

    def get_outbox_expired_messages(self, limit: int = 50) -> list[dict]:
        """Read expired outbox messages (TTL exceeded or invalid)."""
        msgs = self._read_outbox_messages(self.outbox_expired, limit)
        for m in msgs:
            m["status"] = "expired"
        return msgs

    def render_message_queue_panel(
        self,
        width: int,
        max_lines: int = 0,
        msg_selected_idx: int | None = None,
        msg_expanded: set | None = None,
        focused_panel: str | None = None,
    ) -> list[str]:
        """Render the Message Queue panel with Inbox/Pending/Expired subtabs.

        Args:
            width: Available content width inside the box.
            max_lines: Maximum content lines. 0 means no limit (fill all space).
            msg_selected_idx: Index of currently highlighted message.
            msg_expanded: Set of message indices that are expanded to show full text.
            focused_panel: Which panel has keyboard focus ('tasks' or 'messages').

        Returns:
            List of content lines for the panel.
        """
        if msg_selected_idx is None:
            msg_selected_idx = self.msg_selected_idx
        if msg_expanded is None:
            msg_expanded = self.msg_expanded
        if focused_panel is None:
            focused_panel = self.focused_panel

        content = []
        is_focused = focused_panel == "messages"
        active_tab = self.msg_queue_tab

        # Fetch counts for all tabs
        inbox_msgs = self.get_inbox_messages()
        outbox_pending_msgs = self.get_outbox_pending_messages()
        outbox_expired_msgs = self.get_outbox_expired_messages()

        # Split inbox into user messages vs system events for accurate counts
        system_sources = {"system", "dashboard", "self-repair", "health-check", "event"}
        inbox_user_msgs = [
            m for m in inbox_msgs if m.get("source") not in system_sources
        ]
        inbox_event_msgs = [m for m in inbox_msgs if m.get("source") in system_sources]
        inbox_user_count = len(inbox_user_msgs)
        inbox_event_count = len(inbox_event_msgs)
        pending_count = len(outbox_pending_msgs)
        expired_count = len(outbox_expired_msgs)

        # ── Subtab bar ──
        def tab_label(name: str, count: int, key: str, is_active: bool) -> str:
            label = f"{key}:{name}({count})"
            if is_active:
                return f"{T.BG_CYAN}{T.WHITE}{T.BOLD} {label} {T.RESET}"
            else:
                return f"{T.DIM} {label} {T.RESET}"

        # Show user message count on inbox tab; event count shown in status line
        inbox_display_count = inbox_user_count if inbox_user_count else len(inbox_msgs)
        tab_bar = (
            tab_label("Inbox", inbox_display_count, "1", active_tab == "inbox")
            + tab_label("Pending", pending_count, "2", active_tab == "pending")
            + tab_label("Expired", expired_count, "3", active_tab == "expired")
        )
        content.append(f" {tab_bar}")

        # ── Message key / legend ──
        key_line = (
            f" {T.DIM}"
            f"{T.GREEN}{T.CHECK}{T.DIM} Acked  "
            f"{T.CYAN}{T.CIRCLE_FILLED}{T.DIM} Event  "
            f"{T.YELLOW}*{T.DIM} Queued  "
            f"{T.RED}!{T.DIM} Priority"
            f"{T.RESET}"
        )
        content.append(key_line)
        content.append("")

        # ── Select messages for active tab ──
        if active_tab == "inbox":
            messages = inbox_msgs
        elif active_tab == "pending":
            messages = outbox_pending_msgs
        else:
            messages = outbox_expired_msgs

        if not messages:
            if active_tab == "inbox":
                content.append(f" {T.DIM}No inbox messages yet{T.RESET}")
                content.append(f' {T.DIM}Send: claw-town msg <project> "msg"{T.RESET}')
            elif active_tab == "pending":
                content.append(f" {T.DIM}No pending outbox messages{T.RESET}")
                content.append(
                    f" {T.DIM}Messages appear here while awaiting delivery{T.RESET}"
                )
            else:
                content.append(f" {T.DIM}No expired messages{T.RESET}")
                content.append(
                    f" {T.DIM}Messages expire after {self.outbox_message_ttl}s TTL{T.RESET}"
                )

            # Show delivery stats on inbox tab even when empty
            if active_tab == "inbox" and ACK_TRACKING_AVAILABLE:
                ack_stats = get_ack_status(self.project)
                total_d = ack_stats.get("total_delivered", 0)
                total_f = ack_stats.get("total_failed", 0)
                if total_d > 0 or total_f > 0:
                    content.append("")
                    avg_lat = ack_stats.get("avg_latency", 0)
                    content.append(
                        f" {T.DIM}Delivery:{T.RESET} "
                        f"{T.GREEN}{total_d} acked{T.RESET}"
                        f"{f' | {T.RED}{total_f} failed{T.RESET}' if total_f else ''}"
                        f"{f' | {T.DIM}{avg_lat:.1f}s avg{T.RESET}' if avg_lat > 0 else ''}"
                    )
            return content

        # ── Status summary line (inbox tab only) ──
        if active_tab == "inbox":
            inbox_pending = [m for m in messages if m.get("status") == "pending"]
            status_parts = []
            if inbox_pending:
                status_parts.append(f"{T.YELLOW}{len(inbox_pending)} queued{T.RESET}")
            if inbox_user_count > 0:
                status_parts.append(f"{T.WHITE}{inbox_user_count} user{T.RESET}")
            if inbox_event_count > 0:
                status_parts.append(f"{T.CYAN}{inbox_event_count} events{T.RESET}")

            if ACK_TRACKING_AVAILABLE:
                ack_stats = get_ack_status(self.project)
                total_d = ack_stats.get("total_delivered", 0)
                total_f = ack_stats.get("total_failed", 0)
                avg_lat = ack_stats.get("avg_latency", 0)
                if total_d > 0:
                    status_parts.append(f"{T.GREEN}{total_d} acked{T.RESET}")
                if total_f > 0:
                    status_parts.append(f"{T.RED}{total_f} failed{T.RESET}")
                if avg_lat > 0:
                    status_parts.append(f"{T.DIM}{avg_lat:.1f}s avg{T.RESET}")
            content.append(f" {' | '.join(status_parts)}")
            content.append("")

        # ── Clamp selected index ──
        if messages:
            msg_selected_idx = max(0, min(msg_selected_idx, len(messages) - 1))

        # ── Render message list ──
        for idx, msg in enumerate(messages):
            is_selected = is_focused and idx == msg_selected_idx
            is_expanded = idx in msg_expanded

            # Selection prefix
            if is_focused:
                sel_prefix = (
                    f"{T.BOLD}{T.CYAN}\u25b6 {T.RESET}" if is_selected else "  "
                )
            else:
                sel_prefix = " "

            # Expand/collapse indicator
            expand_icon = "[-]" if is_expanded else "[+]"

            # Priority indicator
            priority = msg.get("priority", 3)
            priority_int = (
                int(priority)
                if isinstance(priority, int)
                or (isinstance(priority, str) and priority.isdigit())
                else 3
            )
            if priority == "high" or priority_int <= 2:
                p_icon = f"{T.RED}{T.BOLD}!{T.RESET}"
            elif priority == "low" or priority_int >= 4:
                p_icon = f"{T.GRAY}-{T.RESET}"
            else:
                p_icon = f"{T.BLUE}.{T.RESET}"

            # Status indicator varies by tab
            if active_tab == "inbox":
                source = msg.get("source", "")
                if source in (
                    "system",
                    "dashboard",
                    "self-repair",
                    "health-check",
                    "event",
                ):
                    s_icon = f"{T.CYAN}{T.CIRCLE_FILLED}{T.RESET}"
                elif msg.get("status") == "pending":
                    s_icon = f"{T.YELLOW}*{T.RESET}"
                else:
                    s_icon = f"{T.GREEN}{T.CHECK}{T.RESET}"
            elif active_tab == "pending":
                s_icon = f"{T.YELLOW}*{T.RESET}"
            else:  # expired
                s_icon = f"{T.RED}{T.CROSS}{T.RESET}"

            # Source / target display
            if active_tab == "inbox":
                sender = msg.get("sender", "?")[:10]
                label = f"{T.CYAN}{sender}{T.RESET}"
            elif active_tab == "pending":
                target = msg.get("target", "?")
                if ":" in target:
                    target = target.split(":")[-1]
                target = target[:16]
                sender = msg.get("sender", "?")[:8]
                age = msg.get("age", 0)
                age_str = (
                    f"{int(age)}s" if age < 60 else f"{int(age // 60)}m{int(age % 60)}s"
                )
                label = (
                    f"{T.DIM}{sender}{T.RESET}"
                    f"{T.DIM}{T.ARROW_R}{T.RESET}"
                    f"{T.CYAN}{target}{T.RESET}"
                    f" {T.DIM}{age_str}{T.RESET}"
                )
            else:  # expired
                target = msg.get("target", "?")
                if ":" in target:
                    target = target.split(":")[-1]
                target = target[:16]
                sender = msg.get("sender", "?")[:8]
                age = msg.get("age", 0)
                if age > 3600:
                    age_str = f"{int(age // 3600)}h{int((age % 3600) // 60)}m"
                elif age > 60:
                    age_str = f"{int(age // 60)}m{int(age % 60)}s"
                else:
                    age_str = f"{int(age)}s"
                label = (
                    f"{T.DIM}{sender}{T.RESET}"
                    f"{T.DIM}{T.ARROW_R}{T.RESET}"
                    f"{T.RED}{target}{T.RESET}"
                    f" {T.DIM}expired {age_str} ago{T.RESET}"
                )

            # Time: format nicely if it looks like YYYYMMDD_HHMMSS
            raw_time = msg.get("time", "")
            if len(raw_time) >= 15 and "_" in raw_time:
                time_part = raw_time.split("_")[-1] if "_" in raw_time else raw_time
                if len(time_part) == 6:
                    display_time = f"{time_part[:2]}:{time_part[2:4]}:{time_part[4:6]}"
                else:
                    display_time = raw_time[-8:]
            else:
                display_time = raw_time[-8:] if len(raw_time) > 8 else raw_time

            # Header line
            header = (
                f"{sel_prefix}{p_icon}{s_icon} "
                f"{T.DIM}{expand_icon} {display_time}{T.RESET} "
                f"{label}"
            )
            if is_selected:
                header = f"{T.REVERSE}{header}{T.RESET}"
            content.append(header)

            # Message text
            text = msg.get("text", "").rstrip("\n")
            if text:
                indent = "     " if is_focused else "   "
                if is_expanded:
                    wrap_width = width - len(indent) - 1
                    if wrap_width < 10:
                        wrap_width = 10
                    for line in text.split("\n"):
                        if not line:
                            content.append(indent)
                            continue
                        while len(line) > wrap_width:
                            break_at = line.rfind(" ", 0, wrap_width)
                            if break_at <= 0:
                                break_at = wrap_width
                            content.append(f"{indent}{line[:break_at]}")
                            line = line[break_at:].lstrip()
                        content.append(f"{indent}{line}")
                else:
                    first_line = text.split("\n")[0]
                    avail = width - len(indent) - 1
                    if avail < 5:
                        avail = 5
                    if len(first_line) > avail:
                        first_line = first_line[: avail - 1] + "\u2026"
                    content.append(f"{indent}{T.DIM}{first_line}{T.RESET}")

        return content

    def render_tasks_panel(self, tasks: list[dict], width: int) -> list[str]:
        """Render the Tasks DAG panel with scrolling support."""
        content = []
        # Map content line index -> task_id for click-to-expand
        self._task_content_line_map: dict[int, str] = {}

        if not tasks:
            content.append(f" {T.DIM}No tasks defined{T.RESET}")
            return content

        # Status icons (aligned with Mission panel)
        status_icons = {
            "open": f"{T.CYAN}{T.CIRCLE}{T.RESET}",
            "pending": f"{T.YELLOW}{T.CIRCLE}{T.RESET}",
            "blocked": f"{T.RED}{T.CIRCLE}{T.RESET}",
            "in_progress": f"{T.YELLOW}{T.CIRCLE_FILLED}{T.RESET}",
            "completed": f"{T.GREEN}{T.CHECK}{T.RESET}",
            "failed": f"{T.RED}{T.CROSS}{T.RESET}",
        }

        # Calculate depths for indentation
        # The orchestrator task is the root (depth 0). The orchestrator's
        # blocked_by lists all its direct children (the reversed data model).
        # Tasks listed in the orchestrator's blocked_by render at depth 1.
        # Tasks with blocked_by pointing to a non-orchestrator task render
        # at depth 2+. Tasks not in any chain default to depth 1.
        completed_ids = {t.get("id") for t in tasks if t.get("status") == "completed"}
        task_by_id = {t.get("id"): t for t in tasks}

        # Find orchestrator task and its direct children
        orch_id = None
        orch_children: set[str] = set()
        for t in tasks:
            if t.get("type") == "orchestrator":
                orch_id = t.get("id")
                orch_children = set(t.get("blocked_by", t.get("depends_on", [])))
                break

        # Build reverse map: task_id -> set of tasks that depend on it
        depended_on_by: dict[str, set[str]] = {}
        for t in tasks:
            tid = t.get("id", "")
            for dep in t.get("blocked_by", t.get("depends_on", [])):
                depended_on_by.setdefault(dep, set()).add(tid)

        def get_depth(task_id: str, visited: set = None) -> int:
            """Compute depth for display. Orchestrator = 0, others based on chain."""
            if task_id == orch_id:
                return 0
            if visited is None:
                visited = set()
            if task_id in visited:
                return 1
            visited.add(task_id)
            # If this task is a direct child of orchestrator, depth 1
            if task_id in orch_children:
                return 1
            task = task_by_id.get(task_id)
            if not task:
                return 1
            deps = task.get("blocked_by", task.get("depends_on", []))
            # Filter out orchestrator from deps
            deps = [d for d in deps if d != orch_id]
            if deps:
                return 1 + max(get_depth(d, visited.copy()) for d in deps)
            # No deps — check if another task depends on this one
            dependents = depended_on_by.get(task_id, set())
            if dependents:
                # Render one level deeper than the shallowest dependent
                return 1 + min(get_depth(d, visited.copy()) for d in dependents)
            return 1  # truly orphan

        total_tasks = len(tasks)
        self.task_scroll_total = total_tasks

        # Get task numbers mapping for T-number display
        task_numbers = self.get_task_numbers()

        # Show all tasks - let terminal handle scrolling
        visible_tasks = tasks

        for _idx, task in enumerate(visible_tasks):
            status = task.get("status", "open")
            task_id = task.get("id", "?")
            # Get task title - try multiple field names
            task_title = (
                task.get("title") or task.get("subject") or task.get("name") or task_id
            )
            deps = task.get(
                "blocked_by", task.get("depends_on", task.get("blockedBy", []))
            )
            is_ready = is_task_ready(task, tasks)
            depth = get_depth(task_id)

            # Status icon
            if is_ready and status in ["open", "pending"]:
                icon = f"{T.CYAN}{T.CIRCLE_DOT}{T.RESET}"
            else:
                icon = status_icons.get(status, f"{T.GRAY}?{T.RESET}")

            # Indentation — use connector chars for visual DAG
            if depth == 0:
                indent = ""
            else:
                indent = "│ " * (depth - 1) + "├─"

            # T-number display
            t_number = task_numbers.get(task_id, "")
            if t_number:
                t_display = f"{T.MAGENTA}{t_number}{T.RESET} "
            else:
                t_display = ""

            # Title with expand/collapse support
            prefix = f" {indent}{icon} {t_display}"
            prefix_vis = strip_ansi(prefix)
            title_str = str(task_title)
            first_avail = max(10, width - prefix_vis)
            is_expanded = task_id in self.task_expanded

            # Record the content line index for the main task line
            task_line_idx = len(content)
            self._task_content_line_map[task_line_idx] = task_id

            if len(title_str) <= first_avail:
                content.append(f"{prefix}{title_str}")
            elif is_expanded:
                # Expanded: word-wrap title onto indented continuation lines
                wrap_indent = " " * (prefix_vis + 2)
                cont_avail = max(10, width - len(wrap_indent))
                words = title_str.split()
                wrapped: list[str] = []
                cur = ""
                avail = first_avail
                for word in words:
                    if not cur:
                        cur = word
                    elif len(cur) + 1 + len(word) <= avail:
                        cur += " " + word
                    else:
                        wrapped.append(cur)
                        cur = word
                        avail = cont_avail
                if cur:
                    wrapped.append(cur)
                if wrapped:
                    content.append(f"{prefix}{wrapped[0]}")
                    for wl in wrapped[1:]:
                        content.append(f"{wrap_indent}{wl}")
                else:
                    content.append(f"{prefix}{title_str}")
            else:
                # Collapsed: truncate with ellipsis
                content.append(f"{prefix}{title_str[: first_avail - 1]}\u2026")

            # Show waiting deps (for any non-completed task with unresolved deps)
            if deps:
                waiting = [d[:10] for d in deps if d not in completed_ids]
                if waiting:
                    dep_indent = "│ " * depth if depth > 0 else ""
                    content.append(
                        f" {dep_indent}  {T.DIM}{T.ARROW_L} waits on {', '.join(waiting)}{T.RESET}"
                    )

        return content

    def _query_cursor_row(self) -> int | None:
        """Query the terminal for the current cursor row using ANSI DSR."""
        return query_cursor_row()

    # ─────────────────────────────────────────────────────────────────────────
    # Main Render
    # ─────────────────────────────────────────────────────────────────────────

    def render(self) -> None:
        """Render the complete dashboard.

        All panels are rendered into a buffer first, then truncated to fit
        within the terminal height so that the footer is always visible and
        nothing overflows the alternate screen buffer.
        """
        self.get_terminal_size()

        # Clear screen and move cursor to top
        print("\033[2J\033[H", end="")

        # Load state
        state = self.load_state()
        tasks = state.get("tasks", [])
        agents = self.get_live_agents()
        working_dir = state.get("working_dir", "")

        # Calculate panel width (single column for sidebar layout)
        panel_width = self.term_width - 2

        # ─────────────────────────────────────────────────────────────────────
        # Collect all output lines into a buffer for height management
        # ─────────────────────────────────────────────────────────────────────
        lines: list[str] = []

        # ─────────────────────────────────────────────────────────────────────
        # Header with Lobster Town Art (responsive to terminal width)
        # ─────────────────────────────────────────────────────────────────────
        L = T.LOBSTER_RED
        subtitle = "Multi-Agent Orchestrator"

        lines.append("")
        lines.append("")
        lines.append("")

        if self.term_width >= 72:
            # Wide: 3 lobsters + CLAW TOWN box side-by-side (66 visible chars)
            content_width = 66
            pad = max(0, (self.term_width - content_width) // 2)
            p = " " * pad
            lines.append(
                f"{p}{L}▚▘ ▐▛███▜▌ ▘▞{T.RESET}  {L}▚▘ ▐▛███▜▌ ▘▞{T.RESET}  {L}▚▘ ▐▛███▜▌ ▘▞{T.RESET}   {L}╔══════════════════╗{T.RESET}"
            )
            lines.append(
                f"{p}  {L}▝▜█████▛▘{T.RESET}      {L}▝▜█████▛▘{T.RESET}      {L}▝▜█████▛▘{T.RESET}     {L}║{T.RESET}{T.BOLD}{L}    CLAW TOWN     {T.RESET}{L}║{T.RESET}"
            )
            lines.append(
                f"{p}    {L}▘▘ ▝▝{T.RESET}          {L}▘▘ ▝▝{T.RESET}          {L}▘▘ ▝▝{T.RESET}       {L}╚══════════════════╝{T.RESET}"
            )
            sub_pad = pad + content_width - len(subtitle)
            lines.append(f"{' ' * max(0, sub_pad)}{T.DIM}{subtitle}{T.RESET}")

        elif self.term_width >= 50:
            # Medium: 1 lobster + CLAW TOWN box side-by-side (36 visible chars)
            content_width = 36
            pad = max(0, (self.term_width - content_width) // 2)
            p = " " * pad
            lines.append(
                f"{p}{L}▚▘ ▐▛███▜▌ ▘▞{T.RESET}   {L}╔══════════════════╗{T.RESET}"
            )
            lines.append(
                f"{p}  {L}▝▜█████▛▘{T.RESET}     {L}║{T.RESET}{T.BOLD}{L}    CLAW TOWN     {T.RESET}{L}║{T.RESET}"
            )
            lines.append(
                f"{p}    {L}▘▘ ▝▝{T.RESET}       {L}╚══════════════════╝{T.RESET}"
            )
            sub_pad = pad + content_width - len(subtitle)
            lines.append(f"{' ' * max(0, sub_pad)}{T.DIM}{subtitle}{T.RESET}")

        else:
            # Narrow: text-only centered
            title = "═══ CLAW TOWN ═══"
            title_pad = max(0, (self.term_width - len(title)) // 2)
            lines.append(f"{' ' * title_pad}{L}{T.BOLD}{title}{T.RESET}")
            sub_pad = max(0, (self.term_width - len(subtitle)) // 2)
            lines.append(f"{' ' * sub_pad}{T.DIM}{subtitle}{T.RESET}")
        lines.append("")
        lines.append(f"  {T.CYAN}Project:{T.RESET} {self.project}")
        if working_dir:
            display_dir = truncate(working_dir, 60)
            lines.append(f"  {T.DIM}Dir:{T.RESET} {display_dir}")

        # Mode indicators
        skip_perms, yolo, parallel = self.get_mode_status()
        sp_color = T.GREEN if skip_perms else T.RED
        sp_label = "ON" if skip_perms else "OFF"
        yolo_color = T.GREEN if yolo else T.RED
        yolo_label = "ON" if yolo else "OFF"
        par_color = T.GREEN if parallel else T.RED
        par_label = "ON" if parallel else "OFF"
        mode_line = (
            f"  {sp_color}{T.BOLD}[SKIP-PERMS: {sp_label}]{T.RESET}"
            f"  {yolo_color}{T.BOLD}[YOLO: {yolo_label}]{T.RESET}"
            f"  {par_color}{T.BOLD}[FORCE PARALLEL: {par_label}]{T.RESET}"
        )
        lines.append(mode_line)

        lines.append("")
        lines.append(f"  {L}{'─' * (self.term_width - 4)}{T.RESET}")
        lines.append("")

        # ─────────────────────────────────────────────────────────────────────
        # Panels — render into buffer, then truncate to fit terminal height
        # ─────────────────────────────────────────────────────────────────────
        inner_w = panel_width - 4

        # Mission panel
        mission_content = self.render_mission_panel(tasks, agents, inner_w)
        mission_box = draw_box("Mission", mission_content, panel_width, T.CYAN)
        for line in mission_box:
            lines.append(f" {line}")
        lines.append("")

        # Agents panel
        agents_content = self.render_agents_panel(agents, inner_w)
        agents_box = draw_box("CLI Agents", agents_content, panel_width, T.MAGENTA)
        for line in agents_box:
            lines.append(f" {line}")
        lines.append("")

        # Orchestrator panel (includes loop status info)
        orch_content = self.render_orchestrator_panel(agents, tasks, inner_w)
        orch_box = draw_box("Orchestrator", orch_content, panel_width, T.YELLOW)
        for line in orch_box:
            lines.append(f" {line}")
        lines.append("")

        # Task Tracking panel (combined TG + GSD)
        tt_content = self.render_task_tracking_panel(inner_w)
        tt_box = draw_box("Task Tracking", tt_content, panel_width, T.BLUE)
        for line in tt_box:
            lines.append(f" {line}")
        lines.append("")

        # Sub-Orchestrators panel (only if there are sub-orchestrators)
        sub_orchs = self.get_sub_orchestrator_details()
        if sub_orchs:
            sub_content = self.render_sub_orchestrators_panel(inner_w)
            sub_box = draw_box("Sub-Orchestrators", sub_content, panel_width, T.CYAN)
            for line in sub_box:
                lines.append(f" {line}")
            lines.append("")

        # Tasks panel
        tasks_box_start = len(lines)
        if tasks:
            tasks_content = self.render_tasks_panel(tasks, inner_w)
            tasks_box = draw_box("Tasks", tasks_content, panel_width, T.BLUE)
            for line in tasks_box:
                lines.append(f" {line}")
            lines.append("")

        # ─────────────────────────────────────────────────────────────────────
        # Footer — always visible at bottom
        # ─────────────────────────────────────────────────────────────────────
        timestamp = datetime.now().strftime("%H:%M:%S")

        # Context-sensitive keyboard hints based on focused panel
        panel_indicator = f"[{self.focused_panel.capitalize()}]"
        scroll_hint = ""
        if self._total_content_lines > self.term_height - 1:
            scroll_hint = " │ \u2191\u2193/PgUp/PgDn: scroll"
        if self.focused_panel == "messages":
            hints = f"Tab: panel │ j/k: navigate │ Enter: expand{scroll_hint}"
        elif self.focused_panel == "tasks":
            hints = f"Tab: panel │ j/k: navigate │ Enter: select{scroll_hint}"
        else:
            hints = f"Tab: switch panels{scroll_hint}"

        footer = (
            f" {T.DIM}{timestamp} │ {panel_indicator} {hints} │ "
            f"vo:{self.viewport_offset} │ "
            f"Ctrl+b n: next window{T.RESET}"
        )

        # ─────────────────────────────────────────────────────────────────────
        # Output: viewport scrolling — show a window of content based on offset
        # ─────────────────────────────────────────────────────────────────────
        max_content_lines = self.term_height - 1  # Reserve 1 line for footer
        self._total_content_lines = len(lines)

        # Clamp viewport offset
        max_offset = max(0, len(lines) - max_content_lines)
        _vo_before = self.viewport_offset
        self.viewport_offset = max(0, min(self.viewport_offset, max_offset))
        if _vo_before != self.viewport_offset or self.viewport_offset > 0:
            with open("/tmp/claw_dashboard_scroll_debug.log", "a") as _f:
                _f.write(
                    f"[{time.strftime('%H:%M:%S')}] RENDER: lines={len(lines)} max_content={max_content_lines} max_offset={max_offset} vo_before={_vo_before} vo_after={self.viewport_offset}\n"
                )

        if len(lines) > max_content_lines:
            # Show viewport window with scroll indicators
            visible = lines[
                self.viewport_offset : self.viewport_offset + max_content_lines
            ]
            above = self.viewport_offset
            below = len(lines) - self.viewport_offset - max_content_lines

            # Replace first visible line with scroll-up indicator if content above
            if above > 0:
                visible[0] = (
                    f" {T.DIM}\u25b2 {above} lines above (PgUp/PgDn to scroll){T.RESET}"
                )

            # Replace last visible line with scroll-down indicator if content below
            if below > 0:
                visible[-1] = (
                    f" {T.DIM}\u25bc {below} lines below (PgUp/PgDn to scroll){T.RESET}"
                )

            for line in visible:
                print(line)
        else:
            for line in lines:
                print(line)

        # Build _task_row_map for click handling on tasks panel
        self._task_row_map = {}
        if tasks and hasattr(self, "_task_content_line_map"):
            # tasks_box_start is the index in lines[] where Tasks panel begins
            # The box has 1 header line (top border), then content lines.
            for content_idx, task_id in self._task_content_line_map.items():
                screen_row = (
                    tasks_box_start + 1 + content_idx + 1
                )  # +1 for 1-based rows
                self._task_row_map[screen_row] = task_id

        print(footer)

    # ─────────────────────────────────────────────────────────────────────────
    # Nudge Logic
    # ─────────────────────────────────────────────────────────────────────────

    def should_nudge(self, agents: list[dict], tasks: list[dict]) -> bool:
        """Determine if orchestrator needs a nudge."""
        idle_time = time.time() - self.last_activity_time
        cooldown_elapsed = time.time() - self.last_nudge_time > self.nudge_interval

        # Reset nudge counter when orchestrator becomes active (output changed)
        if idle_time < self.nudge_interval and self.nudge_count > 0:
            self.nudge_count = 0

        if idle_time < self.nudge_interval:
            return False

        if not cooldown_elapsed:
            return False

        # Check if there's something actionable
        needs_input = [a for a in agents if a["status"] == AgentStatus.NEEDS_INPUT]
        ready_tasks = [
            t for t in tasks if is_task_ready(t, tasks) and not t.get("owner")
        ]

        return bool(needs_input or ready_tasks)

    def nudge_orchestrator(self, agents: list[dict], tasks: list[dict]) -> bool:
        """Send a nudge to the orchestrator via the message queue — targeted delivery."""
        # Count actionable items
        needs_input = [a for a in agents if a["status"] == AgentStatus.NEEDS_INPUT]
        ready_tasks = [
            t for t in tasks if is_task_ready(t, tasks) and not t.get("owner")
        ]

        # Build assertive nudge message
        message = f"""🚨 **WHY ARE YOU STALLED? KEEP GOING!** 🚨

You've been idle. The orchestrator must ALWAYS be making progress.

**Current State:**
- Agents needing input: {len(needs_input)}
- Ready tasks without owner: {len(ready_tasks)}
- Total agents: {len(agents)}

**What you should be doing RIGHT NOW:**
1. Check if any agents completed → collect their outputs
2. Spawn agents for ready tasks → don't let work pile up
3. Nudge stuck agents → send them recovery prompts
4. Check task dependencies → unblock what you can

**CARDINAL RULES:**
⛔ ALL work is spawned as new Claude sessions - are you spawning or doing work directly?
⛔ Keep orchestrating - don't wait for input, use your judgment!

**Stop waiting. Start orchestrating. NOW.**"""

        try:
            # Queue for targeted delivery to the orchestrator pane
            self.queue_message(
                target=self.orchestrator_pane,
                content=message,
                priority=1,
                source="nudge",
            )

            self.last_nudge_time = time.time()
            self.nudge_count += 1
            self._log_activity("Orchestrator nudge queued for delivery")

            # Log event for visibility
            if EVENT_LOG_AVAILABLE:
                idle_time = int(time.time() - self.last_activity_time) // 60
                log_event(
                    self.project,
                    "nudge_orch",
                    f"Orchestrator nudged ({self.nudge_count}/{self.max_nudges}) -- idle {idle_time}m",
                )

            return True
        except Exception as e:
            self._log_activity(f"Nudge error: {e}")
            return False

    def update_agent_activity(self, agent_name: str, is_active: bool) -> None:
        """Update activity tracking for an agent."""
        now = time.time()

        if agent_name not in self.agent_nudge_state:
            self.agent_nudge_state[agent_name] = {
                "last_activity": now,
                "last_nudge": 0,
                "nudge_count": 0,
            }

        if is_active:
            self.agent_nudge_state[agent_name]["last_activity"] = now
            # Reset nudge count when agent becomes active
            self.agent_nudge_state[agent_name]["nudge_count"] = 0

    # _normalize_output_for_comparison is now normalize_output_for_comparison()
    # imported from dashboard_data

    def _check_stuck_orchestrator(self, orch_output: str) -> None:
        """Detect if orchestrator is stuck in 'thinking' state with background tasks.

        If the orchestrator shows 'thinking' or 'background tasks' for too long,
        force the idle time to exceed the nudge threshold.
        """
        # Initialize stuck tracking
        if not hasattr(self, "_stuck_state_start"):
            self._stuck_state_start = None
            self._stuck_state_type = None

        # Grace period: don't run stuck detection in the first 5 minutes
        # after dashboard start.  This prevents false positives when starting
        # with stale tmux output from a previous session.
        if not hasattr(self, "_dashboard_start_time"):
            self._dashboard_start_time = time.time()
        if time.time() - self._dashboard_start_time < 300:
            return

        # Detect stuck patterns
        is_thinking = "thinking" in orch_output.lower()
        has_background_tasks = "background task" in orch_output.lower()

        # Only look for elapsed time in Claude's status line format, not
        # arbitrary numbers followed by 'm'.  The status line typically
        # shows something like "⏳ 6m 7s" or "elapsed: 12m 34s".
        elapsed_seconds = 0
        # Match "Nm Ns" or "Nm" only when preceded by whitespace/start or
        # a typical separator (colon, dash) to avoid matching inside words.
        elapsed_match = re.search(
            r"(?:^|[\s:⏳▸>-])\s*(\d+)m\s*(\d+)?s?\b", orch_output
        )
        if elapsed_match:
            minutes = int(elapsed_match.group(1))
            seconds = int(elapsed_match.group(2)) if elapsed_match.group(2) else 0
            elapsed_seconds = minutes * 60 + seconds

        # Check for hour format too (e.g., "1h 23m")
        hour_match = re.search(r"(?:^|[\s:⏳▸>-])\s*(\d+)h\s*(\d+)?m?\b", orch_output)
        if hour_match:
            hours = int(hour_match.group(1))
            minutes = int(hour_match.group(2)) if hour_match.group(2) else 0
            elapsed_seconds = hours * 3600 + minutes * 60

        # Stuck threshold: 3 minutes of "thinking" with background tasks
        STUCK_THRESHOLD = 180  # 3 minutes

        if (is_thinking or has_background_tasks) and elapsed_seconds > STUCK_THRESHOLD:
            # Force idle state to trigger nudge
            if self._stuck_state_start is None:
                self._stuck_state_start = time.time()
                self._stuck_state_type = (
                    "thinking_with_bg_tasks"
                    if has_background_tasks
                    else "long_thinking"
                )
                self._log_activity(
                    f"Detected stuck orchestrator: {self._stuck_state_type} for {elapsed_seconds}s"
                )

            # Force the last_activity_time back to trigger nudge
            # Set it to be older than nudge_interval
            self.last_activity_time = time.time() - self.nudge_interval - 10
        else:
            # Reset stuck tracking when output looks normal
            if self._stuck_state_start is not None:
                self._log_activity("Orchestrator no longer appears stuck")
            self._stuck_state_start = None
            self._stuck_state_type = None

    def _check_user_input_activity(self) -> None:
        """Detect if the user is actively typing into the orchestrator pane.

        Captures the cursor line from the tmux pane.  If the line looks like a
        Claude Code input prompt (starts with '>') with user-typed content, or
        if the cursor-line content changed since the last check, treat the
        orchestrator as active and reset the idle timer.
        """
        try:
            # Capture just the cursor line (line 0 = cursor position)
            result = subprocess.run(
                [
                    "tmux",
                    "capture-pane",
                    "-t",
                    self.orchestrator_pane,
                    "-p",
                    "-S",
                    "0",
                    "-E",
                    "0",
                ],
                capture_output=True,
                text=True,
                timeout=3,
            )
            if result.returncode != 0:
                return
            cursor_line = result.stdout.strip()
        except Exception:
            return

        # Initialise tracking state on first call
        if not hasattr(self, "_last_cursor_line"):
            self._last_cursor_line: str = cursor_line
            return

        # Heuristic 1: cursor line changed → user is typing or Claude is
        # producing output at the cursor position.
        if cursor_line != self._last_cursor_line:
            self.last_activity_time = time.time()
            self._last_cursor_line = cursor_line
            return

        # Heuristic 2: cursor line looks like a prompt with user input.
        # Claude Code prompts: "> some text", "❯ some text", "… some text"
        # A bare ">" with no text means waiting, not active.
        stripped = cursor_line.lstrip()
        for prefix in (">", "❯", "…"):
            if stripped.startswith(prefix):
                after_prompt = stripped[len(prefix) :].strip()
                if after_prompt:
                    # User has typed something at the prompt
                    self.last_activity_time = time.time()
                    break

        self._last_cursor_line = cursor_line

    def should_nudge_agent(self, agent: dict) -> bool:
        """Check if a specific agent needs nudging."""
        agent_name = agent.get("name", "")
        status = agent.get("status", AgentStatus.UNKNOWN)

        # Never nudge completed agents — FREE + "completed" means done, not idle
        if (
            agent.get("status_detail") == "completed"
            or agent_name in self.completed_agents
        ):
            return False

        # Only nudge idle or needs_input agents (never completed)
        if status not in [AgentStatus.FREE, AgentStatus.NEEDS_INPUT]:
            return False
        if status == AgentStatus.COMPLETED:
            return False

        # Get or create nudge state
        if agent_name not in self.agent_nudge_state:
            self.agent_nudge_state[agent_name] = {
                "last_activity": time.time(),
                "last_nudge": 0,
                "nudge_count": 0,
            }

        state = self.agent_nudge_state[agent_name]

        # Check if max nudges exceeded
        if state["nudge_count"] >= self.agent_max_nudges:
            return False

        now = time.time()
        idle_time = now - state["last_activity"]
        cooldown_elapsed = now - state["last_nudge"] > self.agent_nudge_interval

        return idle_time >= self.agent_nudge_interval and cooldown_elapsed

    def nudge_agent(self, agent: dict, task_context: str = "") -> bool:
        """Send a nudge to a specific worker agent via the message queue."""
        agent_name = agent.get("name", "")
        agent_window = agent.get("window", f"{self.tmux_session}:{agent_name}")

        # Get nudge state
        state = self.agent_nudge_state.get(agent_name, {"nudge_count": 0})
        nudge_count = state.get("nudge_count", 0)

        # Nudge messages for worker agents - encourage autonomous progress
        messages = [
            "If you're waiting for clarification, use your best judgment and continue. Make reasonable assumptions and proceed.",
            "Don't wait for input - you have autonomy to make decisions. Keep working and adjust if needed later.",
            "Continue with your task. If you're stuck, try a different approach rather than waiting.",
            "Make a decision and proceed. You can always course-correct, but don't block on missing info.",
            "Finish your task autonomously. Signal TASK_COMPLETE when done - don't wait for confirmation.",
        ]

        message = messages[nudge_count % len(messages)]
        if task_context:
            message = f"{message} - {task_context}"

        try:
            # Queue for targeted delivery to the agent's tmux pane
            self.queue_message(
                target=agent_window,
                content=message,
                priority=4,
                source="nudge-agent",
            )

            now = time.time()
            self.agent_nudge_state[agent_name] = {
                "last_activity": state.get("last_activity", now),
                "last_nudge": now,
                "nudge_count": nudge_count + 1,
            }

            # Log event for visibility
            if EVENT_LOG_AVAILABLE:
                idle_time = int(now - state.get("last_activity", now)) // 60
                log_event(
                    self.project,
                    "nudge_agent",
                    f"{agent_name} nudged ({nudge_count + 1}/{self.agent_max_nudges}) -- idle {idle_time}m",
                )

            return True
        except Exception as e:
            self._log_activity(f"Agent nudge error for {agent_name}: {e}")
            return False

    def nudge_sub_orchestrator(self, sub_orch: dict) -> bool:
        """Send a nudge to a sub-orchestrator via the message queue."""
        sub_id = sub_orch.get("id", "")
        sub_window = sub_orch.get("window", f"{self.tmux_session}:[{sub_id}] orch")

        # Get nudge state (use sub_orch ID as key)
        state_key = f"sub-orch-{sub_id}"
        state = self.agent_nudge_state.get(state_key, {"nudge_count": 0})
        nudge_count = state.get("nudge_count", 0)

        if nudge_count >= self.agent_max_nudges:
            return False

        messages = [
            "Use your best judgment and continue orchestrating. Don't wait for input - make decisions autonomously.",
            "Keep your subtree moving. If you need clarification, make reasonable assumptions and proceed.",
            "Continue coordinating your work. You have autonomy to make decisions - adjust later if needed.",
            "Don't block on missing information. Use your judgment and keep the workflow progressing.",
        ]

        message = messages[nudge_count % len(messages)]

        try:
            # Queue for targeted delivery to the sub-orchestrator's tmux pane
            self.queue_message(
                target=sub_window,
                content=message,
                priority=4,
                source="nudge-sub-orch",
            )

            now = time.time()
            self.agent_nudge_state[state_key] = {
                "last_activity": state.get("last_activity", now),
                "last_nudge": now,
                "nudge_count": nudge_count + 1,
            }
            # Log event for visibility
            if EVENT_LOG_AVAILABLE:
                idle_time = int(now - state.get("last_activity", now)) // 60
                log_event(
                    self.project,
                    "nudge_agent",
                    f"Sub-orchestrator '{sub_id}' nudged ({nudge_count + 1}/{self.agent_max_nudges}) -- idle {idle_time}m",
                )
            return True
        except Exception as e:
            self._log_activity(f"Sub-orchestrator nudge error for {sub_id}: {e}")
            return False

    def nudge_all_stalled(
        self, agents: list[dict], tasks: list[dict], state: dict
    ) -> int:
        """Nudge all stalled agents, sub-orchestrators, and orchestrator.

        Returns the number of nudges sent.
        """
        nudges_sent = 0
        now = time.time()

        # 0. Check if orchestrator nudges are exhausted -> trigger restart
        if self.nudge_count >= self.max_nudges:
            if self.restart_count < self.max_restarts:
                self._log_activity(
                    f"Orchestrator nudges exhausted ({self.nudge_count}/{self.max_nudges}). "
                    "Triggering auto-restart."
                )
                self.restart_orchestrator(agents, tasks)
                return 0  # Skip further nudging this cycle
            # else: max restarts exceeded, fall through (no more restarts)

        # 1. Nudge orchestrator if needed (existing logic)
        if self.should_nudge(agents, tasks):
            if self.nudge_orchestrator(agents, tasks):
                nudges_sent += 1

        # 2. Nudge stalled worker agents (or restart if max nudges hit)
        # Build set of agent names whose tasks are already completed in tasks.json
        completed_task_agents: set[str] = set()
        for task in tasks:
            if task.get("status") == "completed":
                for field in ("name", "agent_window", "owner", "assigned_to"):
                    val = task.get(field)
                    if val:
                        completed_task_agents.add(val)

        for agent in agents:
            agent_name = agent.get("name", "")
            status = agent.get("status", AgentStatus.UNKNOWN)

            # Skip completed agents entirely — they're done, not stalled
            if (
                agent.get("status_detail") == "completed"
                or agent_name in self.completed_agents
                or agent_name in completed_task_agents
            ):
                continue

            # Update activity tracking
            is_active = status == AgentStatus.WORKING
            self.update_agent_activity(agent_name, is_active)

            # Check if agent has exhausted nudges -> trigger restart
            agent_state = self.agent_nudge_state.get(agent_name, {})
            if agent_state.get("nudge_count", 0) >= self.agent_max_nudges:
                agent_restarts = self.agent_restart_count.get(agent_name, 0)
                if agent_restarts < self.agent_max_restarts:
                    # Find task context for this agent
                    task_context = ""
                    for task in tasks:
                        owner = task.get("owner") or task.get("assigned_to") or ""
                        if owner and owner in agent_name:
                            task_context = (
                                task.get("title") or task.get("subject") or ""
                            )
                            break
                    self.restart_agent(agent, task_context)
                elif agent_restarts == self.agent_max_restarts:
                    # Max restarts exhausted - write stall file and log event
                    self._handle_agent_stall(agent_name)
                continue

            # Check if needs nudging
            if self.should_nudge_agent(agent):
                # Find task context for this agent
                task_context = ""
                for task in tasks:
                    owner = task.get("owner") or task.get("assigned_to") or ""
                    if owner and owner in agent_name:
                        task_context = task.get("title") or task.get("subject") or ""
                        break

                if self.nudge_agent(agent, task_context):
                    nudges_sent += 1

        # 3. Nudge stalled sub-orchestrators
        sub_orchestrators = state.get("sub_orchestrators", [])
        for sub in sub_orchestrators:
            sub_id = sub.get("id", "")
            sub_window = sub.get("window", f"sub-{sub_id}")
            state_key = f"sub-orch-{sub_id}"

            # Check if sub-orchestrator window exists and is idle
            output = tmux_capture_pane(f"{self.tmux_session}:{sub_window}", 50)
            if not output:
                continue

            sub_status = detect_agent_status(output)
            is_active = sub_status == AgentStatus.WORKING
            self.update_agent_activity(state_key, is_active)

            # Check if needs nudging
            if state_key not in self.agent_nudge_state:
                self.agent_nudge_state[state_key] = {
                    "last_activity": now,
                    "last_nudge": 0,
                    "nudge_count": 0,
                }

            sub_state = self.agent_nudge_state[state_key]
            idle_time = now - sub_state["last_activity"]
            cooldown_ok = now - sub_state["last_nudge"] > self.agent_nudge_cooldown
            under_limit = sub_state["nudge_count"] < self.agent_max_nudges

            if (
                not is_active
                and idle_time >= self.agent_nudge_interval
                and cooldown_ok
                and under_limit
            ):
                if self.nudge_sub_orchestrator(sub):
                    nudges_sent += 1

        return nudges_sent

    def restart_agent(self, agent: dict, task_context: str = "") -> bool:
        """Kill and restart a stalled agent window.

        Returns True if restart was initiated successfully.
        """
        agent_name = agent.get("name", "")
        window = agent.get("window", agent_name)
        pane = f"{self.tmux_session}:{window}"
        restarts = self.agent_restart_count.get(agent_name, 0)

        self._log_activity(
            f"RESTART: Initiating agent '{agent_name}' restart "
            f"(attempt {restarts + 1}/{self.agent_max_restarts})"
        )

        try:
            # Step 1: Send Ctrl+C to gracefully stop
            subprocess.run(
                ["tmux", "send-keys", "-t", pane, "C-c"],
                capture_output=True,
                timeout=5,
            )
            time.sleep(2)

            # Step 2: Send /exit in case Claude is still running
            subprocess.run(
                ["tmux", "send-keys", "-t", pane, "C-c"],
                capture_output=True,
                timeout=5,
            )
            time.sleep(1)
            subprocess.run(
                ["tmux", "send-keys", "-t", pane, "/exit", "Enter"],
                capture_output=True,
                timeout=5,
            )
            time.sleep(2)

            # Step 3: Get working directory from state
            state_data = self.load_state()
            working_dir = state_data.get("working_dir", str(self.project_dir))

            # Step 4: Re-launch Claude in the agent window
            launch_cmd = f"cd {working_dir} && claude --dangerously-skip-permissions"
            subprocess.run(
                ["tmux", "send-keys", "-t", pane, launch_cmd, "Enter"],
                capture_output=True,
                timeout=5,
            )

            # Step 5: Wait for Claude to start
            max_wait = 30
            claude_ready = False
            for _ in range(max_wait):
                time.sleep(1)
                pane_content = tmux_capture_pane(pane, 20)
                if any(
                    indicator in pane_content
                    for indicator in ["❯", "bypass permissions", 'Try "edit']
                ):
                    claude_ready = True
                    break

            if not claude_ready:
                self._log_activity(
                    f"RESTART: Agent '{agent_name}' may not have started properly"
                )

            # Step 6: Send task context to the new session
            context_msg = (
                f'You are a Claw Town agent for project "{self.project}". '
                "Your previous session became unresponsive and was automatically restarted "
                f"(restart #{restarts + 1}). "
            )
            if task_context:
                context_msg += f"\n\nYour task: {task_context}\n"
            context_msg += (
                "\nResume your work. Signal TASK_COMPLETE when done. "
                "Do NOT wait for input - use your judgment."
            )
            time.sleep(3)
            tmux_send_keys(pane, context_msg)

            # Step 7: Update state
            self.agent_restart_count[agent_name] = restarts + 1
            # Clear sticky completion — agent is being reassigned
            self.completed_agents.discard(agent_name)
            if hasattr(self, "_agent_task_complete_seen"):
                self._agent_task_complete_seen.discard(agent_name)
            # Reset nudge state for this agent
            self.agent_nudge_state[agent_name] = {
                "last_activity": time.time(),
                "last_nudge": 0,
                "nudge_count": 0,
            }

            # Log event
            if EVENT_LOG_AVAILABLE:
                log_event(
                    self.project,
                    "restart_agent",
                    f"{agent_name} restarted ({restarts + 1}/{self.agent_max_restarts})",
                )

            self._log_activity(
                f"RESTART: Agent '{agent_name}' restarted successfully "
                f"(restart #{restarts + 1})"
            )
            return True

        except Exception as e:
            self._log_activity(
                f"RESTART ERROR: Failed to restart agent '{agent_name}': {e}"
            )
            return False

    def _handle_agent_stall(self, agent_name: str) -> None:
        """Handle an agent that has exhausted all restart attempts.

        Logs the stall event and bumps the restart counter.
        """
        restarts = self.agent_restart_count.get(agent_name, 0)

        self._log_activity(
            f"STALLED: Agent '{agent_name}' unresponsive after {restarts} restarts. "
            "Manual intervention required."
        )

        if EVENT_LOG_AVAILABLE:
            log_event(
                self.project,
                "agent_stall",
                f"{agent_name} unresponsive after {restarts} restarts",
            )

        # Bump restart count past max so we don't keep re-entering this path
        self.agent_restart_count[agent_name] = restarts + 1

    # ─────────────────────────────────────────────────────────────────────────
    # Agent Health Checks (ported from self-repair)
    # ─────────────────────────────────────────────────────────────────────────

    def check_agent_health(
        self, agents: list[dict], tasks: list[dict], state: dict
    ) -> None:
        """Check agent health: dead windows, errors, implicit completions.

        Runs each tick alongside nudge_all_stalled to detect and recover from
        issues that the nudge system doesn't cover.
        """
        # Initialize tracking state on first call
        if not hasattr(self, "_health_error_seen"):
            self._health_error_seen: set[str] = set()
        if not hasattr(self, "_health_completion_prompted"):
            self._health_completion_prompted: set[str] = set()
        if not hasattr(self, "_health_respawn_cooldown"):
            self._health_respawn_cooldown: dict[str, float] = {}

        live_window_names = {a.get("name", "") for a in agents}

        # 1. Dead window detection — check in-progress tasks with assigned agents
        for task in tasks:
            status = task.get("status", "")
            if status != "in_progress":
                continue
            # Skip orchestrator task
            if task.get("type") == "orchestrator":
                continue

            agent_window = (
                task.get("agent_window")
                or task.get("owner")
                or task.get("assigned_to")
                or ""
            )
            if not agent_window:
                continue

            # Normalize: agent_window may be "session:window" but live names
            # are just the window part. Compare using window name only.
            window_name = (
                agent_window.split(":")[-1] if ":" in agent_window else agent_window
            )

            # Check if window is alive
            if window_name not in live_window_names:
                # Respect restart cap before attempting
                restarts = self.agent_restart_count.get(agent_window, 0)
                if restarts >= self.agent_max_restarts:
                    continue

                # Cooldown: don't respawn the same window too rapidly
                last_respawn = self._health_respawn_cooldown.get(agent_window, 0)
                if time.time() - last_respawn < 120:
                    continue

                self._log_activity(
                    f"HEALTH: Dead window detected for task '{task.get('id', '')}' "
                    f"(window '{agent_window}' missing). Respawning."
                )
                self._health_respawn_cooldown[agent_window] = time.time()

                # Build a minimal agent dict for restart_agent
                agent_dict = {"name": agent_window, "window": agent_window}
                task_context = (
                    task.get("title") or task.get("subject") or task.get("name", "")
                )
                self.restart_agent(agent_dict, task_context)

                if EVENT_LOG_AVAILABLE:
                    log_event(
                        self.project,
                        "restart_agent",
                        f"Dead window '{agent_window}' respawned for task {task.get('id', '')}",
                    )

        # 2. Error recovery — check agent output for error patterns
        error_patterns = [
            r"Traceback \(most recent call last\)",
            r"Exception:",
            r"FAILED",
            r"panic:",
            r"error\[E",
        ]
        error_re = re.compile("|".join(error_patterns), re.IGNORECASE)

        for agent in agents:
            agent_name = agent.get("name", "")
            status = agent.get("status", "")
            last_output = agent.get("last_output", "")

            # Only check non-working, non-completed agents
            if status == AgentStatus.WORKING:
                continue
            if not last_output:
                continue

            # Skip if we already sent a recovery prompt for this agent's current error
            if agent_name in self._health_error_seen:
                continue

            match = error_re.search(last_output)
            if match:
                self._health_error_seen.add(agent_name)
                error_snippet = match.group(0)[:100]

                self._log_activity(
                    f"HEALTH: Error detected in agent '{agent_name}': {error_snippet}"
                )

                # Send recovery prompt via message queue
                try:
                    agent_window = agent.get(
                        "window", f"{self.tmux_session}:{agent_name}"
                    )
                    recovery_msg = (
                        f"An error was detected in your output: {error_snippet}\n\n"
                        "Please try one of these:\n"
                        "1. If it's a transient error, retry the operation\n"
                        "2. If it's a dependency issue, work around it\n"
                        "3. If you can't proceed, say TASK_BLOCKED: <reason>\n\n"
                        "Continue working on your task."
                    )
                    self.queue_message(
                        target=agent_window,
                        content=recovery_msg,
                        priority=2,
                        source="health-check",
                    )

                    if EVENT_LOG_AVAILABLE:
                        log_event(
                            self.project,
                            "nudge_agent",
                            f"{agent_name} error recovery: {error_snippet[:50]}",
                        )
                except Exception as e:
                    self._log_activity(
                        f"HEALTH: Failed to write error recovery for '{agent_name}': {e}"
                    )

        # 3. Implicit completion detection — agent output suggests done but no signal
        completion_patterns = [
            r"(?:done|finished|completed|success)",
            r"all (?:tests|checks) pass",
            r"implementation (?:complete|ready)",
            r"changes (?:committed|saved)",
            r"output (?:saved|written) to",
        ]
        completion_re = re.compile("|".join(completion_patterns), re.IGNORECASE)

        for agent in agents:
            agent_name = agent.get("name", "")
            status = agent.get("status", "")
            last_output = agent.get("last_output", "")

            # Only check idle agents (not working, not already completed)
            if status == AgentStatus.WORKING:
                continue
            if not last_output:
                continue
            # Already processed this agent
            if agent_name in self._health_completion_prompted:
                continue
            # Already saw TASK_COMPLETE — skip
            if "TASK_COMPLETE" in last_output:
                continue
            # Sticky completion — agent already finished its task
            if agent_name in self.completed_agents:
                continue

            # Check for completion hints + idle prompt
            if completion_re.search(last_output):
                # Also need an idle prompt indicator in the tail
                tail = last_output[-100:]
                if "❯" in tail or ">>>" in tail:
                    self._health_completion_prompted.add(agent_name)

                    self._log_activity(
                        f"HEALTH: Implicit completion detected for agent '{agent_name}'. "
                        "Prompting for explicit TASK_COMPLETE signal."
                    )

                    try:
                        agent_window = agent.get(
                            "window", f"{self.tmux_session}:{agent_name}"
                        )
                        self.queue_message(
                            target=agent_window,
                            content=(
                                "It looks like you may have finished your task. "
                                "If so, please say: TASK_COMPLETE: <summary of what you accomplished>"
                            ),
                            priority=4,
                            source="health-check",
                        )

                        if EVENT_LOG_AVAILABLE:
                            log_event(
                                self.project,
                                "nudge_agent",
                                f"{agent_name} prompted for completion signal (implicit done detected)",
                            )
                    except Exception as e:
                        self._log_activity(
                            f"HEALTH: Failed to queue completion prompt for '{agent_name}': {e}"
                        )

        # Reset error tracking when agent output changes (allows re-detection)
        for agent in agents:
            agent_name = agent.get("name", "")
            if agent_name in self._health_error_seen:
                last_output = agent.get("last_output", "")
                if not error_re.search(last_output):
                    self._health_error_seen.discard(agent_name)

    def restart_orchestrator(self, agents: list[dict], tasks: list[dict]) -> bool:
        """Kill and restart the orchestrator Claude session when nudges are exhausted.

        Returns True if restart was initiated successfully.
        """
        # Check if we've exceeded max restarts
        if self.restart_count >= self.max_restarts:
            self._log_activity(
                f"STALLED: Orchestrator unresponsive after {self.restart_count} restarts. "
                "Manual intervention required."
            )
            if EVENT_LOG_AVAILABLE:
                log_event(
                    self.project,
                    "restart_orch",
                    f"Orchestrator unresponsive after {self.restart_count} restarts - manual intervention needed",
                )
            return False

        self._log_activity(
            f"RESTART: Initiating orchestrator restart (attempt {self.restart_count + 1}/{self.max_restarts})"
        )
        if EVENT_LOG_AVAILABLE:
            log_event(
                self.project,
                "restart_orch",
                f"Orchestrator restarted ({self.restart_count + 1}/{self.max_restarts})",
            )

        try:
            # Step 1: Send Ctrl+C to gracefully stop the current session
            subprocess.run(
                ["tmux", "send-keys", "-t", self.orchestrator_pane, "C-c"],
                capture_output=True,
                timeout=5,
            )
            time.sleep(2)

            # Step 2: Send another Ctrl+C and 'exit' in case Claude is still running
            subprocess.run(
                ["tmux", "send-keys", "-t", self.orchestrator_pane, "C-c"],
                capture_output=True,
                timeout=5,
            )
            time.sleep(1)
            subprocess.run(
                ["tmux", "send-keys", "-t", self.orchestrator_pane, "/exit", "Enter"],
                capture_output=True,
                timeout=5,
            )
            time.sleep(2)

            # Step 3: Get working directory from state
            state = self.load_state()
            working_dir = state.get("working_dir", str(self.project_dir))

            # Step 4: Re-launch Claude in the orchestrator pane
            # Match how claw_town.sh launches it: cd to working dir, run claude, kill session on exit
            launch_cmd = (
                f"cd {working_dir} && claude --dangerously-skip-permissions; "
                "echo 'Claude exited. Cleaning up...'; sleep 1; "
                f"tmux kill-session -t {self.tmux_session}"
            )
            subprocess.run(
                [
                    "tmux",
                    "send-keys",
                    "-t",
                    self.orchestrator_pane,
                    launch_cmd,
                    "Enter",
                ],
                capture_output=True,
                timeout=5,
            )

            # Step 5: Wait for Claude to start
            max_wait = 30
            claude_ready = False
            for _ in range(max_wait):
                time.sleep(1)
                pane_content = tmux_capture_pane(self.orchestrator_pane, 20)
                if any(
                    indicator in pane_content
                    for indicator in ["❯", "bypass permissions", 'Try "edit']
                ):
                    claude_ready = True
                    break

            if not claude_ready:
                self._log_activity("RESTART: Claude may not have started properly")

            # Step 6: Send context to the new session about current state
            in_progress_tasks = [
                t for t in tasks if t.get("status") in ("in_progress", "open")
            ]
            task_summaries = []
            for t in in_progress_tasks[:10]:
                tid = t.get("id", "?")
                title = (
                    t.get("title") or t.get("subject") or t.get("name") or "untitled"
                )
                status = t.get("status", "unknown")
                task_summaries.append(f"  - [{tid}] {title} ({status})")

            agent_summaries = []
            for a in agents[:10]:
                name = a.get("name", "unknown")
                astatus = a.get("status", "unknown")
                agent_summaries.append(f"  - {name}: {astatus}")

            # Check for pending inbox messages
            pending_msgs = []
            if self.inbox_pending.exists():
                for msg_file in sorted(self.inbox_pending.iterdir())[:5]:
                    try:
                        content = msg_file.read_text().strip()
                        if content:
                            # Truncate long messages
                            if len(content) > 100:
                                content = content[:100] + "..."
                            pending_msgs.append(f"  - {content}")
                    except Exception:
                        pass

            context_msg = f"""You are the Claw Town orchestrator for project "{self.project}". Your previous session became unresponsive and was automatically restarted.

## Current State (restart #{self.restart_count + 1})

### Active/Open Tasks ({len(in_progress_tasks)}):
{chr(10).join(task_summaries) if task_summaries else "  (none)"}

### Running Agents ({len(agents)}):
{chr(10).join(agent_summaries) if agent_summaries else "  (none)"}

### Pending Messages ({len(pending_msgs)}):
{chr(10).join(pending_msgs) if pending_msgs else "  (none)"}

## Instructions
1. Read the full task state: cat ~/projects/{self.project}/.claw_town/tasks.json
2. Check agent status in tmux windows
3. Resume orchestration - pick up where the previous session left off
4. Use /claw-town {self.project} to load the orchestrator skill

**Keep orchestrating. Do NOT wait for input - use your judgment.**"""

            # Wait a moment for Claude to be ready, then send context
            time.sleep(3)
            tmux_send_keys(self.orchestrator_pane, context_msg)

            # Step 7: Update state
            self.restart_count += 1
            self.nudge_count = 0
            self.last_nudge_time = 0
            self.last_activity_time = time.time()

            self._log_activity(
                f"RESTART: Orchestrator restarted successfully (restart #{self.restart_count})"
            )

            return True

        except Exception as e:
            self._log_activity(f"RESTART ERROR: Failed to restart orchestrator: {e}")
            if EVENT_LOG_AVAILABLE:
                log_event(
                    self.project,
                    "restart_orch",
                    f"Orchestrator restart failed: {e}",
                )
            return False

    # ─────────────────────────────────────────────────────────────────────────
    # Keyboard Input Handling
    # ─────────────────────────────────────────────────────────────────────────

    def check_keyboard_input(self) -> None:
        """Check for keyboard input (non-blocking) to handle scrolling and message navigation.

        Uses os.read() for unbuffered fd-level reads to avoid issues with
        Python's BufferedReader / TextIOWrapper sitting between select()
        and the actual read.
        """
        import select

        _dbg = Path("/tmp/claw_dashboard_scroll_debug.log")
        fd = sys.stdin.fileno()

        # Auto-exit tmux copy mode (throttled to once per main loop cycle).
        # Copy mode intercepts ALL keystrokes, preventing the dashboard
        # from receiving any input.
        _now = time.time()
        if not hasattr(self, "_last_copy_mode_check"):
            self._last_copy_mode_check = 0.0
        if _now - self._last_copy_mode_check > 2.0:
            self._last_copy_mode_check = _now
            try:
                # Find our own pane by matching tty
                our_tty = os.ttyname(fd)
                result = subprocess.run(
                    [
                        "tmux",
                        "list-panes",
                        "-t",
                        f"{self.tmux_session}:control",
                        "-F",
                        "#{pane_tty} #{pane_mode} #{pane_id}",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=1,
                )
                if result.returncode == 0:
                    for line in result.stdout.strip().split("\n"):
                        parts = line.split()
                        if (
                            len(parts) >= 2
                            and parts[0] == our_tty
                            and "copy-mode" in parts[1]
                        ):
                            pane_id = parts[2] if len(parts) >= 3 else None
                            if pane_id:
                                subprocess.run(
                                    ["tmux", "send-keys", "-t", pane_id, "q"],
                                    capture_output=True,
                                    timeout=1,
                                )
            except Exception:
                pass

        # Check if stdin has data available (non-blocking)
        if not sys.stdin.isatty():
            return

        def _read1() -> str:
            """Read one byte from fd unbuffered, return as string.

            Returns empty string on EOF / empty read.
            """
            data = os.read(fd, 1)
            if not data:
                return ""
            return data.decode("latin-1")

        def _has_data(timeout: float = 0) -> bool:
            """Check if fd has data available."""
            return bool(select.select([fd], [], [], timeout)[0])

        try:
            # Terminal is already in cbreak mode (set in run()), so just poll.
            # Check if input is available (timeout 0 = non-blocking)
            if _has_data(0):
                char = _read1()
                if not char:
                    return  # EOF or empty read from terminal
                with open(_dbg, "a") as _f:
                    _f.write(
                        f"[{time.strftime('%H:%M:%S')}] char={repr(char)} ord={ord(char)} focused={self.focused_panel} vo={self.viewport_offset}\n"
                    )

                    # Handle escape sequences (arrow keys, page up/down, etc.)
                    if char == "\033":
                        # Read more to distinguish different escapes
                        if _has_data(0.01):
                            c2 = _read1()
                            if c2 == "[":
                                if _has_data(0.01):
                                    c3 = _read1()
                                    if c3 == "M":
                                        # X10 mouse event — drain bytes
                                        _read1()
                                        _read1()
                                        _read1()
                                    elif c3 == "<":
                                        # SGR mouse event — drain until M/m
                                        while True:
                                            if _has_data(0.01):
                                                sc = _read1()
                                                if sc in ("M", "m"):
                                                    break
                                            else:
                                                break
                                    elif c3 == "5":
                                        # Page Up: \033[5~
                                        if _has_data(0.01):
                                            _read1()  # consume '~'
                                        scroll_amount = max(1, self.term_height // 2)
                                        self.viewport_offset = max(
                                            0, self.viewport_offset - scroll_amount
                                        )
                                        self._needs_rerender = True
                                    elif c3 == "6":
                                        # Page Down: \033[6~
                                        if _has_data(0.01):
                                            _read1()  # consume '~'
                                        scroll_amount = max(1, self.term_height // 2)
                                        self.viewport_offset += scroll_amount
                                        self._needs_rerender = True
                                    elif c3 == "A":
                                        # Arrow Up
                                        self.viewport_offset = max(
                                            0, self.viewport_offset - 1
                                        )
                                        self._needs_rerender = True
                                    elif c3 == "B":
                                        # Arrow Down
                                        self.viewport_offset += 1
                                        self._needs_rerender = True
                                    else:
                                        # Other escape (arrow keys, etc.) — drain
                                        while _has_data(0.01):
                                            _read1()
                                else:
                                    pass  # Incomplete escape
                            else:
                                pass  # Alt+key or other
                        # else: bare Escape key, ignore
                        return

                    # Tab: toggle focused panel between 'tasks' and 'messages'
                    if char == "\t":
                        self.focused_panel = (
                            "messages" if self.focused_panel == "tasks" else "tasks"
                        )
                        self._needs_rerender = True

                    elif self.focused_panel == "messages":
                        # Tab switching: 1=Inbox, 2=Pending, 3=Expired
                        if char == "1":
                            if self.msg_queue_tab != "inbox":
                                self.msg_queue_tab = "inbox"
                                self.msg_selected_idx = 0
                                self.msg_expanded.clear()
                            self._needs_rerender = True
                        elif char == "2":
                            if self.msg_queue_tab != "pending":
                                self.msg_queue_tab = "pending"
                                self.msg_selected_idx = 0
                                self.msg_expanded.clear()
                            self._needs_rerender = True
                        elif char == "3":
                            if self.msg_queue_tab != "expired":
                                self.msg_queue_tab = "expired"
                                self.msg_selected_idx = 0
                                self.msg_expanded.clear()
                            self._needs_rerender = True
                        else:
                            # Message panel navigation
                            if self.msg_queue_tab == "inbox":
                                messages = self.get_inbox_messages()
                            elif self.msg_queue_tab == "pending":
                                messages = self.get_outbox_pending_messages()
                            else:
                                messages = self.get_outbox_expired_messages()
                            max_idx = max(0, len(messages) - 1) if messages else 0

                            if char == "j":
                                # Move selection down
                                self.msg_selected_idx = min(
                                    self.msg_selected_idx + 1, max_idx
                                )
                                self._needs_rerender = True
                            elif char == "k":
                                # Move selection up
                                self.msg_selected_idx = max(
                                    self.msg_selected_idx - 1, 0
                                )
                                self._needs_rerender = True
                            elif char == "\r" or char == "\n" or char == "e":
                                # Toggle expand/collapse of selected message
                                if messages:
                                    idx = self.msg_selected_idx
                                    if idx in self.msg_expanded:
                                        self.msg_expanded.discard(idx)
                                    else:
                                        self.msg_expanded.add(idx)
                                    self._needs_rerender = True
                            elif char == "E":
                                # Toggle expand/collapse all messages
                                if messages:
                                    all_indices = set(range(len(messages)))
                                    if self.msg_expanded >= all_indices:
                                        # All expanded -> collapse all
                                        self.msg_expanded.clear()
                                    else:
                                        # Some collapsed -> expand all
                                        self.msg_expanded = all_indices
                                    self._needs_rerender = True

                    else:
                        # Tasks panel: j/k scroll the viewport
                        if char == "j" or char == "J":
                            # Scroll viewport down
                            self.viewport_offset += 3
                            self._needs_rerender = True
                        elif char == "k" or char == "K":
                            # Scroll viewport up
                            self.viewport_offset = max(0, self.viewport_offset - 3)
                            self._needs_rerender = True
                        elif char == "g":
                            # Go to top
                            self.viewport_offset = 0
                            self._needs_rerender = True
                        elif char == "G":
                            # Go to bottom
                            self.viewport_offset = max(
                                0,
                                self._total_content_lines - self.term_height + 1,
                            )
                            self._needs_rerender = True

        except Exception as _ex:
            with open(_dbg, "a") as _f:
                _f.write(f"  -> EXCEPTION: {type(_ex).__name__}: {_ex}\n")

    # ─────────────────────────────────────────────────────────────────────────
    # Dynamic Orchestrator Blocking & Native Task Sync
    # ─────────────────────────────────────────────────────────────────────────

    def ensure_orchestrator_blocked_by(self, state: dict[str, Any]) -> bool:
        """Ensure the orchestrator task's blocked_by list includes ALL non-orchestrator tasks.

        This makes the orchestrator dynamically wait on all child tasks without
        needing manual updates when new tasks are added.

        Returns True if tasks.json was modified and saved.
        """
        tasks = state.get("tasks", [])
        if not tasks:
            return False

        # Find the orchestrator task
        orch_task = None
        orch_idx = -1
        for i, t in enumerate(tasks):
            if t.get("type") == "orchestrator":
                orch_task = t
                orch_idx = i
                break

        if orch_task is None:
            return False

        # Collect all non-orchestrator task IDs
        non_orch_ids = set()
        for t in tasks:
            if t.get("type") != "orchestrator" and t.get("id"):
                non_orch_ids.add(t["id"])

        # Get current blocked_by
        current_blocked = set(orch_task.get("blocked_by", []))

        # Check if update is needed (non-orchestrator tasks missing from blocked_by)
        if non_orch_ids == current_blocked:
            return False

        # Update blocked_by to include all non-orchestrator task IDs
        # Sort for deterministic ordering
        orch_task["blocked_by"] = sorted(
            non_orch_ids, key=lambda x: int(x) if x.isdigit() else x
        )
        state["tasks"][orch_idx] = orch_task

        # Sync change back to known_tasks if present
        if "known_tasks" in state:
            orch_key = orch_task.get("id")
            if orch_key and orch_key in state["known_tasks"]:
                state["known_tasks"][orch_key]["blocked_by"] = orch_task["blocked_by"]

        # Save back to tasks.json (exclude synthetic 'tasks' list)
        try:
            save_data = (
                {k: v for k, v in state.items() if k != "tasks"}
                if "known_tasks" in state
                else state
            )
            with open(self.tasks_file, "w") as f:
                json.dump(save_data, f, indent=2)
            return True
        except IOError:
            return False

    # ─────────────────────────────────────────────────────────────────────────
    # Agent Task Completion Detection
    # ─────────────────────────────────────────────────────────────────────────

    def detect_agent_task_complete(self, agents: list[dict], tasks: list[dict]) -> None:
        """Detect completion signals in agent output and notify orchestrator.

        When an agent signals completion (TASK_COMPLETE or 'Task complete.' in
        its output), sends a close-out message to the orchestrator with the
        exact commands to run and logs the event.
        """
        if not hasattr(self, "_agent_task_complete_seen"):
            self._agent_task_complete_seen: set[str] = set()

        for agent in agents:
            agent_name = agent.get("name", "")
            last_output = agent.get("last_output", "")

            # Check if agent just signaled completion (case-insensitive)
            last_output_lower = last_output.lower()
            if not any(
                signal in last_output_lower
                for signal in [
                    "task_complete",
                    "task complete",
                    "taskcomplete",
                ]
            ):
                continue

            # Skip if we already processed this agent's completion
            if agent_name in self._agent_task_complete_seen:
                continue

            self._agent_task_complete_seen.add(agent_name)

            # Record sticky completion so status persists after buffer scrolls
            self.completed_agents.add(agent_name)

            # Find the task associated with this agent
            task_id = ""
            task_title = ""
            for task in tasks:
                owner = task.get("owner") or task.get("assigned_to") or ""
                if owner and owner in agent_name:
                    task_id = task.get("id", "")
                    task_title = (
                        task.get("title") or task.get("subject") or task.get("name", "")
                    )
                    break

            # Also check tasks.json known_tasks by agent name
            if not task_id:
                state = self.load_state()
                for t_num, t_data in state.get("known_tasks", {}).items():
                    if t_data.get("name") == agent_name:
                        task_id = t_num
                        task_title = t_data.get("title", "")
                        break

            if not task_id:
                task_id = agent_name  # Fallback to agent name

            # Send close-out message to orchestrator
            tasklib = f"python3 {os.path.expanduser('~')}/.claude/skills/claw-town/scripts/claw_town_tasklib.py"
            tjson = f"python3 {os.path.expanduser('~')}/.claude/skills/claw-town/scripts/claw_town_tasks_json.py"
            message = (
                f"📋 **Agent '{agent_name}' completed task {task_id}**"
                f"{f': {task_title}' if task_title else ''}\n\n"
                "Run the close-out sequence:\n"
                f"1. Read findings: `{tasklib} comments {task_id} --prefix FINDINGS`\n"
                f"2. Close remote task: `{tasklib} close {task_id}`\n"
                f"3. Update tasks.json: `{tjson} {self.project} update {task_id} --status completed`\n"
                "4. Check for unblocked downstream tasks and spawn them\n"
            )

            self.queue_message(
                target=self.orchestrator_pane,
                content=message,
                priority=2,
                source="task-completion",
            )

            self._log_activity(
                f"COMPLETION: {task_id} ({agent_name}) detected. "
                "Close-out message sent to orchestrator."
            )

            # Log event
            if EVENT_LOG_AVAILABLE:
                summary = f"Task {task_id} completed"
                details = f"Agent: {agent_name}"
                if task_title:
                    details += f"\nTitle: {task_title}"
                log_event(
                    self.project,
                    "task_complete",
                    summary,
                    details=details,
                )

            self._log_activity(
                f"Task completion detected: {task_id} by agent '{agent_name}'"
            )

    # ─────────────────────────────────────────────────────────────────────────
    # Main Loop
    # ─────────────────────────────────────────────────────────────────────────

    def run(self, refresh_interval: float = 2.0) -> None:
        """Run the interactive dashboard."""

        # Set up signal handlers for graceful shutdown.
        # Signal handlers must not do I/O (stdout.write/flush) because
        # the signal may fire while stdout is mid-write, causing
        # "reentrant call inside BufferedWriter". Instead, set a flag
        # and let the main loop handle cleanup.
        self._shutdown_requested = False

        def signal_handler(signum, frame):
            self._shutdown_requested = True

        signal.signal(signal.SIGTERM, signal_handler)
        signal.signal(signal.SIGHUP, signal_handler)

        # Enable alternate screen buffer
        setup_terminal()

        # Enter cbreak mode once for the entire dashboard session.
        # This prevents keystrokes from echoing and ensures mouse escape
        # sequences are captured rather than leaking to stdout.
        self._old_term_settings = None
        if sys.stdin.isatty():
            self._old_term_settings = termios.tcgetattr(sys.stdin)
            tty.setcbreak(sys.stdin.fileno())

        # Enable mouse tracking so scroll events reach this pane
        # even when it's not the focused tmux pane.
        sys.stdout.write("\033[?1000h\033[?1002h\033[?1006h")
        sys.stdout.flush()

        _crash_log = "/tmp/claw_dashboard_crash.log"

        try:
            while True:
                try:
                    # Check for deferred signal-based shutdown
                    if self._shutdown_requested:
                        self.cleanup_session()
                        sys.exit(0)

                    # Check for keyboard input (scrolling)
                    self.check_keyboard_input()

                    # Process outbox queue first (centralized message delivery)
                    self.process_outbox()

                    # Load state and agents
                    state = self.load_state()
                    tasks = state.get("tasks", [])
                    agents = self.get_live_agents()

                    # Dynamic orchestrator blocking was removed — the sync's
                    # walk_dag is the source of truth for blocked_by fields.
                    # See: ensure_orchestrator_blocked_by (kept but no longer called)

                    # Detect task completions and emit learning events
                    self.detect_task_completions(tasks)

                    # Detect TASK_COMPLETE in agent output and notify orchestrator
                    self.detect_agent_task_complete(agents, tasks)

                    # Detect externally added tasks and notify orchestrator
                    self.detect_new_tasks(tasks)

                    # Determine if project is idle (no active tasks, no agents)
                    active_tasks = [
                        t
                        for t in tasks
                        if t.get("status") not in ("completed", "cancelled")
                        and t.get("id") != "orchestrator"
                    ]
                    is_idle = len(active_tasks) == 0 and len(agents) == 0

                    # Check for activity (orchestrator output changed)
                    # Strip dynamic content (timers, spinners, token counts) before comparing
                    orch_output = tmux_capture_pane(self.orchestrator_pane, 20)
                    normalized_output = normalize_output_for_comparison(orch_output)
                    if (
                        hasattr(self, "_last_orch_output_normalized")
                        and normalized_output != self._last_orch_output_normalized
                    ):
                        self.last_activity_time = time.time()
                    self._last_orch_output_normalized = normalized_output
                    self._last_orch_output = orch_output

                    # Also check if user is actively typing input into the
                    # orchestrator pane.  When Claude Code is waiting for input
                    # the cursor sits on a ">" prompt line.  If that line has
                    # content after the prompt (user is typing), or the content
                    # changes between checks, treat it as activity.
                    self._check_user_input_activity()

                    # Detect stuck state: "thinking" for too long with background tasks
                    self._check_stuck_orchestrator(orch_output)

                    # Auto-nudge all stalled agents (orchestrator, workers, sub-orchestrators)
                    if not is_idle:
                        self.nudge_all_stalled(agents, tasks, state)

                    # Agent health checks: dead windows, errors, implicit completions
                    if not is_idle:
                        self.check_agent_health(agents, tasks, state)

                    # Periodic checkpoint creation for session resilience
                    # When idle, reduce checkpoint frequency to every 5 minutes
                    if is_idle:
                        idle_checkpoint_interval = 300  # 5 minutes
                        if (
                            time.time() - self.last_checkpoint_time
                            >= idle_checkpoint_interval
                        ):
                            self.check_and_create_checkpoint(tasks)
                    else:
                        self.check_and_create_checkpoint(tasks)

                    # Continuous activity logging for detailed notes
                    if not is_idle:
                        self.log_agent_activity(agents, tasks, state)

                    # Self-learning: analyze orchestrator for mistakes and update skills
                    if not is_idle:
                        self.check_and_run_self_learn()

                    # Task sync: periodically discover new/changed tasks
                    self.check_and_run_task_sync()

                    # Render
                    self.render()
                    self._needs_rerender = False

                    # Sleep with responsive polling
                    # Instead of one long sleep, do 10 iterations of 200ms sleep
                    # checking for keyboard input on each iteration
                    for _ in range(int(refresh_interval / 0.2)):
                        time.sleep(0.2)
                        self.check_keyboard_input()
                        if self._needs_rerender:
                            break

                except (KeyboardInterrupt, SystemExit):
                    raise
                except Exception as _loop_exc:
                    import traceback

                    try:
                        with open(_crash_log, "a") as _cf:
                            _cf.write(
                                f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] "
                                f"Non-fatal error in main loop:\n"
                            )
                            traceback.print_exc(file=_cf)
                            _cf.write("\n")
                    except Exception:
                        pass
                    time.sleep(1)

        except KeyboardInterrupt:
            self.cleanup_session()

    def cleanup_session(self) -> None:
        """Clean up the Claw Town session, including killing the tmux session."""
        # Restore terminal settings (exit cbreak mode) before anything else
        if getattr(self, "_old_term_settings", None) is not None:
            try:
                termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self._old_term_settings)
            except Exception:
                pass
        # Disable mouse tracking and restore normal screen buffer
        sys.stdout.write("\033[?1006l\033[?1002l\033[?1000l")
        sys.stdout.flush()
        # Restore normal screen buffer
        cleanup_terminal()

        print(f"\n{T.DIM}Stopping Claw Town session...{T.RESET}")

        # Create a final checkpoint before shutdown
        try:
            state = self.load_state()
            tasks = state.get("tasks", [])
            if tasks:
                print(f"{T.DIM}  Creating final checkpoint...{T.RESET}")
                self.check_and_create_checkpoint(tasks, force=True)
        except Exception:
            pass

        self._kill_tmux_session()

        print(f"{T.DIM}Claw Town session stopped.{T.RESET}")

    def _kill_tmux_session(self) -> None:
        """Kill the tmux session with retries and fallback."""
        for attempt in range(3):
            try:
                result = subprocess.run(
                    ["tmux", "kill-session", "-t", self.tmux_session],
                    capture_output=True,
                    text=True,
                    timeout=5,
                )
                if result.returncode == 0:
                    print(
                        f"{T.GREEN}✓{T.RESET} Tmux session '{self.tmux_session}' terminated"
                    )
                    return
                # Check if session doesn't exist (already killed)
                if (
                    "no server running" in result.stderr
                    or "session not found" in result.stderr.lower()
                ):
                    return
            except subprocess.TimeoutExpired:
                print(f"{T.YELLOW}!{T.RESET} Tmux kill attempt {attempt + 1} timed out")
            except Exception as e:
                print(f"{T.YELLOW}!{T.RESET} Kill attempt {attempt + 1} failed: {e}")

        # Forcefully kill all windows in the session as fallback
        try:
            subprocess.run(
                ["tmux", "kill-window", "-t", f"{self.tmux_session}:"],
                capture_output=True,
                timeout=3,
            )
        except Exception:
            pass


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <project-name>")
        sys.exit(1)

    project = sys.argv[1]
    dashboard = ClawTownDashboard(project)
    dashboard.run()


if __name__ == "__main__":
    main()
