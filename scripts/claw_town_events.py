#!/usr/bin/env python3
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
from __future__ import annotations

"""
Claw Town Unified Event Log

Provides a single append-only JSONL event log for all Claw Town activity within
a project. Other Claw Town scripts import `log_event` to record what happened;
the dashboard and debugging tools use `read_events` / `last_event_of_type` to
query the log.

File location: ~/projects/<project>/.claw_town/events.jsonl

Usage:
    from claw_town_events import log_event, read_events, last_event_of_type

    log_event("my-project", EVENT_NUDGE_ORCH, "Orchestrator idle 5 min")
    recent = read_events("my-project", limit=20)
    last = last_event_of_type("my-project", EVENT_TASK_COMPLETE)

CLI (for testing):
    python3 claw_town_events.py log --project imp --type nudge_orch --summary "test"
    python3 claw_town_events.py read --project imp --limit 10
    python3 claw_town_events.py last --project imp --type nudge_orch
"""

import argparse
import fcntl
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

# ---------------------------------------------------------------------------
# Event type constants
# ---------------------------------------------------------------------------
EVENT_NUDGE_ORCH: str = "nudge_orch"
EVENT_NUDGE_AGENT: str = "nudge_agent"
EVENT_AGENT_STALL: str = "agent_stall"
EVENT_LEARN: str = "learn"
EVENT_TASK_SYNC: str = "task_sync"
EVENT_TASK_COMPLETE: str = "task_complete"
EVENT_RESTART_ORCH: str = "restart_orch"
EVENT_RESTART_AGENT: str = "restart_agent"

ALL_EVENT_TYPES: list[str] = [
    EVENT_NUDGE_ORCH,
    EVENT_NUDGE_AGENT,
    EVENT_AGENT_STALL,
    EVENT_LEARN,
    EVENT_TASK_SYNC,
    EVENT_TASK_COMPLETE,
    EVENT_RESTART_ORCH,
    EVENT_RESTART_AGENT,
]

# Maximum number of events retained in the log file.
_MAX_EVENTS: int = 500


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _events_path(project: str, state_dir: Path | None = None) -> Path:
    """Return the path to the events.jsonl file for *project*."""
    if state_dir is not None:
        base = state_dir
    else:
        try:
            from claw_town_paths import get_state_dir

            base = get_state_dir(project)
        except ImportError:
            base = Path.home() / "projects" / project / ".claw_town"
    return base / "events.jsonl"


def _ensure_parent(path: Path) -> None:
    """Create parent directories if they don't exist."""
    path.parent.mkdir(parents=True, exist_ok=True)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------


def log_event(
    project: str,
    event_type: str,
    summary: str,
    details: str | None = None,
    state_dir: Path | None = None,
) -> None:
    """Append an event to ``events.jsonl``.

    Parameters
    ----------
    project:
        Project name (used to locate the state directory).
    event_type:
        One of the ``EVENT_*`` constants defined in this module.
    summary:
        Short one-line description of the event.
    details:
        Optional longer description (may be multiline).
    state_dir:
        Override the default ``~/projects/<project>/.claw_town`` directory.
    """
    try:
        path = _events_path(project, state_dir)
        _ensure_parent(path)

        record: dict = {
            "ts": datetime.now(timezone.utc).isoformat(),
            "type": event_type,
            "summary": summary,
        }
        if details is not None:
            record["details"] = details

        line = json.dumps(record, ensure_ascii=False) + "\n"

        with open(path, "a") as fh:
            fcntl.flock(fh, fcntl.LOCK_EX)
            try:
                fh.write(line)
            finally:
                fcntl.flock(fh, fcntl.LOCK_UN)

        # Auto-truncate if the file has grown beyond the limit.
        _truncate_if_needed(path)

    except Exception as exc:
        print(f"[claw_town_events] log_event error: {exc}", file=sys.stderr)


def read_events(
    project: str,
    since: str | None = None,
    event_type: str | None = None,
    limit: int = 50,
    state_dir: Path | None = None,
) -> list[dict]:
    """Read events from ``events.jsonl``, optionally filtered.

    Parameters
    ----------
    project:
        Project name.
    since:
        ISO 8601 timestamp; only events at or after this time are returned.
    event_type:
        If given, only events matching this type are returned.
    limit:
        Maximum number of events to return (most recent first).
    state_dir:
        Override the default state directory.

    Returns
    -------
    list[dict]:
        Events in chronological order (oldest first), capped to *limit*.
    """
    try:
        path = _events_path(project, state_dir)
        if not path.exists():
            return []

        with open(path, "r") as fh:
            fcntl.flock(fh, fcntl.LOCK_SH)
            try:
                lines = fh.readlines()
            finally:
                fcntl.flock(fh, fcntl.LOCK_UN)

        events: list[dict] = []
        for raw in lines:
            raw = raw.strip()
            if not raw:
                continue
            try:
                ev = json.loads(raw)
            except json.JSONDecodeError:
                continue

            if event_type and ev.get("type") != event_type:
                continue
            if since and ev.get("ts", "") < since:
                continue

            events.append(ev)

        # Return the *last* `limit` entries (most recent), preserving
        # chronological order.
        if limit and len(events) > limit:
            events = events[-limit:]

        return events

    except Exception as exc:
        print(f"[claw_town_events] read_events error: {exc}", file=sys.stderr)
        return []


