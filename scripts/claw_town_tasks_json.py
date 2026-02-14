#!/usr/bin/env python3
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
# pyre-strict
"""
Claw Town tasks.json Manager — Cache + Operational Tracker

This module manages the tasks.json file which serves as both:
1. A local CACHE of remote task entity state (title, status, blocked_by)
2. An OPERATIONAL tracker for agent assignments and lifecycle

The remote Tasks API is the source of truth for cached fields. Sync overwrites
them without question. Operational fields are local-only and never come from remote.

T-number is the canonical ID everywhere — no numeric IDs.

Schema:
{
    "project": "my-project",
    "root_task": "T253894461",
    "gsd_url": null,
    "gsd_project_id": null,
    "known_tasks": {
        "T253894467": {
            "title": "Research auth patterns",
            "status": "working",
            "blocked_by": ["T253894461"],
            "name": "research-auth",
            "agent_window": "claw-town-imp:research-auth",
            "created_by": "claw-town",
            "last_synced": "2026-02-08T18:00:00Z"
        }
    },
    "last_dag_walk": null,
    "last_gsd_poll": null
}

Field categories:
  Cached (synced from remote, overwritten by sync):
    - title: Task title from remote
    - blocked_by: List of T-numbers that block this task

  Operational (local only, never from remote):
    - status: Agent lifecycle state (working, completed, pending, etc.)
    - name: Short name for agent window naming
    - agent_window: tmux window name (e.g. "claw-town-imp:research-auth")
    - created_by: Who created the local tracking entry

  Metadata:
    - last_synced: ISO timestamp of last sync that touched this entry
"""

from __future__ import annotations

import fcntl
import json
import os
import re
import sys
import tempfile
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Generator, Literal

# Allow importing from the same directory
_SCRIPTS_DIR = str(Path(__file__).parent)
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

# Valid status values for a known task
AgentState = Literal[
    "orchestrator",  # The root orchestration task
    "working",  # Agent is actively working
    "idle",  # Agent exists but is idle
    "stuck",  # Agent is stuck / needs input
    "pending",  # No agent assigned yet
    "completed",  # Task is done
]

# Keep TaskState as alias for backward compatibility with callers
TaskState = AgentState

CreatedBy = Literal["claw-town", "human"]

VALID_STATES: set[str] = {
    "orchestrator",
    "working",
    "idle",
    "stuck",
    "pending",
    "completed",
}

# Backward-compatible alias
VALID_AGENT_STATES: set[str] = VALID_STATES

TASKLIB_CMD: list[str] = [
    "python3",
    str(Path(__file__).parent / "claw_town_tasklib.py"),
]


_T_NUMBER_RE = re.compile(r"^T\d+$")


def _validate_t_number(value: str) -> None:
    """Validate that a value is a proper T-number (T followed by digits only).

    Raises SystemExit with code 1 and a clear error message if invalid.
    """
    if not _T_NUMBER_RE.match(value):
        print(
            f"Error: Invalid T-number '{value}'. "
            "Must be 'T' followed by digits only (e.g., T254710014)",
            file=sys.stderr,
        )
        sys.exit(1)


# --------------------------------------------------------------------------
# Migration helpers
# --------------------------------------------------------------------------


def _migrate_task_entry(entry: dict[str, Any]) -> dict[str, Any]:
    """Migrate a single task entry from old schema to new schema.

    Old field names -> new field names:
      - "window" -> "agent_window"
      - "state" -> "status" (via agent_state)
      - "agent_state" -> "status"
      - "agent" -> removed (was redundant with window)

    Adds missing cached fields with None/[] defaults.
    """
    migrated = dict(entry)

    # Rename "window" -> "agent_window"
    if "window" in migrated and "agent_window" not in migrated:
        migrated["agent_window"] = migrated.pop("window")
    elif "window" in migrated and "agent_window" in migrated:
        migrated.pop("window")

    # Migrate "state" -> "status" (oldest field name)
    if "state" in migrated:
        if migrated.get("status") is None and "agent_state" not in migrated:
            migrated["status"] = migrated.pop("state")
        else:
            migrated.pop("state")

    # Migrate "agent_state" -> "status"
    if "agent_state" in migrated:
        if migrated.get("status") is None:
            migrated["status"] = migrated.pop("agent_state")
        else:
            migrated.pop("agent_state")

    # Remove old "agent" field (redundant with agent_window)
    migrated.pop("agent", None)

    # Ensure cached fields exist with defaults
    migrated.setdefault("title", None)
    migrated.setdefault("blocked_by", [])

    # Ensure operational fields exist with defaults
    migrated.setdefault("status", "pending")
    migrated.setdefault("name", None)
    migrated.setdefault("agent_window", None)
    migrated.setdefault("created_by", "claw-town")
    migrated.setdefault("last_synced", None)

    return migrated


