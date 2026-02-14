#!/usr/bin/env python3
"""
Claw Town Scuba logging module — NO-OP STUB.

All functions have the same signatures as the original but do nothing.
This allows other scripts to import without breaking.
"""

from __future__ import annotations

import argparse

# Event type constants (kept for compatibility)
EVENT_SESSION_START: str = "session_start"
EVENT_SESSION_STOP: str = "session_stop"
EVENT_AGENT_SPAWN: str = "agent_spawn"
EVENT_AGENT_COMPLETE: str = "agent_complete"
EVENT_TASK_CREATE: str = "task_create"
EVENT_TASK_CLOSE: str = "task_close"
EVENT_CLONE_CREATE: str = "clone_create"


def log_event(
    event_type: str = "",
    project: str = "",
    task_id: str = "",
    agent_name: str = "",
    duration_ms: int = 0,
    status: str = "",
    error_message: str = "",
    yolo_mode: str = "false",
    session_name: str = "",
    skip_permissions: str = "false",
    working_dir: str = "",
    eden_repo_name: str = "",
    **kwargs: object,
) -> None:
    """No-op stub. Accepts all arguments and does nothing."""
    return


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Claw Town Scuba logging — no-op stub.",
    )
    parser.add_argument("--event-type", required=True)
    parser.add_argument("--project", required=True)
    parser.add_argument("--task-id", default="")
    parser.add_argument("--agent-name", default="")
    parser.add_argument("--duration-ms", type=int, default=0)
    parser.add_argument("--status", default="")
    parser.add_argument("--error-message", default="")
    parser.add_argument("--yolo-mode", default="false")
    parser.add_argument("--session-name", default="")
    parser.add_argument("--skip-permissions", default="false")
    parser.add_argument("--working-dir", default="")
    parser.add_argument("--eden-repo-name", default="")

    parser.parse_args()
    # No-op: do nothing


if __name__ == "__main__":
    main()
