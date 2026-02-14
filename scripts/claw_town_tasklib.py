#!/usr/bin/env python3
"""
Claw Town tasklib — local JSON file-based task operations.

All tasks are stored as individual JSON files in a .tasks/ directory.
All output is JSON to stdout; errors go to stderr as JSON.

Usage:
    python3 claw_town_tasklib.py <command> [args]
"""

from __future__ import annotations

import argparse
import fcntl
import json
import os
import sys
import tempfile
import traceback
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


# --------------------------------------------------------------------------
# Tasks directory resolution
# --------------------------------------------------------------------------

def _get_tasks_dir() -> Path:
    """Resolve the .tasks/ directory path.

    Priority:
    1. --tasks-dir CLI flag (handled by caller)
    2. CLAW_TOWN_TASKS_DIR env var
    3. .tasks/ relative to current working directory
    """
    env_dir = os.environ.get("CLAW_TOWN_TASKS_DIR")
    if env_dir:
        return Path(env_dir)
    return Path.cwd() / ".tasks"


# Module-level default; overridden by --tasks-dir flag in main()
TASKS_DIR: Path = _get_tasks_dir()


def _ensure_tasks_dir() -> None:
    """Create the .tasks/ directory if it doesn't exist."""
    TASKS_DIR.mkdir(parents=True, exist_ok=True)


# --------------------------------------------------------------------------
# Counter management (auto-incrementing IDs)
# --------------------------------------------------------------------------

def _counter_path() -> Path:
    return TASKS_DIR / "counter.json"


def _next_id() -> int:
    """Get the next task ID, atomically incrementing the counter."""
    _ensure_tasks_dir()
    counter_file = _counter_path()

    # Use file locking on a lock file for the counter
    lock_path = TASKS_DIR / ".counter.lock"
    lock_fd = open(lock_path, "a")
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX)

        if counter_file.exists():
            with open(counter_file) as f:
                data = json.load(f)
            next_id = data.get("next_id", 1)
        else:
            next_id = 1

        # Write incremented counter atomically
        new_data = {"next_id": next_id + 1}
        tmp_fd, tmp_path = tempfile.mkstemp(
            dir=str(TASKS_DIR), suffix=".tmp"
        )
        try:
            with os.fdopen(tmp_fd, "w") as f:
                json.dump(new_data, f)
                f.write("\n")
                f.flush()
                os.fsync(f.fileno())
            os.replace(tmp_path, str(counter_file))
        except BaseException:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
            raise

        return next_id
    finally:
        fcntl.flock(lock_fd, fcntl.LOCK_UN)
        lock_fd.close()


# --------------------------------------------------------------------------
# Task file I/O
# --------------------------------------------------------------------------

def _task_path(t_number: str) -> Path:
    """Get the file path for a task by T-number."""
    return TASKS_DIR / f"{t_number}.json"


def _read_task(t_number: str) -> dict[str, Any]:
    """Read a task file. Returns the task dict or exits with error."""
    path = _task_path(t_number)
    if not path.exists():
        _error(f"Task {t_number} not found")
    with open(path) as f:
        return json.load(f)


def _write_task(t_number: str, data: dict[str, Any]) -> None:
    """Write a task file atomically with file locking."""
    _ensure_tasks_dir()
    path = _task_path(t_number)
    lock_path = TASKS_DIR / f".{t_number}.lock"

    lock_fd = open(lock_path, "a")
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX)

        data["updated_at"] = datetime.now(timezone.utc).isoformat()

        tmp_fd, tmp_path = tempfile.mkstemp(
            dir=str(TASKS_DIR), suffix=".tmp"
        )
        try:
            with os.fdopen(tmp_fd, "w") as f:
                json.dump(data, f, indent=2, default=str)
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
    finally:
        fcntl.flock(lock_fd, fcntl.LOCK_UN)
        lock_fd.close()


def _read_all_tasks() -> list[dict[str, Any]]:
    """Read all task files from the .tasks/ directory."""
    _ensure_tasks_dir()
    tasks = []
    for f in TASKS_DIR.glob("T*.json"):
        try:
            with open(f) as fh:
                tasks.append(json.load(fh))
        except (json.JSONDecodeError, OSError):
            continue
    return tasks


def _format_t_number(num: int) -> str:
    """Format an integer as a T-number like T001, T012, T123."""
    return f"T{num:03d}"


# --------------------------------------------------------------------------
# Output helpers
# --------------------------------------------------------------------------

def _output(data: dict[str, Any]) -> None:
    """Print JSON to stdout."""
    print(json.dumps(data, default=str))


