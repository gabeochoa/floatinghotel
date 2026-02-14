#!/usr/bin/env python3
"""
Claw Town Sync — Local file-based sync loop that discovers tasks
by reading .tasks/*.json files and diffing against tasks.json.

No remote API calls (no jf graphql, no GSD polling).

Usage:
    python3 claw_town_sync.py sync --project my-project
    python3 claw_town_sync.py watch --project my-project --interval 30
    python3 claw_town_sync.py dag --project my-project
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

# Allow importing from the same directory
_SCRIPTS_DIR = str(Path(__file__).parent)
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

import claw_town_tasks_json as tasks_json  # noqa: E402
from claw_town_events import EVENT_TASK_SYNC, log_event  # noqa: E402


# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

DAG_MAX_DEPTH = 5
CACHE_FILENAME = ".sync_cache.json"


# --------------------------------------------------------------------------
# Tasks directory resolution
# --------------------------------------------------------------------------

def _get_tasks_dir(state_dir: Path | None = None) -> Path:
    """Get the .tasks/ directory path."""
    env_dir = os.environ.get("CLAW_TOWN_TASKS_DIR")
    if env_dir:
        return Path(env_dir)
    return Path.cwd() / ".tasks"


def _read_all_task_files(tasks_dir: Path) -> list[dict[str, Any]]:
    """Read all T*.json files from the .tasks/ directory."""
    tasks = []
    if not tasks_dir.exists():
        return tasks
    for f in sorted(tasks_dir.glob("T*.json")):
        try:
            with open(f) as fh:
                tasks.append(json.load(fh))
        except (json.JSONDecodeError, OSError):
            continue
    return tasks


# --------------------------------------------------------------------------
# Sync cache
# --------------------------------------------------------------------------

def _cache_path(project: str, state_dir: Path | None = None) -> Path:
    if state_dir:
        d = state_dir
    else:
        try:
            from claw_town_paths import get_state_dir
            d = get_state_dir(project)
        except ImportError:
            d = Path.home() / "projects" / project / ".claw_town"
    return d / CACHE_FILENAME


def _load_cache(project: str, state_dir: Path | None = None) -> dict[str, Any]:
    p = _cache_path(project, state_dir)
    if not p.exists():
        return {}
    try:
        with open(p) as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return {}


def _save_cache(
    project: str, cache: dict[str, Any], state_dir: Path | None = None
) -> None:
    p = _cache_path(project, state_dir)
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = str(p) + ".tmp"
    try:
        with open(tmp, "w") as f:
            json.dump(cache, f, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, str(p))
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


# --------------------------------------------------------------------------
# Local DAG walking (reads .tasks/ files)
# --------------------------------------------------------------------------

def walk_dag(root_task: str, tasks_dir: Path | None = None) -> dict[str, Any]:
    """Walk the dependency DAG from root by reading local task files.

    Returns:
        {
            "root": "T001",
            "tasks": [...],
            "error": None
        }
    """
    if tasks_dir is None:
        tasks_dir = _get_tasks_dir()

    visited: set[str] = set()
    tasks: list[dict[str, Any]] = []
    queue: list[tuple[str, int]] = [(root_task, 0)]

    while queue:
        t_number, depth = queue.pop(0)
        if t_number in visited:
            continue
        visited.add(t_number)

        task_path = tasks_dir / f"{t_number}.json"
        if not task_path.exists():
            return {
                "root": root_task,
                "tasks": tasks,
                "error": f"task {t_number} not found",
            }

        try:
            with open(task_path) as f:
                task_data = json.load(f)
        except (json.JSONDecodeError, OSError) as e:
            return {
                "root": root_task,
                "tasks": tasks,
                "error": f"error reading {t_number}: {e}",
            }

        blocking = task_data.get("blocking", [])
        blocked_by = task_data.get("blocked_by", [])

        tasks.append({
            "t_number": t_number,
            "title": task_data.get("title", ""),
            "status": task_data.get("status", "open"),
            "blocks": blocking,
            "blocked_by": blocked_by,
        })

        # Follow both directions to discover the full graph
        if depth < DAG_MAX_DEPTH:
            for b in blocked_by:
                if b not in visited:
                    queue.append((b, depth + 1))
            for b in blocking:
                if b not in visited:
                    queue.append((b, depth + 1))

    return {
        "root": root_task,
        "tasks": tasks,
        "error": None,
    }


# --------------------------------------------------------------------------
# Core sync logic
# --------------------------------------------------------------------------

def run_sync(
    project: str,
    state_dir: Path | None = None,
    deep: bool = False,
) -> dict[str, Any]:
    """Run one sync cycle: read .tasks/ files, diff against tasks.json.

    Args:
        project: Project name
        state_dir: Override state directory
        deep: Unused (kept for CLI compatibility)

    Returns:
        Structured JSON report of all discovered changes.
    """
    now = datetime.now(timezone.utc).isoformat()
    errors: list[str] = []

    # Load current operational state
    data = tasks_json.load(project, state_dir)
    root_task = data.get("root_task")
    if not root_task:
        return {
            "timestamp": now,
            "root_task": None,
            "error": "No root_task configured in tasks.json",
            "changes": _empty_changes(),
        }

    known_tasks: dict[str, dict[str, Any]] = data.get("known_tasks", {})
    known_t_numbers: set[str] = set(known_tasks.keys())
    cache = _load_cache(project, state_dir)

    # Read all local task files
    tasks_dir = _get_tasks_dir()
    all_task_files = _read_all_task_files(tasks_dir)

    # Build lookup by t_number
    local_by_t: dict[str, dict[str, Any]] = {}
    for t in all_task_files:
        tn = t.get("t_number", "")
        if tn:
            local_by_t[tn] = t

    local_t_numbers: set[str] = set(local_by_t.keys())

    # Diff
    changes: dict[str, list[dict[str, Any]]] = _empty_changes()

    # New tasks: in .tasks/ but not in known_tasks
    new_t = local_t_numbers - known_t_numbers
    for tn in sorted(new_t):
        task_info = local_by_t[tn]
        changes["new_tasks"].append({
            "t_number": tn,
            "title": task_info.get("title", ""),
            "source": "local",
            "created_by": "human",
        })

    # Removed tasks: in known_tasks but not in .tasks/ (excluding root)
    removed_t = known_t_numbers - local_t_numbers - {root_task}
    for tn in sorted(removed_t):
        if cache.get(tn):
            changes["removed_tasks"].append({
                "t_number": tn,
                "reason": "file_deleted",
            })

    # Status changes
    tasks_to_check = known_t_numbers & local_t_numbers
    closed_tasks: set[str] = set()

    for tn in sorted(tasks_to_check):
        cached = cache.get(tn, {})
        old_status = cached.get("status")
        current_status = local_by_t[tn].get("status")

        if current_status and old_status and current_status != old_status:
            changes["status_changes"].append({
                "t_number": tn,
                "old_status": old_status,
                "new_status": current_status,
            })
            if current_status == "closed":
                closed_tasks.add(tn)

        if current_status == "closed" and not old_status:
            if known_tasks.get(tn, {}).get("status") not in ("completed",):
                closed_tasks.add(tn)

        # Blocked_by changes
        remote_blocked_by = sorted(local_by_t[tn].get("blocked_by", []))
        local_blocked_by = sorted(known_tasks.get(tn, {}).get("blocked_by", []))
        if remote_blocked_by != local_blocked_by:
            changes["blocked_by_changes"].append({
                "t_number": tn,
                "old_blocked_by": local_blocked_by,
                "new_blocked_by": remote_blocked_by,
            })

    # Newly unblocked
    for tn in sorted(local_t_numbers - closed_tasks):
        task_info = local_by_t.get(tn)
        if not task_info:
            continue
        blocked_by = task_info.get("blocked_by", [])
        if not blocked_by:
            continue

        all_closed = True
        has_newly_closed = False
        for blocker in blocked_by:
            blocker_info = local_by_t.get(blocker, {})
            blocker_status = blocker_info.get("status")
            if blocker_status != "closed":
                all_closed = False
                break
            if blocker in closed_tasks:
                has_newly_closed = True

        if all_closed and has_newly_closed:
            changes["newly_unblocked"].append({
                "t_number": tn,
                "title": task_info.get("title", ""),
            })

    # Update cache
    new_cache: dict[str, Any] = {}
    for tn, task_info in local_by_t.items():
        new_cache[tn] = {
            "status": task_info.get("status"),
        }
    _save_cache(project, new_cache, state_dir)

    # Update tasks.json with new tasks
    for new_info in changes["new_tasks"]:
        tn = new_info["t_number"]
        task_info = local_by_t.get(tn, {})
        current_status = task_info.get("status")
        initial_state = "completed" if current_status == "closed" else "pending"
        tasks_json.add_task(
            project,
            tn,
            title=new_info.get("title"),
            created_by="human",
            status=initial_state,
            state_dir=state_dir,
        )

    # Mark closed tasks as completed
    for tn in closed_tasks:
        if tn in known_tasks and known_tasks[tn].get("status") != "completed":
            tasks_json.update_agent(
                project, tn, status="completed", state_dir=state_dir
            )

    # Update blocked_by in tasks.json
    for change in changes["blocked_by_changes"]:
        tn = change["t_number"]
        tasks_json.update_cached_fields(
            project, tn, blocked_by=change["new_blocked_by"], state_dir=state_dir
        )

    # Sync titles
    for tn, task_info in local_by_t.items():
        title = task_info.get("title")
        if title and tn in known_tasks:
            existing_title = known_tasks[tn].get("title")
            if existing_title != title:
                tasks_json.update_cached_fields(
                    project, tn, title=title, state_dir=state_dir
                )

    # Update DAG walk timestamp
    tasks_json.update_dag_walk_timestamp(project, state_dir)

    # Event log
    if _has_changes(changes):
        one_liner, full_summary = _build_sync_summary(changes)
        log_event(project, EVENT_TASK_SYNC, one_liner, details=full_summary, state_dir=state_dir)
    else:
        log_event(project, EVENT_TASK_SYNC, "No changes", state_dir=state_dir)

    report: dict[str, Any] = {
        "timestamp": now,
        "root_task": root_task,
        "changes": changes,
        "dag_size": len(all_task_files),
        "gsd_size": 0,
    }
    if errors:
        report["errors"] = errors

    return report


def _has_changes(changes: dict[str, list[dict[str, Any]]]) -> bool:
    return any(len(v) > 0 for v in changes.values())


def _build_sync_summary(
    changes: dict[str, list[dict[str, Any]]],
) -> tuple[str, str]:
    """Build a human-readable summary of sync changes."""
    lines: list[str] = ["Task graph updated"]

    n_new = len(changes.get("new_tasks", []))
    n_removed = len(changes.get("removed_tasks", []))
    n_status = len(changes.get("status_changes", []))
    n_blocked_by = len(changes.get("blocked_by_changes", []))
    n_unblocked = len(changes.get("newly_unblocked", []))
    n_reopened = len(changes.get("reopened_tasks", []))

    for info in changes.get("new_tasks", []):
        tn = info.get("t_number", "")
        title = info.get("title", "")
        lines.append(f'  LOCAL: +1 new {tn} "{title}"')

    for info in changes.get("removed_tasks", []):
        tn = info.get("t_number", "")
        lines.append(f"  LOCAL: -1 removed {tn}")

    for info in changes.get("status_changes", []):
        tn = info.get("t_number", "")
        old = info.get("old_status", "?")
        new = info.get("new_status", "?")
        lines.append(f"  LOCAL: {tn} {old} -> {new}")

    for info in changes.get("blocked_by_changes", []):
        tn = info.get("t_number", "")
        old = info.get("old_blocked_by", [])
        new = info.get("new_blocked_by", [])
        lines.append(f"  LOCAL: {tn} blocked_by {old} -> {new}")

    for info in changes.get("newly_unblocked", []):
        tn = info.get("t_number", "")
        title = info.get("title", "")
        lines.append(f'  LOCAL: {tn} "{title}" now unblocked')

    for info in changes.get("reopened_tasks", []):
        tn = info.get("t_number", "")
        lines.append(f"  LOCAL: {tn} reopened")

    full_summary = "\n".join(lines)
    one_liner = (
        f"Task graph updated: +{n_new} new, -{n_removed} removed, "
        f"~{n_status} status, ~{n_blocked_by} blocked_by, "
        f"!{n_unblocked} unblocked, ↺{n_reopened} reopened"
    )

    return one_liner, full_summary


def _empty_changes() -> dict[str, list[dict[str, Any]]]:
    return {
        "new_tasks": [],
        "removed_tasks": [],
        "status_changes": [],
        "description_changes": [],
        "priority_changes": [],
        "blocked_by_changes": [],
        "newly_unblocked": [],
        "reopened_tasks": [],
    }


# --------------------------------------------------------------------------
# Watch mode
# --------------------------------------------------------------------------

def run_watch(
    project: str,
    interval: int = 30,
    state_dir: Path | None = None,
    deep: bool = False,
) -> None:
    """Run sync in a loop until interrupted."""
    stop = False

    def _handle_signal(signum: int, frame: Any) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    print(
        f"Watching project '{project}' every {interval}s (Ctrl+C to stop)", flush=True
    )

    while not stop:
        try:
            report = run_sync(project, state_dir=state_dir, deep=deep)
            _print_report_summary(report)
        except Exception as e:
            print(f"[ERROR] Sync failed: {e}", file=sys.stderr, flush=True)

        elapsed = 0.0
        while elapsed < interval and not stop:
            time.sleep(min(1.0, interval - elapsed))
            elapsed += 1.0

    print("Watch stopped.", flush=True)


def _print_report_summary(report: dict[str, Any]) -> None:
    ts = report.get("timestamp", "?")
    changes = report.get("changes", {})
    n_new = len(changes.get("new_tasks", []))
    n_removed = len(changes.get("removed_tasks", []))
    n_status = len(changes.get("status_changes", []))
    n_blocked_by = len(changes.get("blocked_by_changes", []))
    n_unblocked = len(changes.get("newly_unblocked", []))
    total = n_new + n_removed + n_status + n_blocked_by + n_unblocked
    dag = report.get("dag_size", 0)
    errs = report.get("errors", [])

    parts = [f"[{ts}]"]
    if total == 0 and not errs:
        parts.append(f"no changes (local={dag})")
    else:
        if n_new:
            parts.append(f"+{n_new} new")
        if n_removed:
            parts.append(f"-{n_removed} removed")
        if n_status:
            parts.append(f"~{n_status} status")
        if n_blocked_by:
            parts.append(f"~{n_blocked_by} blocked_by")
        if n_unblocked:
            parts.append(f"!{n_unblocked} unblocked")
        if errs:
            parts.append(f"({len(errs)} errors)")

    print(" ".join(parts), flush=True)


# --------------------------------------------------------------------------
# DAG-only mode
# --------------------------------------------------------------------------

def run_dag(project: str, state_dir: Path | None = None) -> dict[str, Any]:
    """Walk the DAG and return current state (no diffing)."""
    data = tasks_json.load(project, state_dir)
    root_task = data.get("root_task")
    if not root_task:
        return {"error": "No root_task configured in tasks.json"}
    return walk_dag(root_task)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Claw Town Sync — local file-based task discovery and change detection"
    )
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # sync
    p_sync = subparsers.add_parser("sync", help="Run one sync cycle")
    p_sync.add_argument("--project", required=True, help="Project name")
    p_sync.add_argument("--state-dir", help="Override state directory")
    p_sync.add_argument("--deep", action="store_true", help="(unused, kept for compat)")
    p_sync.add_argument("--pretty", action="store_true", help="Pretty-print JSON")

    # watch
    p_watch = subparsers.add_parser("watch", help="Run continuous sync loop")
    p_watch.add_argument("--project", required=True, help="Project name")
    p_watch.add_argument("--interval", type=int, default=30, help="Seconds between cycles")
    p_watch.add_argument("--state-dir", help="Override state directory")
    p_watch.add_argument("--deep", action="store_true", help="(unused, kept for compat)")

    # dag
    p_dag = subparsers.add_parser("dag", help="Walk DAG only (no diffing)")
    p_dag.add_argument("--project", required=True, help="Project name")
    p_dag.add_argument("--state-dir", help="Override state directory")
    p_dag.add_argument("--pretty", action="store_true", help="Pretty-print JSON")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    state_dir = Path(args.state_dir) if getattr(args, "state_dir", None) else None

    if args.command == "sync":
        report = run_sync(args.project, state_dir=state_dir, deep=args.deep)
        indent = 2 if args.pretty else None
        print(json.dumps(report, indent=indent))

    elif args.command == "watch":
        run_watch(
            args.project,
            interval=args.interval,
            state_dir=state_dir,
            deep=args.deep,
        )

    elif args.command == "dag":
        result = run_dag(args.project, state_dir=state_dir)
        indent = 2 if args.pretty else None
        print(json.dumps(result, indent=indent))

    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
