#!/usr/bin/env python3
"""
Claw Town Roles & Pipeline — Task lifecycle stage management.

Tasks move through a pipeline of stages, each owned by a role agent:
    pm -> tech-lead -> intern -> code-review -> perf-check -> qa-test -> design-audit -> done

Commands:
    python3 claw_town_roles.py pipeline
    python3 claw_town_roles.py list-available <role>
    python3 claw_town_roles.py claim <task> <role>
    python3 claw_town_roles.py release <task>
    python3 claw_town_roles.py stage <task>
    python3 claw_town_roles.py set-stage <task> <stage>
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

# Allow importing from the same directory
_SCRIPTS_DIR = str(Path(__file__).parent)
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

import claw_town_tasklib as tasklib  # noqa: E402

# --------------------------------------------------------------------------
# Pipeline definition
# --------------------------------------------------------------------------

PIPELINE_STAGES: list[str] = [
    "pm",
    "tech-lead",
    "intern",
    "code-review",
    "perf-check",
    "qa-test",
    "design-audit",
    "done",
]

VALID_STAGES: set[str] = set(PIPELINE_STAGES)

# Map each stage to the role that handles it
STAGE_TO_ROLE: dict[str, str] = {
    "pm": "pm",
    "tech-lead": "tech-lead",
    "intern": "intern",
    "code-review": "code-reviewer",
    "perf-check": "perf-checker",
    "qa-test": "qa-tester",
    "design-audit": "design-auditor",
}

VALID_ROLES: set[str] = set(STAGE_TO_ROLE.values())

# Reverse map: role -> stage it handles
ROLE_TO_STAGE: dict[str, str] = {v: k for k, v in STAGE_TO_ROLE.items()}


def next_stage(current: str) -> str | None:
    """Return the next stage in the pipeline, or None if at 'done'."""
    try:
        idx = PIPELINE_STAGES.index(current)
    except ValueError:
        return None
    if idx + 1 >= len(PIPELINE_STAGES):
        return None
    return PIPELINE_STAGES[idx + 1]


# --------------------------------------------------------------------------
# Task I/O helpers (reads/writes stage and owner fields)
# --------------------------------------------------------------------------

def _read_task(t_number: str) -> dict[str, Any]:
    """Read a task, ensuring stage and owner fields exist."""
    path = tasklib._task_path(t_number)
    if not path.exists():
        print(json.dumps({"error": f"Task {t_number} not found"}), file=sys.stderr)
        sys.exit(1)
    with open(path) as f:
        data = json.load(f)
    data.setdefault("stage", None)
    data.setdefault("owner", None)
    return data


def _read_all_tasks_with_stages() -> list[dict[str, Any]]:
    """Read all tasks, adding default stage/owner if missing."""
    tasks = tasklib._read_all_tasks()
    for t in tasks:
        t.setdefault("stage", None)
        t.setdefault("owner", None)
    return tasks


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------

def cmd_pipeline(args: argparse.Namespace) -> None:
    """Show the pipeline stage order."""
    stages = []
    for s in PIPELINE_STAGES:
        role = STAGE_TO_ROLE.get(s, "—")
        stages.append({
            "stage": s,
            "role": role,
            "position": PIPELINE_STAGES.index(s),
        })
    print(json.dumps({"pipeline": stages}, indent=2))


def cmd_list_available(args: argparse.Namespace) -> None:
    """List tasks available for a role to claim."""
    role = args.role
    if role not in VALID_ROLES:
        print(json.dumps({
            "error": f"Unknown role '{role}'",
            "valid_roles": sorted(VALID_ROLES),
        }), file=sys.stderr)
        sys.exit(1)

    # Find the stage this role handles
    stage = ROLE_TO_STAGE.get(role)
    if not stage:
        print(json.dumps({"error": f"No stage mapped to role '{role}'"}), file=sys.stderr)
        sys.exit(1)

    tasks = _read_all_tasks_with_stages()
    available = []
    for t in tasks:
        # Available = correct stage AND no current owner
        if t.get("stage") == stage and t.get("owner") is None:
            available.append({
                "t_number": t.get("t_number", ""),
                "title": t.get("title", ""),
                "stage": t.get("stage"),
                "status": t.get("status", "open"),
            })

    print(json.dumps({
        "role": role,
        "stage": stage,
        "available": available,
        "count": len(available),
    }, indent=2))


def cmd_claim(args: argparse.Namespace) -> None:
    """Claim a task for a role. Sets owner and marks in_progress."""
    t_number = tasklib._parse_t_number(args.task)
    role = args.role

    if role not in VALID_ROLES:
        print(json.dumps({
            "error": f"Unknown role '{role}'",
            "valid_roles": sorted(VALID_ROLES),
        }), file=sys.stderr)
        sys.exit(1)

    task = _read_task(t_number)
    expected_stage = ROLE_TO_STAGE.get(role)

    # Validate: task must be at the stage this role handles
    if task.get("stage") != expected_stage:
        print(json.dumps({
            "error": f"Task {t_number} is at stage '{task.get('stage')}', "
                     f"but role '{role}' handles stage '{expected_stage}'",
        }), file=sys.stderr)
        sys.exit(1)

    # Validate: task must not already be owned
    if task.get("owner") is not None:
        print(json.dumps({
            "error": f"Task {t_number} is already owned by '{task['owner']}'",
        }), file=sys.stderr)
        sys.exit(1)

    # Claim it
    task["owner"] = role
    task["status"] = "in_progress"
    tasklib._write_task(t_number, task)

    print(json.dumps({
        "t_number": t_number,
        "claimed_by": role,
        "stage": task["stage"],
    }))


def cmd_release(args: argparse.Namespace) -> None:
    """Release a task: clear owner and advance to the next pipeline stage."""
    t_number = tasklib._parse_t_number(args.task)
    task = _read_task(t_number)

    current_stage = task.get("stage")
    current_owner = task.get("owner")

    if current_owner is None:
        print(json.dumps({
            "error": f"Task {t_number} has no owner to release",
        }), file=sys.stderr)
        sys.exit(1)

    # Advance stage
    new_stage = next_stage(current_stage) if current_stage else None
    if new_stage is None and current_stage != "done":
        print(json.dumps({
            "error": f"Task {t_number} is at unknown stage '{current_stage}', cannot advance",
        }), file=sys.stderr)
        sys.exit(1)

    task["owner"] = None
    task["stage"] = new_stage or "done"

    if new_stage == "done" or new_stage is None:
        task["status"] = "closed"
    else:
        task["status"] = "open"

    tasklib._write_task(t_number, task)

    print(json.dumps({
        "t_number": t_number,
        "released_by": current_owner,
        "old_stage": current_stage,
        "new_stage": task["stage"],
        "status": task["status"],
    }))


def cmd_stage(args: argparse.Namespace) -> None:
    """Show the current stage and owner of a task."""
    t_number = tasklib._parse_t_number(args.task)
    task = _read_task(t_number)

    current_stage = task.get("stage")
    position = PIPELINE_STAGES.index(current_stage) if current_stage in PIPELINE_STAGES else None
    total = len(PIPELINE_STAGES)

    print(json.dumps({
        "t_number": t_number,
        "title": task.get("title", ""),
        "stage": current_stage,
        "owner": task.get("owner"),
        "position": position,
        "total_stages": total,
        "next_stage": next_stage(current_stage) if current_stage else None,
    }, indent=2))


def cmd_set_stage(args: argparse.Namespace) -> None:
    """Manually set a task's pipeline stage."""
    t_number = tasklib._parse_t_number(args.task)
    stage = args.stage

    if stage not in VALID_STAGES:
        print(json.dumps({
            "error": f"Unknown stage '{stage}'",
            "valid_stages": PIPELINE_STAGES,
        }), file=sys.stderr)
        sys.exit(1)

    task = _read_task(t_number)
    old_stage = task.get("stage")
    task["stage"] = stage
    # Clear owner when manually changing stage
    task["owner"] = None
    tasklib._write_task(t_number, task)

    print(json.dumps({
        "t_number": t_number,
        "old_stage": old_stage,
        "new_stage": stage,
    }))