def _error(message: str, code: int = 1) -> None:
    """Print JSON error to stderr and exit."""
    print(json.dumps({"error": message}), file=sys.stderr)
    sys.exit(code)


# --------------------------------------------------------------------------
# T-number parsing
# --------------------------------------------------------------------------

def _parse_t_number(raw: str) -> str:
    """Parse and normalize a T-number string like 'T001' or 'T1' or '1'.

    Returns the canonical T-number string (e.g., 'T001').
    """
    cleaned = raw.strip().upper()
    if cleaned.startswith("T"):
        cleaned = cleaned[1:]
    try:
        num = int(cleaned)
    except ValueError:
        _error(f"Invalid T-number: {raw}")
        return ""  # unreachable
    return _format_t_number(num)


# --------------------------------------------------------------------------
# Status mapping
# --------------------------------------------------------------------------

# Map from various status representations to canonical status
VALID_STATUSES: set[str] = {
    "open", "in_progress", "closed", "blocked",
    "no_progress", "planned",
}

def _normalize_status(raw: str) -> str:
    """Normalize status strings to lowercase with underscores."""
    normalized = raw.lower().replace("-", "_").replace(" ", "_")
    # Map GraphQL-style statuses to local equivalents
    status_map = {
        "no_progress": "open",
        "planned": "open",
        "in_progress": "in_progress",
        "blocked": "blocked",
        "closed": "closed",
    }
    return status_map.get(normalized, normalized)


# --------------------------------------------------------------------------
# Command implementations
# --------------------------------------------------------------------------

def cmd_create(args: argparse.Namespace) -> None:
    """Create a new task as a local JSON file."""
    task_id = _next_id()
    t_number = _format_t_number(task_id)
    now = datetime.now(timezone.utc).isoformat()

    tags = []
    if args.tags:
        tags = [t.strip() for t in args.tags.split(",") if t.strip()]
    if "claw-town" not in tags:
        tags.append("claw-town")

    task_data: dict[str, Any] = {
        "t_number": t_number,
        "title": args.title,
        "description": args.description or "No description provided",
        "status": "open",
        "tags": tags,
        "created_at": now,
        "updated_at": now,
        "blocking": [],
        "blocked_by": [],
        "assigned_to": getattr(args, "assigned_to", None),
        "comments": [],
        "stage": getattr(args, "stage", None),
        "owner": getattr(args, "owner", None),
    }

    if args.progress:
        task_data["status"] = _normalize_status(args.progress)

    _write_task(t_number, task_data)

    _output({
        "t_number": t_number,
        "task_id": t_number,
        "title": args.title,
    })


def cmd_get(args: argparse.Namespace) -> None:
    """Get task details by T-number."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)

    _output({
        "t_number": task_data.get("t_number", t_number),
        "task_id": task_data.get("t_number", t_number),
        "title": task_data.get("title", ""),
        "description": task_data.get("description", ""),
        "status": task_data.get("status", "open"),
        "priority": task_data.get("priority", ""),
        "owner": task_data.get("assigned_to"),
        "owner_uid": None,
        "tags": task_data.get("tags", []),
        "created_time": task_data.get("created_at"),
        "updated_time": task_data.get("updated_at"),
        "completed_time": task_data.get("completed_at"),
        "blocking": task_data.get("blocking", []),
        "blocked_by": task_data.get("blocked_by", []),
    })


def cmd_update(args: argparse.Namespace) -> None:
    """Update task fields."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)

    updated_fields: list[str] = []

    if args.status:
        task_data["status"] = _normalize_status(args.status)
        updated_fields.append("status")

    if args.title:
        task_data["title"] = args.title
        updated_fields.append("title")

    if args.description:
        task_data["description"] = args.description
        updated_fields.append("description")

    if args.priority:
        task_data["priority"] = args.priority
        updated_fields.append("priority")

    if args.tags:
        new_tags = [t.strip() for t in args.tags.split(",") if t.strip()]
        existing_tags = task_data.get("tags", [])
        for tag in new_tags:
            if tag not in existing_tags:
                existing_tags.append(tag)
        task_data["tags"] = existing_tags
        updated_fields.append("tags")

    if getattr(args, "stage", None) is not None:
        task_data["stage"] = args.stage
        updated_fields.append("stage")

    if getattr(args, "owner", None) is not None:
        task_data["owner"] = args.owner if args.owner != "none" else None
        updated_fields.append("owner")

    if not updated_fields:
        _error(
            "No fields to update. Provide --status, --title, "
            "--description, --priority, --tags, --stage, or --owner."
        )

    _write_task(t_number, task_data)

    _output({
        "t_number": t_number,
        "updated": updated_fields,
    })