def _migrate_data(data: dict[str, Any]) -> dict[str, Any]:
    """Migrate entire tasks.json data from old schema to new.

    Handles:
    1. Old numeric-ID keyed known_tasks (e.g. {"1": {...}, "2": {...}})
       - These entries may have a "task_number" or "t_number" field with the real T-number
       - Entries without a T-number are dropped (cannot be migrated)
    2. Old "tasks" array format (list of dicts with "id" field)
    3. Old field names within task entries
    """
    known_tasks = data.get("known_tasks", {})
    migrated_tasks: dict[str, dict[str, Any]] = {}

    for key, entry in known_tasks.items():
        if key.startswith("T"):
            # Already a T-number key — just migrate fields
            migrated_tasks[key] = _migrate_task_entry(entry)
        else:
            # Old numeric ID key — try to find the real T-number
            t_number = entry.get("t_number") or entry.get("task_number")
            if t_number and isinstance(t_number, str) and t_number.startswith("T"):
                migrated_tasks[t_number] = _migrate_task_entry(entry)
            # else: drop — can't migrate without a T-number

    # Handle legacy "tasks" array format
    if "tasks" in data and isinstance(data["tasks"], list):
        for task in data["tasks"]:
            t_number = task.get("t_number") or task.get("task_number")
            task_id = task.get("id", "")
            # Use t_number if available, otherwise check if id is a T-number
            tn = t_number
            if not tn and isinstance(task_id, str) and task_id.startswith("T"):
                tn = task_id
            if tn and tn not in migrated_tasks:
                migrated_tasks[tn] = _migrate_task_entry(task)
        # Remove the old "tasks" array
        data.pop("tasks", None)

    data["known_tasks"] = migrated_tasks

    # Ensure top-level fields exist
    data.setdefault("project", "unknown")
    data.setdefault("root_task", None)
    data.setdefault("working_dir", None)
    data.setdefault("gsd_url", None)
    data.setdefault("gsd_project_id", None)
    data.setdefault("last_dag_walk", None)
    data.setdefault("last_gsd_poll", None)

    return data


# --------------------------------------------------------------------------
# Tasklib direct import helper
# --------------------------------------------------------------------------


def _run_tasklib(args: list[str]) -> dict[str, Any]:
    """Run a tasklib command via direct import (no subprocess).

    Parses args the same way the CLI would, calls the handler,
    and captures stdout JSON output.

    Raises RuntimeError on failure.
    """
    import io
    import claw_town_tasklib as tasklib

    parser = tasklib.build_parser()
    parsed = parser.parse_args(args)

    if not parsed.command:
        raise RuntimeError("No tasklib command specified")

    handler = tasklib.COMMAND_MAP.get(parsed.command)
    if handler is None:
        raise RuntimeError(f"Unknown tasklib command: {parsed.command}")

    # Capture stdout to get the JSON output
    old_stdout = sys.stdout
    sys.stdout = captured = io.StringIO()
    try:
        handler(parsed)
    except SystemExit as e:
        sys.stdout = old_stdout
        if e.code != 0:
            raise RuntimeError(f"tasklib {parsed.command} failed with exit code {e.code}")
        # Exit code 0 is fine
    finally:
        sys.stdout = old_stdout

    output = captured.getvalue().strip()
    if not output:
        raise RuntimeError("tasklib returned empty output")
    return json.loads(output)