def last_event_of_type(
    project: str,
    event_type: str,
    state_dir: Path | None = None,
) -> dict | None:
    """Return the most recent event of the given *event_type*, or ``None``."""
    try:
        path = _events_path(project, state_dir)
        if not path.exists():
            return None

        with open(path, "r") as fh:
            fcntl.flock(fh, fcntl.LOCK_SH)
            try:
                lines = fh.readlines()
            finally:
                fcntl.flock(fh, fcntl.LOCK_UN)

        # Walk backwards to find the most recent match quickly.
        for raw in reversed(lines):
            raw = raw.strip()
            if not raw:
                continue
            try:
                ev = json.loads(raw)
            except json.JSONDecodeError:
                continue
            if ev.get("type") == event_type:
                return ev

        return None

    except Exception as exc:
        print(
            f"[claw_town_events] last_event_of_type error: {exc}",
            file=sys.stderr,
        )
        return None


# ---------------------------------------------------------------------------
# Truncation
# ---------------------------------------------------------------------------


def _truncate_if_needed(path: Path) -> None:
    """If the log exceeds ``_MAX_EVENTS`` lines, keep only the last 500."""
    try:
        with open(path, "r+") as fh:
            fcntl.flock(fh, fcntl.LOCK_EX)
            try:
                lines = fh.readlines()
                if len(lines) <= _MAX_EVENTS:
                    return
                kept = lines[-_MAX_EVENTS:]
                fh.seek(0)
                fh.writelines(kept)
                fh.truncate()
            finally:
                fcntl.flock(fh, fcntl.LOCK_UN)
    except Exception as exc:
        print(
            f"[claw_town_events] truncate error: {exc}",
            file=sys.stderr,
        )


# ---------------------------------------------------------------------------
# CLI for testing
# ---------------------------------------------------------------------------


def _cli() -> None:
    parser = argparse.ArgumentParser(
        description="Claw Town event log utility",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    # -- log --
    log_p = sub.add_parser("log", help="Log an event")
    log_p.add_argument("--project", required=True, help="Project name")
    log_p.add_argument(
        "--type",
        required=True,
        choices=ALL_EVENT_TYPES,
        help="Event type",
    )
    log_p.add_argument("--summary", required=True, help="Event summary")
    log_p.add_argument("--details", default=None, help="Optional details")
    log_p.add_argument(
        "--state-dir",
        default=None,
        type=Path,
        help="Override state dir",
    )

    # -- read --
    read_p = sub.add_parser("read", help="Read events")
    read_p.add_argument("--project", required=True, help="Project name")
    read_p.add_argument("--since", default=None, help="ISO timestamp filter")
    read_p.add_argument("--type", default=None, help="Event type filter")
    read_p.add_argument(
        "--limit",
        type=int,
        default=50,
        help="Max events to return",
    )
    read_p.add_argument(
        "--state-dir",
        default=None,
        type=Path,
        help="Override state dir",
    )

    # -- last --
    last_p = sub.add_parser("last", help="Last event of a given type")
    last_p.add_argument("--project", required=True, help="Project name")
    last_p.add_argument(
        "--type",
        required=True,
        choices=ALL_EVENT_TYPES,
        help="Event type",
    )
    last_p.add_argument(
        "--state-dir",
        default=None,
        type=Path,
        help="Override state dir",
    )

    args = parser.parse_args()

    if args.command == "log":
        log_event(
            project=args.project,
            event_type=args.type,
            summary=args.summary,
            details=args.details,
            state_dir=args.state_dir,
        )
        print(f"Logged {args.type} event for project '{args.project}'")

    elif args.command == "read":
        events = read_events(
            project=args.project,
            since=args.since,
            event_type=args.type,
            limit=args.limit,
            state_dir=args.state_dir,
        )
        for ev in events:
            print(json.dumps(ev, ensure_ascii=False))
        if not events:
            print("(no events)", file=sys.stderr)

    elif args.command == "last":
        ev = last_event_of_type(
            project=args.project,
            event_type=args.type,
            state_dir=args.state_dir,
        )
        if ev:
            print(json.dumps(ev, ensure_ascii=False))
        else:
            print(f"(no {args.type} events found)", file=sys.stderr)


if __name__ == "__main__":
    _cli()