def cmd_close(args: argparse.Namespace) -> None:
    """Close a task."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)
    task_data["status"] = "closed"
    task_data["completed_at"] = datetime.now(timezone.utc).isoformat()
    _write_task(t_number, task_data)

    _output({
        "t_number": t_number,
        "closed": True,
    })


def cmd_add_blocking(args: argparse.Namespace) -> None:
    """Add a blocking relationship: blocker blocks blocked."""
    blocker_tn = _parse_t_number(args.blocker)
    blocked_tn = _parse_t_number(args.blocked)

    # Update the blocker's "blocking" list
    blocker_data = _read_task(blocker_tn)
    blocking_list = blocker_data.get("blocking", [])
    if blocked_tn not in blocking_list:
        blocking_list.append(blocked_tn)
    blocker_data["blocking"] = blocking_list
    _write_task(blocker_tn, blocker_data)

    # Update the blocked task's "blocked_by" list
    blocked_data = _read_task(blocked_tn)
    blocked_by_list = blocked_data.get("blocked_by", [])
    if blocker_tn not in blocked_by_list:
        blocked_by_list.append(blocker_tn)
    blocked_data["blocked_by"] = blocked_by_list
    _write_task(blocked_tn, blocked_data)

    _output({
        "blocker": args.blocker,
        "blocked": args.blocked,
        "relationship": "blocks",
    })


def cmd_comment(args: argparse.Namespace) -> None:
    """Post a comment with optional prefix tag."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)

    comments = task_data.get("comments", [])
    comment_id = len(comments) + 1
    now = datetime.now(timezone.utc).isoformat()

    content = args.content
    prefix = args.prefix.upper() if args.prefix else None

    comment_entry: dict[str, Any] = {
        "id": comment_id,
        "content": content,
        "created_at": now,
    }
    if prefix:
        comment_entry["prefix"] = prefix

    comments.append(comment_entry)
    task_data["comments"] = comments
    _write_task(t_number, task_data)

    _output({
        "t_number": t_number,
        "comment_id": comment_id,
        "posted": True,
    })


def cmd_comments(args: argparse.Namespace) -> None:
    """Read comments on a task, optionally filtered by prefix."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)

    comments = task_data.get("comments", [])
    prefix_filter = args.prefix.upper() if args.prefix else None

    result_comments: list[dict[str, Any]] = []
    for c in comments:
        # Filter by prefix if specified
        if prefix_filter:
            comment_prefix = c.get("prefix", "")
            comment_content = c.get("content", "")
            # Check both the prefix field and legacy [PREFIX] format in content
            if comment_prefix != prefix_filter and not comment_content.startswith(
                f"[{prefix_filter}]"
            ):
                continue

        result_comments.append({
            "id": c.get("id", ""),
            "author": c.get("author", ""),
            "content": c.get("content", ""),
            "prefix": c.get("prefix", ""),
            "created_time": c.get("created_at"),
        })

    _output({
        "t_number": t_number,
        "comments": result_comments,
    })


def cmd_walk_dag(args: argparse.Namespace) -> None:
    """Walk the dependency DAG from a root task."""
    root_tn = _parse_t_number(args.root)
    direction = args.direction or "dependents"
    max_depth = args.max_depth

    if args.direct_only:
        # Only show direct relationships
        task_data = _read_task(root_tn)
        tasks = [{
            "t_number": root_tn,
            "task_id": root_tn,
            "title": task_data.get("title", ""),
            "status": task_data.get("status", "open"),
            "blocks": task_data.get("blocking", []),
            "blocked_by": task_data.get("blocked_by", []),
        }]

        if direction == "dependents":
            related = task_data.get("blocking", [])
        else:
            related = task_data.get("blocked_by", [])

        for rel_tn in related:
            try:
                rel_data = _read_task(rel_tn)
                tasks.append({
                    "t_number": rel_tn,
                    "task_id": rel_tn,
                    "title": rel_data.get("title", ""),
                    "status": rel_data.get("status", "open"),
                    "blocks": [],
                    "blocked_by": [],
                })
            except SystemExit:
                # Task file doesn't exist, skip
                continue
    else:
        visited: set[str] = set()
        tasks = _walk_dag_recursive(root_tn, direction, visited, max_depth)

    _output({
        "root": root_tn,
        "direction": direction,
        "tasks": tasks,
    })


def _walk_dag_recursive(
    t_number: str,
    direction: str,
    visited: set[str],
    max_depth: int | None = None,
    current_depth: int = 0,
) -> list[dict[str, Any]]:
    """Recursively walk the dependency DAG locally."""
    if t_number in visited:
        return []
    if max_depth is not None and current_depth > max_depth:
        return []
    visited.add(t_number)

    try:
        task_data = _read_task(t_number)
    except SystemExit:
        return []

    blocking = task_data.get("blocking", [])
    blocked_by = task_data.get("blocked_by", [])

    entry: dict[str, Any] = {
        "t_number": t_number,
        "task_id": t_number,
        "title": task_data.get("title", ""),
        "status": task_data.get("status", "open"),
        "blocks": blocking,
        "blocked_by": blocked_by,
    }

    result: list[dict[str, Any]] = [entry]

    if direction == "dependents":
        related = blocking
    else:
        related = blocked_by

    for rel_tn in related:
        if rel_tn not in visited:
            result.extend(
                _walk_dag_recursive(
                    rel_tn, direction, visited, max_depth, current_depth + 1
                )
            )

    return result


def cmd_list_dependents(args: argparse.Namespace) -> None:
    """List tasks that this task blocks (dependents)."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)

    blocking = task_data.get("blocking", [])
    dependents = []
    for dep_tn in blocking:
        try:
            dep_data = _read_task(dep_tn)
            dependents.append({
                "t_number": dep_tn,
                "task_id": dep_tn,
                "title": dep_data.get("title", ""),
                "status": dep_data.get("status", "open"),
            })
        except SystemExit:
            continue

    _output({
        "t_number": t_number,
        "dependents": dependents,
    })