# --------------------------------------------------------------------------
# Full task creation flow
# --------------------------------------------------------------------------


def create_task_full(
    project: str,
    title: str,
    description: str | None = None,
    blocked_by: list[str] | None = None,
    blocking: list[str] | None = None,
    name: str | None = None,
    tags: str | None = None,
    status: AgentState = "pending",
    created_by: CreatedBy = "claw-town",
    state_dir: Path | None = None,
    # Backward-compatible alias (deprecated)
    agent_state: AgentState | None = None,
) -> dict[str, Any]:
    """Create a task end-to-end: tasklib create -> blocking relationships -> tasks.json.

    Args:
        blocking: T-numbers that the new task blocks. For each target, the new
            task is added as a remote blocker (via tasklib) and appended to
            the target's blocked_by list in tasks.json.

    Returns a result dict with t_number, title, blocking info, and any errors.
    """
    # Build tag string: always include claw-town and project-scoped tag
    tag_parts: list[str] = ["claw-town", f"claw-town-{project}"]
    if tags:
        for t in tags.split(","):
            t = t.strip()
            if t and t not in tag_parts:
                tag_parts.append(t)
    tag_str = ",".join(tag_parts)

    # Step 1: Create remote task via tasklib
    create_args = [
        "create",
        "--title",
        title,
        "--description",
        description or "No description provided",
        "--tags",
        tag_str,
    ]
    tasklib_result = _run_tasklib(create_args)
    t_number: str = tasklib_result["t_number"]

    # Step 2: Set up blocked_by relationships (things that block the new task)
    blocking_errors: list[str] = []
    blocking_set: list[dict[str, str]] = []
    if blocked_by:
        for blocker in blocked_by:
            blocker = blocker.strip()
            if not blocker:
                continue
            try:
                rel_result = _run_tasklib(
                    [
                        "add-blocking",
                        "--blocker",
                        blocker,
                        "--blocked",
                        t_number,
                    ]
                )
                blocking_set.append(rel_result)
            except RuntimeError as e:
                blocking_errors.append(
                    f"Failed to add blocking {blocker} -> {t_number}: {e}"
                )

    # Step 3: Set up blocking relationships (things the new task blocks)
    if blocking:
        for target in blocking:
            target = target.strip()
            if not target:
                continue
            try:
                rel_result = _run_tasklib(
                    [
                        "add-blocking",
                        "--blocker",
                        t_number,
                        "--blocked",
                        target,
                    ]
                )
                blocking_set.append(rel_result)
            except RuntimeError as e:
                blocking_errors.append(
                    f"Failed to add blocking {t_number} -> {target}: {e}"
                )

    # Step 4: Write to tasks.json with cached + operational fields
    resolved_status = agent_state or status
    add_task(
        project,
        t_number,
        title=title,
        blocked_by=blocked_by,
        name=name,
        created_by=created_by,
        status=resolved_status,
        state_dir=state_dir,
    )

    # Step 5: Update target tasks' blocked_by in tasks.json
    if blocking:
        warnings = add_blocking_relationships(
            project, t_number, blocking, state_dir=state_dir
        )
        if warnings:
            blocking_errors.extend(warnings)

    result: dict[str, Any] = {
        "t_number": t_number,
        "title": title,
        "blocked_by": blocked_by or [],
        "blocking": blocking or [],
        "blocking_relationships": blocking_set,
    }
    if name:
        result["name"] = name
    if blocking_errors:
        result["blocking_errors"] = blocking_errors
    return result


# --------------------------------------------------------------------------
# Path helpers
# --------------------------------------------------------------------------


def _default_state_dir(project: str) -> Path:
    try:
        from claw_town_paths import get_state_dir

        return get_state_dir(project)
    except ImportError:
        return Path.home() / "projects" / project / ".claw_town"


def _tasks_file(project: str, state_dir: Path | None = None) -> Path:
    d = state_dir or _default_state_dir(project)
    return d / "tasks.json"