def cmd_board(args: argparse.Namespace) -> None:
    """Show a kanban-style board of all tasks grouped by stage."""
    tasks = _read_all_tasks_with_stages()

    board: dict[str, list[dict[str, Any]]] = {s: [] for s in PIPELINE_STAGES}
    board["unassigned"] = []

    for t in tasks:
        stage = t.get("stage")
        entry = {
            "t_number": t.get("t_number", ""),
            "title": t.get("title", ""),
            "owner": t.get("owner"),
            "status": t.get("status", "open"),
        }
        if stage in board:
            board[stage].append(entry)
        else:
            board["unassigned"].append(entry)

    print(json.dumps({"board": board}, indent=2))


# Rejection rules: which stages can reject to which earlier stages
# Review/check/audit stages can reject back to intern
# Intern can reject back to pm or tech-lead
REJECT_ALLOWED: dict[str, list[str]] = {
    "code-review": ["intern"],
    "perf-check": ["intern"],
    "qa-test": ["intern"],
    "design-audit": ["intern"],
    "intern": ["pm", "tech-lead"],
    "tech-lead": ["pm"],
}


def cmd_reject(args: argparse.Namespace) -> None:
    """Reject a task back to an earlier pipeline stage with a reason."""
    t_number = tasklib._parse_t_number(args.task)
    target_stage = args.target_stage
    reason = args.reason

    if target_stage not in VALID_STAGES:
        print(json.dumps({
            "error": f"Unknown stage '{target_stage}'",
            "valid_stages": PIPELINE_STAGES,
        }), file=sys.stderr)
        sys.exit(1)

    task = _read_task(t_number)
    current_stage = task.get("stage")
    current_owner = task.get("owner")

    # Validate: current stage must be allowed to reject to target
    allowed_targets = REJECT_ALLOWED.get(current_stage, [])
    if target_stage not in allowed_targets:
        print(json.dumps({
            "error": f"Stage '{current_stage}' cannot reject to '{target_stage}'",
            "allowed_targets": allowed_targets,
        }), file=sys.stderr)
        sys.exit(1)

    # Validate: target stage must be earlier in the pipeline
    current_idx = PIPELINE_STAGES.index(current_stage) if current_stage in PIPELINE_STAGES else -1
    target_idx = PIPELINE_STAGES.index(target_stage)
    if target_idx >= current_idx:
        print(json.dumps({
            "error": f"Cannot reject forward: '{target_stage}' is not before '{current_stage}'",
        }), file=sys.stderr)
        sys.exit(1)

    # Update task: clear owner, set stage back, mark open
    task["owner"] = None
    task["stage"] = target_stage
    task["status"] = "open"
    tasklib._write_task(t_number, task)

    # Post rejection reason as a comment
    comment_content = (
        f"Rejected from {current_stage} back to {target_stage} "
        f"(by {current_owner or 'unknown'}): {reason}"
    )

    # Use tasklib's internal write to add comment
    comments = task.get("comments", [])
    comment_id = len(comments) + 1
    from datetime import datetime, timezone
    now = datetime.now(timezone.utc).isoformat()
    comments.append({
        "id": comment_id,
        "content": comment_content,
        "prefix": "REJECTED",
        "created_at": now,
    })
    task["comments"] = comments
    tasklib._write_task(t_number, task)

    print(json.dumps({
        "t_number": t_number,
        "rejected_by": current_owner,
        "old_stage": current_stage,
        "new_stage": target_stage,
        "reason": reason,
        "comment_id": comment_id,
    }))


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Claw Town Roles & Pipeline — task lifecycle stage management",
    )
    parser.add_argument(
        "--tasks-dir",
        help="Override .tasks/ directory path",
    )
    sub = parser.add_subparsers(dest="command", help="Available commands")

    # pipeline
    sub.add_parser("pipeline", help="Show pipeline stage order")

    # list-available
    p_list = sub.add_parser("list-available", help="List tasks available for a role")
    p_list.add_argument("role", help="Role name (pm, tech-lead, intern, code-reviewer, perf-checker, qa-tester, design-auditor)")

    # claim
    p_claim = sub.add_parser("claim", help="Claim a task for a role")
    p_claim.add_argument("task", help="T-number (e.g. T001)")
    p_claim.add_argument("role", help="Role name")

    # release
    p_release = sub.add_parser("release", help="Release a task and advance stage")
    p_release.add_argument("task", help="T-number (e.g. T001)")

    # stage
    p_stage = sub.add_parser("stage", help="Show task stage and owner")
    p_stage.add_argument("task", help="T-number (e.g. T001)")

    # set-stage
    p_set = sub.add_parser("set-stage", help="Manually set a task's stage")
    p_set.add_argument("task", help="T-number (e.g. T001)")
    p_set.add_argument("stage", help="Stage name", choices=PIPELINE_STAGES)

    # board
    sub.add_parser("board", help="Kanban board view of all tasks by stage")

    # reject
    p_reject = sub.add_parser("reject", help="Reject a task back to an earlier stage")
    p_reject.add_argument("task", help="T-number (e.g. T001)")
    p_reject.add_argument("target_stage", help="Stage to send task back to")
    p_reject.add_argument("--reason", required=True, help="Reason for rejection")

    return parser


COMMAND_MAP = {
    "pipeline": cmd_pipeline,
    "list-available": cmd_list_available,
    "claim": cmd_claim,
    "release": cmd_release,
    "stage": cmd_stage,
    "set-stage": cmd_set_stage,
    "board": cmd_board,
    "reject": cmd_reject,
}


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    # Override tasks dir if specified
    if args.tasks_dir:
        tasklib.TASKS_DIR = Path(args.tasks_dir)

    if not args.command:
        parser.print_help()
        sys.exit(1)

    handler = COMMAND_MAP.get(args.command)
    if handler is None:
        print(json.dumps({"error": f"Unknown command: {args.command}"}), file=sys.stderr)
        sys.exit(1)

    handler(args)


if __name__ == "__main__":
    main()