def cmd_list_dependencies(args: argparse.Namespace) -> None:
    """List tasks that block this task (dependencies)."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)

    blocked_by = task_data.get("blocked_by", [])
    dependencies = []
    for dep_tn in blocked_by:
        try:
            dep_data = _read_task(dep_tn)
            dependencies.append({
                "t_number": dep_tn,
                "task_id": dep_tn,
                "title": dep_data.get("title", ""),
                "status": dep_data.get("status", "open"),
            })
        except SystemExit:
            continue

    _output({
        "t_number": t_number,
        "dependencies": dependencies,
    })


def cmd_search(args: argparse.Namespace) -> None:
    """Search tasks by tags."""
    tag_list = [t.strip() for t in args.tags.split(",") if t.strip()]

    all_tasks = _read_all_tasks()
    matching = []
    for task in all_tasks:
        task_tags = task.get("tags", [])
        if any(tag in task_tags for tag in tag_list):
            matching.append({
                "t_number": task.get("t_number", ""),
                "task_id": task.get("t_number", ""),
                "title": task.get("title", ""),
                "status": task.get("status", "open"),
            })

    _output({
        "tags": tag_list,
        "count": len(matching),
        "tasks": matching,
    })


def cmd_assign(args: argparse.Namespace) -> None:
    """Assign a task to a user."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)
    task_data["assigned_to"] = args.user
    _write_task(t_number, task_data)

    _output({
        "t_number": t_number,
        "assigned_to": args.user,
    })


def cmd_reopen(args: argparse.Namespace) -> None:
    """Reopen a closed task."""
    t_number = _parse_t_number(args.task)
    task_data = _read_task(t_number)

    target_status = _normalize_status(args.status) if args.status else "in_progress"
    task_data["status"] = target_status
    task_data.pop("completed_at", None)
    _write_task(t_number, task_data)

    _output({
        "t_number": t_number,
        "reopened": True,
        "status": target_status,
    })