def _lock_file(project: str, state_dir: Path | None = None) -> Path:
    d = state_dir or _default_state_dir(project)
    return d / ".tasks.json.lock"


def _empty_state(project: str) -> dict[str, Any]:
    """Return an empty tasks.json structure."""
    return {
        "project": project,
        "root_task": None,
        "working_dir": None,
        "gsd_url": None,
        "gsd_project_id": None,
        "known_tasks": {},
        "last_dag_walk": None,
        "last_gsd_poll": None,
    }


# --------------------------------------------------------------------------
# Load / Save / Locked update
# --------------------------------------------------------------------------


def load(project: str, state_dir: Path | None = None) -> dict[str, Any]:
    """Read tasks.json with automatic migration from old formats.

    Returns empty state if file doesn't exist. If old format is detected,
    the data is migrated in-memory (not written back — next save will persist).
    """
    f = _tasks_file(project, state_dir)
    if not f.exists():
        return _empty_state(project)
    with open(f) as fh:
        data = json.load(fh)
    return _migrate_data(data)


def save(project: str, data: dict[str, Any], state_dir: Path | None = None) -> None:
    """Write tasks.json atomically with file locking."""
    f = _tasks_file(project, state_dir)
    lf = _lock_file(project, state_dir)

    f.parent.mkdir(parents=True, exist_ok=True)
    fd = open(lf, "a")
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)
        _atomic_write(f, data)
    finally:
        fcntl.flock(fd, fcntl.LOCK_UN)
        fd.close()


def _atomic_write(path: Path, data: dict[str, Any]) -> None:
    """Write JSON atomically via temp file + rename."""
    tmp_fd, tmp_path = tempfile.mkstemp(dir=str(path.parent), suffix=".tmp")
    try:
        with os.fdopen(tmp_fd, "w") as f:
            json.dump(data, f, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_path, str(path))
    except BaseException:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        raise


@contextmanager
def locked_update(
    project: str, state_dir: Path | None = None
) -> Generator[dict[str, Any], None, None]:
    """Context manager for read-modify-write with exclusive lock.

    Automatically migrates old format on read.

    Usage:
        with locked_update("my-project") as data:
            data["known_tasks"]["T123"] = {...}
        # data is written back on exit (atomically)
    """
    f = _tasks_file(project, state_dir)
    lf = _lock_file(project, state_dir)

    f.parent.mkdir(parents=True, exist_ok=True)
    fd = open(lf, "a")
    try:
        fcntl.flock(fd, fcntl.LOCK_EX)

        if f.exists():
            with open(f) as fh:
                data = json.load(fh)
            data = _migrate_data(data)
        else:
            data = _empty_state(project)

        yield data

        _atomic_write(f, data)
    finally:
        fcntl.flock(fd, fcntl.LOCK_UN)
        fd.close()


# --------------------------------------------------------------------------
# Task CRUD operations
# --------------------------------------------------------------------------


def add_task(
    project: str,
    t_number: str,
    title: str | None = None,
    status: AgentState = "pending",
    blocked_by: list[str] | None = None,
    name: str | None = None,
    agent_window: str | None = None,
    created_by: CreatedBy = "claw-town",
    state_dir: Path | None = None,
    # Backward-compatible aliases (deprecated)
    agent: str | None = None,
    window: str | None = None,
    state: AgentState | None = None,
    agent_state: AgentState | None = None,
) -> None:
    """Add a new known task with operational fields. No-op if already exists.

    Accepts old field names (agent, window, state, agent_state) for backward
    compatibility. New names take precedence.
    """
    resolved_window = agent_window or window
    if agent and not resolved_window:
        resolved_window = agent
    resolved_status = agent_state or state or status

    with locked_update(project, state_dir) as data:
        tasks = data.setdefault("known_tasks", {})
        if t_number not in tasks:
            tasks[t_number] = {
                # Cached fields
                "title": title,
                "blocked_by": blocked_by or [],
                # Operational fields
                "status": resolved_status,
                "name": name,
                "agent_window": resolved_window,
                "created_by": created_by,
                "last_synced": None,
            }


def update_agent(
    project: str,
    t_number: str,
    agent_window: str | None = None,
    status: AgentState | None = None,
    name: str | None = None,
    state_dir: Path | None = None,
    # Backward-compatible aliases (deprecated)
    agent: str | None = None,
    window: str | None = None,
    state: AgentState | None = None,
    agent_state: AgentState | None = None,
) -> bool:
    """Update operational fields for a task. Returns False if task not found.

    Accepts both new field names (agent_window, status) and old names
    (window, state, agent, agent_state) for backward compatibility.
    New names take precedence.
    """
    # Resolve backward-compatible aliases
    resolved_window = agent_window or window
    resolved_status = status or agent_state or state
    # "agent" was the old separate field; if provided and no agent_window, use it
    if agent and not resolved_window:
        resolved_window = agent

    with locked_update(project, state_dir) as data:
        tasks = data.get("known_tasks", {})
        if t_number not in tasks:
            return False
        entry = tasks[t_number]
        if resolved_window is not None:
            entry["agent_window"] = resolved_window
        if name is not None:
            entry["name"] = name
        if resolved_status is not None:
            if resolved_status not in VALID_STATES:
                raise ValueError(
                    f"Invalid status '{resolved_status}'. "
                    f"Must be one of: {VALID_STATES}"
                )
            entry["status"] = resolved_status
    return True


def update_cached_fields(
    project: str,
    t_number: str,
    title: str | None = None,
    status: str | None = None,
    blocked_by: list[str] | None = None,
    state_dir: Path | None = None,
) -> bool:
    """Update cached fields from remote sync. Returns False if task not found.

    This is the function sync should call when it discovers changes from remote.
    Remote is source of truth — these fields are overwritten without question.
    """
    now = datetime.now(timezone.utc).isoformat()
    with locked_update(project, state_dir) as data:
        tasks = data.get("known_tasks", {})
        if t_number not in tasks:
            return False
        entry = tasks[t_number]
        if title is not None:
            entry["title"] = title
        if status is not None:
            entry["status"] = status
        if blocked_by is not None:
            entry["blocked_by"] = blocked_by
        entry["last_synced"] = now
    return True


def remove_task(project: str, t_number: str, state_dir: Path | None = None) -> bool:
    """Remove a task from known_tasks. Returns False if not found."""
    with locked_update(project, state_dir) as data:
        tasks = data.get("known_tasks", {})
        if t_number in tasks:
            del tasks[t_number]
            return True
        return False


def add_blocking_relationships(
    project: str,
    blocker: str,
    targets: list[str],
    state_dir: Path | None = None,
) -> list[str]:
    """Add blocker to each target task's blocked_by list. Returns list of warnings.

    For each T-number in targets, appends blocker to that task's blocked_by list
    (if not already present). Silently skips targets that don't exist in tasks.json
    and returns a warning for each.
    """
    warnings: list[str] = []
    with locked_update(project, state_dir) as data:
        tasks = data.get("known_tasks", {})
        for target in targets:
            target = target.strip()
            if not target:
                continue
            if target not in tasks:
                warnings.append(
                    f"Target task {target} not found in tasks.json, skipping"
                )
                continue
            blocked_by = tasks[target].setdefault("blocked_by", [])
            if blocker not in blocked_by:
                blocked_by.append(blocker)
    return warnings


# --------------------------------------------------------------------------
# Query helpers
# --------------------------------------------------------------------------


def get_pending_tasks(
    project: str, state_dir: Path | None = None
) -> dict[str, dict[str, Any]]:
    """List tasks with status 'pending'."""
    data = load(project, state_dir)
    return {
        t: info
        for t, info in data.get("known_tasks", {}).items()
        if info.get("status") == "pending"
    }


def get_working_tasks(
    project: str, state_dir: Path | None = None
) -> dict[str, dict[str, Any]]:
    """List tasks with status 'working'."""
    data = load(project, state_dir)
    return {
        t: info
        for t, info in data.get("known_tasks", {}).items()
        if info.get("status") == "working"
    }