# --------------------------------------------------------------------------
# Argument parser
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    """Build the argument parser with all subcommands."""
    parser = argparse.ArgumentParser(
        description="Claw Town tasklib — local JSON file-based task operations",
    )
    parser.add_argument(
        "--tasks-dir",
        help="Override .tasks/ directory path (default: ./.tasks/)",
    )
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # create
    p_create = subparsers.add_parser("create", help="Create a new task")
    p_create.add_argument("--title", required=True, help="Task title")
    p_create.add_argument("--description", help="Task description")
    p_create.add_argument("--priority", default="med-pri", help="Priority level")
    p_create.add_argument("--tags", help="Comma-separated tag names")
    p_create.add_argument(
        "--assigned-to", dest="assigned_to", help="Assignee username"
    )
    p_create.add_argument("--progress", help="Initial progress status")
    p_create.add_argument("--stage", help="Pipeline stage (pm, tech-lead, intern, code-review, perf-check, qa-test, design-audit, done)")
    p_create.add_argument("--owner", help="Pipeline role owner")

    # add-blocking
    p_block = subparsers.add_parser("add-blocking", help="Add blocking relationship")
    p_block.add_argument("--blocker", required=True, help="T-number of blocker task")
    p_block.add_argument("--blocked", required=True, help="T-number of blocked task")

    # get
    p_get = subparsers.add_parser("get", help="Get task details")
    p_get.add_argument("task", help="T-number (e.g. T001)")

    # update
    p_update = subparsers.add_parser("update", help="Update task fields")
    p_update.add_argument("task", help="T-number (e.g. T001)")
    p_update.add_argument("--status", help="New progress status")
    p_update.add_argument("--title", help="New title")
    p_update.add_argument("--description", help="New description")
    p_update.add_argument("--priority", help="New priority")
    p_update.add_argument("--tags", help="Comma-separated tag names to add")
    p_update.add_argument("--stage", help="Pipeline stage")
    p_update.add_argument("--owner", help="Pipeline role owner (use 'none' to clear)")

    # close
    p_close = subparsers.add_parser("close", help="Close and complete a task")
    p_close.add_argument("task", help="T-number (e.g. T001)")

    # comment
    p_comment = subparsers.add_parser("comment", help="Post a comment")
    p_comment.add_argument("task", help="T-number (e.g. T001)")
    p_comment.add_argument("--prefix", help="Comment prefix tag (e.g. FINDINGS)")
    p_comment.add_argument("--content", required=True, help="Comment text")

    # comments
    p_comments = subparsers.add_parser("comments", help="Read comments")
    p_comments.add_argument("task", help="T-number (e.g. T001)")
    p_comments.add_argument("--prefix", help="Filter by prefix tag")

    # walk-dag
    p_dag = subparsers.add_parser("walk-dag", help="Walk dependency DAG")
    p_dag.add_argument("--root", required=True, help="Root T-number")
    p_dag.add_argument(
        "--direction",
        choices=["dependents", "dependencies"],
        default="dependents",
        help="Walk direction",
    )
    p_dag.add_argument(
        "--direct-only",
        action="store_true",
        help="Only show direct relationships",
    )
    p_dag.add_argument(
        "--max-depth",
        type=int,
        default=None,
        help="Maximum recursion depth",
    )

    # list-dependents
    p_ldep = subparsers.add_parser(
        "list-dependents", help="List tasks this one blocks"
    )
    p_ldep.add_argument("task", help="T-number (e.g. T001)")

    # list-dependencies
    p_ldeps = subparsers.add_parser(
        "list-dependencies", help="List tasks blocking this one"
    )
    p_ldeps.add_argument("task", help="T-number (e.g. T001)")

    # search
    p_search = subparsers.add_parser("search", help="Search tasks by tags")
    p_search.add_argument("--tags", required=True, help="Comma-separated tag names")

    # assign
    p_assign = subparsers.add_parser("assign", help="Assign task to user")
    p_assign.add_argument("task", help="T-number (e.g. T001)")
    p_assign.add_argument("--user", required=True, help="Username")

    # reopen
    p_reopen = subparsers.add_parser("reopen", help="Reopen a closed task")
    p_reopen.add_argument("task", help="T-number (e.g. T001)")
    p_reopen.add_argument(
        "--status",
        default="IN_PROGRESS",
        help="Status to set (default: IN_PROGRESS)",
    )

    return parser


COMMAND_MAP: dict[str, Any] = {
    "create": cmd_create,
    "add-blocking": cmd_add_blocking,
    "get": cmd_get,
    "update": cmd_update,
    "close": cmd_close,
    "reopen": cmd_reopen,
    "comment": cmd_comment,
    "comments": cmd_comments,
    "walk-dag": cmd_walk_dag,
    "list-dependents": cmd_list_dependents,
    "list-dependencies": cmd_list_dependencies,
    "search": cmd_search,
    "assign": cmd_assign,
}


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    # Override tasks dir if specified
    global TASKS_DIR
    if args.tasks_dir:
        TASKS_DIR = Path(args.tasks_dir)

    if not args.command:
        parser.print_help()
        sys.exit(1)

    try:
        handler = COMMAND_MAP.get(args.command)
        if handler is None:
            _error(f"Unknown command: {args.command}")
        handler(args)
    except SystemExit:
        raise
    except Exception as e:
        tb = traceback.format_exc()
        print(
            json.dumps({"error": str(e), "traceback": tb}),
            file=sys.stderr,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