def get_tasks_by_state(
    project: str, agent_state: AgentState, state_dir: Path | None = None
) -> dict[str, dict[str, Any]]:
    """List tasks filtered by status."""
    if agent_state not in VALID_STATES:
        raise ValueError(
            f"Invalid status '{agent_state}'. Must be one of: {VALID_STATES}"
        )
    data = load(project, state_dir)
    return {
        t: info
        for t, info in data.get("known_tasks", {}).items()
        if info.get("status") == agent_state
    }


# --------------------------------------------------------------------------
# Top-level config setters
# --------------------------------------------------------------------------


def set_root_task(project: str, t_number: str, state_dir: Path | None = None) -> None:
    """Set the root orchestration task."""
    with locked_update(project, state_dir) as data:
        data["root_task"] = t_number


def set_working_dir(
    project: str, working_dir: str, state_dir: Path | None = None
) -> None:
    """Set or update the working directory for agents."""
    with locked_update(project, state_dir) as data:
        data["working_dir"] = working_dir


def get_working_dir(project: str, state_dir: Path | None = None) -> str | None:
    """Get the stored working directory, or None if not set."""
    data = load(project, state_dir)
    return data.get("working_dir")


def set_gsd_config(
    project: str,
    gsd_url: str | None = None,
    gsd_project_id: str | None = None,
    state_dir: Path | None = None,
) -> None:
    """Set GSD sync configuration."""
    with locked_update(project, state_dir) as data:
        if gsd_url is not None:
            data["gsd_url"] = gsd_url
        if gsd_project_id is not None:
            data["gsd_project_id"] = gsd_project_id


def update_dag_walk_timestamp(project: str, state_dir: Path | None = None) -> None:
    """Record that a DAG walk just completed."""
    with locked_update(project, state_dir) as data:
        data["last_dag_walk"] = datetime.now(timezone.utc).isoformat()


def update_gsd_poll_timestamp(project: str, state_dir: Path | None = None) -> None:
    """Record that a GSD poll just completed."""
    with locked_update(project, state_dir) as data:
        data["last_gsd_poll"] = datetime.now(timezone.utc).isoformat()


# --------------------------------------------------------------------------
# Initialize
# --------------------------------------------------------------------------


def initialize(
    project: str,
    root_task: str | None = None,
    working_dir: str | None = None,
    gsd_url: str | None = None,
    gsd_project_id: str | None = None,
    state_dir: Path | None = None,
) -> dict[str, Any]:
    """Initialize a new tasks.json, or update metadata on existing one.

    If the file already exists, updates root_task/working_dir/gsd_url/gsd_project_id
    if they are provided and the existing values are None.
    Returns the current state.
    """
    f = _tasks_file(project, state_dir)
    if f.exists():
        data = load(project, state_dir)
        changed = False
        if root_task and not data.get("root_task"):
            data["root_task"] = root_task
            changed = True
        if working_dir and not data.get("working_dir"):
            data["working_dir"] = working_dir
            changed = True
        if gsd_url and not data.get("gsd_url"):
            data["gsd_url"] = gsd_url
            changed = True
        if gsd_project_id and not data.get("gsd_project_id"):
            data["gsd_project_id"] = gsd_project_id
            changed = True
        if changed:
            save(project, data, state_dir)
        return data

    data = _empty_state(project)
    data["root_task"] = root_task
    data["working_dir"] = working_dir
    data["gsd_url"] = gsd_url
    data["gsd_project_id"] = gsd_project_id

    if root_task:
        data["known_tasks"][root_task] = {
            "title": None,
            "blocked_by": [],
            "status": "orchestrator",
            "name": None,
            "agent_window": f"claw-town-{project}:orchestrator",
            "created_by": "claw-town",
            "last_synced": None,
        }

    save(project, data, state_dir)
    return data


# --------------------------------------------------------------------------
# CLI interface
# --------------------------------------------------------------------------

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Claw Town tasks.json manager")
    parser.add_argument("project", help="Project name")
    sub = parser.add_subparsers(dest="command")

    # show
    sub.add_parser("show", help="Show current tasks.json")

    # init
    init_p = sub.add_parser("init", help="Initialize tasks.json")
    init_p.add_argument("--root-task", help="Root task T-number")
    init_p.add_argument("--working-dir", help="Working directory for agents")
    init_p.add_argument("--gsd-url", help="GSD URL")
    init_p.add_argument("--gsd-project-id", help="GSD project ID")

    # add
    add_p = sub.add_parser("add", help="Add a known task")
    add_p.add_argument("t_number", help="Task T-number (e.g. T253894461)")
    add_p.add_argument("--title", help="Task title")
    add_p.add_argument(
        "--blocked-by",
        nargs="*",
        default=[],
        help="T-numbers that block this task",
    )
    add_p.add_argument("--name", help="Short name for agent window")
    add_p.add_argument("--agent-window", help="tmux window name")
    add_p.add_argument(
        "--status",
        choices=list(VALID_STATES),
        default="pending",
        help="Agent lifecycle status",
        dest="status",
    )
    add_p.add_argument(
        "--agent-state",
        choices=list(VALID_STATES),
        default=None,
        help="(deprecated, use --status) Agent lifecycle status",
        dest="agent_state_deprecated",
    )
    add_p.add_argument(
        "--created-by",
        choices=["claw-town", "human"],
        default="claw-town",
    )
    add_p.add_argument(
        "--blocking",
        nargs="*",
        default=[],
        help="T-numbers that this task blocks (adds this task to their blocked_by)",
    )

    # create (full flow: tasklib + blocking + tasks.json)
    create_p = sub.add_parser(
        "create",
        help="Create task via tasklib and add to tasks.json",
    )
    create_p.add_argument("--title", required=True, help="Task title")
    create_p.add_argument("--description", help="Task description")
    create_p.add_argument(
        "--blocked-by",
        nargs="*",
        default=[],
        help="T-numbers that block this task (e.g. T123 T456)",
    )
    create_p.add_argument("--name", help="Short name for the task")
    create_p.add_argument("--tags", help="Comma-separated additional tags")
    create_p.add_argument(
        "--blocking",
        nargs="*",
        default=[],
        help="T-numbers that this task blocks (adds this task to their blocked_by)",
    )

    # update
    upd_p = sub.add_parser("update", help="Update operational fields")
    upd_p.add_argument("t_number", help="Task T-number")
    upd_p.add_argument("--agent-window", help="tmux window name")
    upd_p.add_argument(
        "--status",
        choices=list(VALID_STATES),
        dest="status",
        help="Agent lifecycle status",
    )
    upd_p.add_argument(
        "--agent-state",
        choices=list(VALID_STATES),
        default=None,
        help="(deprecated, use --status) Agent lifecycle status",
        dest="agent_state_deprecated",
    )
    upd_p.add_argument("--name", help="Short name")
    upd_p.add_argument(
        "--blocked-by",
        nargs="*",
        help="T-numbers that block this task (replaces existing list)",
    )
    upd_p.add_argument(
        "--blocking",
        nargs="*",
        help="T-numbers that this task blocks (adds this task to their blocked_by)",
    )

    # sync-cache
    sync_p = sub.add_parser("sync-cache", help="Update cached fields from remote")
    sync_p.add_argument("t_number", help="Task T-number")
    sync_p.add_argument("--title", help="Title from remote")
    sync_p.add_argument("--status", help="Status from remote")
    sync_p.add_argument(
        "--blocked-by",
        nargs="*",
        help="T-numbers that block this task",
    )

    # remove
    rm_p = sub.add_parser("remove", help="Remove a known task")
    rm_p.add_argument("t_number", help="Task T-number")

    # pending
    sub.add_parser("pending", help="List pending tasks")

    # working
    sub.add_parser("working", help="List working tasks")

    args = parser.parse_args()

    if args.command == "show":
        data = load(args.project)
        json.dump(data, sys.stdout, indent=2)
        print()

    elif args.command == "init":
        data = initialize(
            args.project,
            root_task=args.root_task,
            working_dir=args.working_dir,
            gsd_url=args.gsd_url,
            gsd_project_id=args.gsd_project_id,
        )
        json.dump(data, sys.stdout, indent=2)
        print()

    elif args.command == "add":
        _validate_t_number(args.t_number)
        resolved_status = args.agent_state_deprecated or args.status
        add_task(
            args.project,
            args.t_number,
            title=args.title,
            status=resolved_status,
            blocked_by=args.blocked_by or [],
            name=args.name,
            agent_window=args.agent_window,
            created_by=args.created_by,
            state_dir=None,
        )
        print(f"Added {args.t_number}")
        if args.blocking:
            warnings = add_blocking_relationships(
                args.project, args.t_number, args.blocking
            )
            for target in args.blocking:
                target = target.strip()
                if target:
                    print(f"  {args.t_number} now blocks {target}")
            for w in warnings:
                print(f"WARNING: {w}", file=sys.stderr)

    elif args.command == "create":
        # Validate blocked-by T-numbers
        for tn in args.blocked_by or []:
            _validate_t_number(tn)
        for tn in args.blocking or []:
            _validate_t_number(tn)
        try:
            result = create_task_full(
                args.project,
                title=args.title,
                description=args.description,
                blocked_by=args.blocked_by,
                blocking=args.blocking or None,
                name=args.name,
                tags=args.tags,
            )
            json.dump(result, sys.stdout, indent=2)
            print()
            if result.get("blocking_errors"):
                for err in result["blocking_errors"]:
                    print(f"WARNING: {err}", file=sys.stderr)
        except RuntimeError as e:
            print(
                json.dumps({"error": str(e)}),
                file=sys.stderr,
            )
            sys.exit(1)

    elif args.command == "update":
        _validate_t_number(args.t_number)
        resolved_status = args.agent_state_deprecated or args.status
        ok = update_agent(
            args.project,
            args.t_number,
            agent_window=args.agent_window,
            status=resolved_status,
            name=args.name,
        )
        if not ok:
            print(f"Task {args.t_number} not found", file=sys.stderr)
            sys.exit(1)
        # Update blocked_by if provided
        if args.blocked_by is not None:
            update_cached_fields(
                args.project,
                args.t_number,
                blocked_by=args.blocked_by,
            )
        # Handle --blocking: update target tasks' blocked_by lists
        if args.blocking:
            warnings = add_blocking_relationships(
                args.project, args.t_number, args.blocking
            )
            for target in args.blocking:
                target = target.strip()
                if target:
                    print(f"  {args.t_number} now blocks {target}")
            for w in warnings:
                print(f"WARNING: {w}", file=sys.stderr)
        print(f"Updated {args.t_number}")

    elif args.command == "sync-cache":
        ok = update_cached_fields(
            args.project,
            args.t_number,
            title=args.title,
            status=args.status,
            blocked_by=args.blocked_by,
        )
        if ok:
            print(f"Synced cached fields for {args.t_number}")
        else:
            print(f"Task {args.t_number} not found", file=sys.stderr)
            sys.exit(1)

    elif args.command == "remove":
        ok = remove_task(args.project, args.t_number)
        if ok:
            print(f"Removed {args.t_number}")
        else:
            print(f"Task {args.t_number} not found", file=sys.stderr)
            sys.exit(1)

    elif args.command == "pending":
        tasks = get_pending_tasks(args.project)
        for t, info in tasks.items():
            title = info.get("title") or "(no title)"
            print(f"  {t}  {title}  created_by={info['created_by']}")
        if not tasks:
            print("  (none)")

    elif args.command == "working":
        tasks = get_working_tasks(args.project)
        for t, info in tasks.items():
            title = info.get("title") or "(no title)"
            window = info.get("agent_window") or "?"
            print(f"  {t}  {title}  window={window}")
        if not tasks:
            print("  (none)")

    else:
        parser.print_help()
