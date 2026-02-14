#!/usr/bin/env python3
"""
Claw Town Agent Lifecycle Manager

Tracks agent health via a registry in .claw_town/agents/<name>.json.
Provides list/status/respawn/kill commands for managing agents.

Each agent health file contains:
    {
        "name": "fix-spawn",
        "task_id": "T002",
        "window": "claw-town-floatinghotel:fix-spawn",
        "pid": 12345,
        "status": "alive",
        "last_heartbeat": "2026-02-14T06:00:00Z",
        "started_at": "2026-02-14T05:50:00Z",
        "agent_mode": "headless",
        "missed_heartbeats": 0
    }

Usage:
    python3 claw_town_agents.py list
    python3 claw_town_agents.py status <name>
    python3 claw_town_agents.py respawn <name>
    python3 claw_town_agents.py kill <name>
    python3 claw_town_agents.py heartbeat <name>
    python3 claw_town_agents.py check-health
    python3 claw_town_agents.py register <name> --task-id T004 --window claw-town-floatinghotel:reliability --pid 1234
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------

def _get_project_dir() -> Path:
    """Get project directory from env or cwd."""
    env = os.environ.get("CLAW_TOWN_PROJECT_DIR")
    if env:
        return Path(env)
    return Path.cwd()


def _get_state_dir() -> Path:
    """Get .claw_town state directory."""
    return _get_project_dir() / ".claw_town"


def _agents_dir() -> Path:
    """Get .claw_town/agents/ directory."""
    d = _get_state_dir() / "agents"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _locks_dir() -> Path:
    """Get .claw_town/locks/ directory for per-agent locks."""
    d = _get_state_dir() / "locks"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _agent_file(name: str) -> Path:
    """Path to an agent's health file."""
    return _agents_dir() / f"{name}.json"


def _agent_lock_file(name: str) -> Path:
    """Path to an agent's lock file."""
    return _locks_dir() / f"{name}.lock"


# --------------------------------------------------------------------------
# Agent health file I/O
# --------------------------------------------------------------------------

def _read_agent(name: str) -> dict[str, Any] | None:
    """Read an agent health file. Returns None if not found."""
    path = _agent_file(name)
    if not path.exists():
        return None
    try:
        with open(path) as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def _write_agent(name: str, data: dict[str, Any]) -> None:
    """Write an agent health file atomically."""
    path = _agent_file(name)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_fd, tmp_path = tempfile.mkstemp(dir=str(path.parent), suffix=".tmp")
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


def _read_all_agents() -> list[dict[str, Any]]:
    """Read all agent health files."""
    agents_d = _agents_dir()
    agents = []
    for f in sorted(agents_d.glob("*.json")):
        try:
            with open(f) as fh:
                agents.append(json.load(fh))
        except (json.JSONDecodeError, OSError):
            continue
    return agents


def _delete_agent(name: str) -> bool:
    """Remove an agent health file."""
    path = _agent_file(name)
    if path.exists():
        path.unlink()
        return True
    return False


# --------------------------------------------------------------------------
# tmux helpers
# --------------------------------------------------------------------------

def _get_tmux_session() -> str:
    """Get the tmux session name from tasks.json or default."""
    tasks_file = _get_state_dir() / "tasks.json"
    if tasks_file.exists():
        try:
            with open(tasks_file) as f:
                data = json.load(f)
            project = data.get("project", "unknown")
            return f"claw-town-{project}"
        except (json.JSONDecodeError, OSError):
            pass
    return "claw-town-floatinghotel"


def _tmux_window_exists(session: str, window: str) -> bool:
    """Check if a tmux window exists in the given session."""
    try:
        result = subprocess.run(
            ["tmux", "list-windows", "-t", session, "-F", "#{window_name}"],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode != 0:
            return False
        return window in result.stdout.strip().split("\n")
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return False


def _tmux_pane_pid(session: str, window: str) -> int | None:
    """Get the pane PID for a tmux window."""
    try:
        result = subprocess.run(
            ["tmux", "list-panes", "-t", f"{session}:{window}", "-F", "#{pane_pid}"],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode != 0:
            return None
        pid_str = result.stdout.strip().split("\n")[0]
        return int(pid_str) if pid_str else None
    except (subprocess.TimeoutExpired, FileNotFoundError, ValueError):
        return None


def _process_alive(pid: int) -> bool:
    """Check if a process is still alive."""
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, PermissionError):
        return False


def _has_claude_child(pid: int) -> bool:
    """Check if a claude process exists in the process tree of pid."""
    try:
        result = subprocess.run(
            ["pgrep", "-P", str(pid), "-f", "claude"],
            capture_output=True, text=True, timeout=5,
        )
        return result.returncode == 0
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return False


# --------------------------------------------------------------------------
# Health checking
# --------------------------------------------------------------------------

STUCK_THRESHOLD = 3  # missed heartbeats before marking stuck


def _determine_status(agent: dict[str, Any]) -> str:
    """Determine an agent's current status based on all signals.

    Checks tmux window, pane PID, claude process, and heartbeats.
    Returns: "alive", "stuck", or "dead".
    """
    session = _get_tmux_session()
    window_name = agent.get("name", "")

    # Check if the tmux window exists
    if not _tmux_window_exists(session, window_name):
        return "dead"

    # Check pane PID
    pane_pid = _tmux_pane_pid(session, window_name)
    if pane_pid is None:
        return "dead"

    # Check if pane process is alive
    if not _process_alive(pane_pid):
        return "dead"

    # Check if claude is running under the pane
    if not _has_claude_child(pane_pid):
        # The shell is alive but claude isn't running - could be finishing up or dead
        return "dead"

    # Check heartbeat staleness
    missed = agent.get("missed_heartbeats", 0)
    if missed >= STUCK_THRESHOLD:
        return "stuck"

    return "alive"


def check_all_health() -> list[dict[str, Any]]:
    """Check health of all registered agents and update their status."""
    agents = _read_all_agents()
    results = []

    for agent in agents:
        name = agent.get("name", "unknown")
        old_status = agent.get("status", "unknown")
        new_status = _determine_status(agent)

        if new_status != old_status:
            agent["status"] = new_status
            _write_agent(name, agent)

        results.append({
            "name": name,
            "task_id": agent.get("task_id"),
            "status": new_status,
            "old_status": old_status,
            "changed": new_status != old_status,
            "missed_heartbeats": agent.get("missed_heartbeats", 0),
        })

    return results


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------

def cmd_register(args: argparse.Namespace) -> None:
    """Register a new agent in the health registry."""
    now = datetime.now(timezone.utc).isoformat()
    name = args.name

    data: dict[str, Any] = {
        "name": name,
        "task_id": args.task_id,
        "window": args.window or f"{_get_tmux_session()}:{name}",
        "pid": args.pid,
        "status": "alive",
        "last_heartbeat": now,
        "started_at": now,
        "agent_mode": args.agent_mode or "headless",
        "role": args.role,
        "missed_heartbeats": 0,
    }

    _write_agent(name, data)

    # Also create the per-agent lock
    lock_data = {
        "agent": name,
        "session": _get_tmux_session(),
        "pid": args.pid,
        "timestamp": now,
    }
    lock_path = _agent_lock_file(name)
    tmp_fd, tmp_path = tempfile.mkstemp(dir=str(lock_path.parent), suffix=".tmp")
    try:
        with os.fdopen(tmp_fd, "w") as f:
            json.dump(lock_data, f, indent=2)
            f.write("\n")
        os.replace(tmp_path, str(lock_path))
    except BaseException:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        raise

    print(json.dumps({"registered": name, "task_id": args.task_id}))


def cmd_heartbeat(args: argparse.Namespace) -> None:
    """Record a heartbeat for an agent."""
    name = args.name
    agent = _read_agent(name)
    if agent is None:
        print(json.dumps({"error": f"Agent '{name}' not found"}), file=sys.stderr)
        sys.exit(1)

    now = datetime.now(timezone.utc).isoformat()
    agent["last_heartbeat"] = now
    agent["missed_heartbeats"] = 0
    agent["status"] = "alive"
    _write_agent(name, agent)

    print(json.dumps({"heartbeat": name, "timestamp": now}))


def cmd_miss_heartbeat(args: argparse.Namespace) -> None:
    """Increment missed heartbeat count for an agent."""
    name = args.name
    agent = _read_agent(name)
    if agent is None:
        print(json.dumps({"error": f"Agent '{name}' not found"}), file=sys.stderr)
        sys.exit(1)

    missed = agent.get("missed_heartbeats", 0) + 1
    agent["missed_heartbeats"] = missed

    if missed >= STUCK_THRESHOLD:
        agent["status"] = "stuck"

    _write_agent(name, agent)
    print(json.dumps({"agent": name, "missed_heartbeats": missed, "status": agent["status"]}))


def cmd_list(args: argparse.Namespace) -> None:
    """List all agents with health info."""
    agents = _read_all_agents()
    if not agents:
        print(json.dumps({"agents": [], "count": 0}))
        return

    # Optionally refresh health
    if not args.no_refresh:
        check_all_health()
        agents = _read_all_agents()

    output = []
    for agent in agents:
        output.append({
            "name": agent.get("name"),
            "task_id": agent.get("task_id"),
            "status": agent.get("status"),
            "last_heartbeat": agent.get("last_heartbeat"),
            "started_at": agent.get("started_at"),
            "missed_heartbeats": agent.get("missed_heartbeats", 0),
            "agent_mode": agent.get("agent_mode"),
        })

    print(json.dumps({"agents": output, "count": len(output)}, indent=2))


def cmd_status(args: argparse.Namespace) -> None:
    """Show detailed status for a specific agent."""
    name = args.name
    agent = _read_agent(name)
    if agent is None:
        print(json.dumps({"error": f"Agent '{name}' not found"}), file=sys.stderr)
        sys.exit(1)

    # Refresh health
    new_status = _determine_status(agent)
    if new_status != agent.get("status"):
        agent["status"] = new_status
        _write_agent(name, agent)

    # Add live info
    session = _get_tmux_session()
    pane_pid = _tmux_pane_pid(session, name)
    lock_path = _agent_lock_file(name)

    output = dict(agent)
    output["live_pane_pid"] = pane_pid
    output["tmux_window_exists"] = _tmux_window_exists(session, name)
    output["lock_file_exists"] = lock_path.exists()
    output["tmux_session"] = session

    print(json.dumps(output, indent=2, default=str))


def cmd_kill(args: argparse.Namespace) -> None:
    """Gracefully stop an agent."""
    name = args.name
    agent = _read_agent(name)
    if agent is None:
        print(json.dumps({"error": f"Agent '{name}' not found"}), file=sys.stderr)
        sys.exit(1)

    session = _get_tmux_session()
    killed = False

    # Try to send /exit to claude first
    try:
        subprocess.run(
            ["tmux", "send-keys", "-t", f"{session}:{name}", "/exit", "Enter"],
            capture_output=True, timeout=5,
        )
        killed = True
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass

    # If the pane PID is known, send SIGTERM as fallback
    pid = agent.get("pid")
    if pid and _process_alive(pid):
        try:
            os.kill(pid, signal.SIGTERM)
            killed = True
        except (ProcessLookupError, PermissionError):
            pass

    # Update health file
    agent["status"] = "dead"
    _write_agent(name, agent)

    # Remove per-agent lock
    lock_path = _agent_lock_file(name)
    if lock_path.exists():
        lock_path.unlink()

    print(json.dumps({"killed": name, "success": killed}))


def cmd_respawn(args: argparse.Namespace) -> None:
    """Kill and respawn a dead/stuck agent."""
    name = args.name
    agent = _read_agent(name)
    if agent is None:
        print(json.dumps({"error": f"Agent '{name}' not found"}), file=sys.stderr)
        sys.exit(1)

    session = _get_tmux_session()
    task_id = agent.get("task_id", "")

    # Kill existing if still somehow alive
    pid = agent.get("pid")
    if pid and _process_alive(pid):
        try:
            os.kill(pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass

    # Kill tmux window if it exists
    if _tmux_window_exists(session, name):
        try:
            subprocess.run(
                ["tmux", "kill-window", "-t", f"{session}:{name}"],
                capture_output=True, timeout=5,
            )
        except (subprocess.TimeoutExpired, FileNotFoundError):
            pass

    # Remove old lock
    lock_path = _agent_lock_file(name)
    if lock_path.exists():
        lock_path.unlink()

    # Also remove the per-directory lock if it exists
    project_dir = _get_project_dir()
    old_lock = project_dir / ".claw-town-agent.lock"
    if old_lock.exists():
        try:
            with open(old_lock) as f:
                lock_data = json.load(f)
            if lock_data.get("agent") == name:
                old_lock.unlink()
        except (json.JSONDecodeError, OSError):
            pass

    # Look for the prompt file for this task
    prompt_dir = _get_state_dir() / "prompts"
    prompt_file = prompt_dir / f"{name}.md"

    if not prompt_file.exists():
        # Try to find it in the task data
        prompt_file = prompt_dir / f"{task_id}.md"

    if not prompt_file.exists():
        print(json.dumps({
            "error": f"Cannot respawn '{name}': no prompt file found at {prompt_file}",
            "hint": "Create a prompt file at .claw_town/prompts/<name>.md and retry",
        }), file=sys.stderr)
        # Update status but don't respawn
        agent["status"] = "dead"
        _write_agent(name, agent)
        sys.exit(1)

    working_dir = agent.get("working_dir") or str(project_dir)

    # Respawn via spawn_agent.sh
    spawn_script = project_dir / "spawn_agent.sh"
    if not spawn_script.exists():
        spawn_script = Path(os.environ.get("SCRIPTS_DIR", "scripts")) / "spawn_agent.sh"

    try:
        result = subprocess.run(
            [
                str(spawn_script),
                session,
                name,
                working_dir,
                str(prompt_file),
                "--skip-permissions",
            ],
            capture_output=True, text=True, timeout=120,
        )
        if result.returncode != 0:
            print(json.dumps({
                "error": f"spawn_agent.sh failed: {result.stderr}",
                "returncode": result.returncode,
            }), file=sys.stderr)
            sys.exit(1)

        # Update health file
        now = datetime.now(timezone.utc).isoformat()
        agent["status"] = "alive"
        agent["last_heartbeat"] = now
        agent["started_at"] = now
        agent["missed_heartbeats"] = 0
        _write_agent(name, agent)

        print(json.dumps({"respawned": name, "task_id": task_id}))
    except subprocess.TimeoutExpired:
        print(json.dumps({"error": f"spawn_agent.sh timed out for '{name}'"}), file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print(json.dumps({"error": f"spawn_agent.sh not found at {spawn_script}"}), file=sys.stderr)
        sys.exit(1)


def cmd_check_health(args: argparse.Namespace) -> None:
    """Check health of all agents and report changes."""
    results = check_all_health()

    changed = [r for r in results if r["changed"]]
    stuck = [r for r in results if r["status"] == "stuck"]
    dead = [r for r in results if r["status"] == "dead"]

    output = {
        "total": len(results),
        "alive": len([r for r in results if r["status"] == "alive"]),
        "stuck": len(stuck),
        "dead": len(dead),
        "changes": changed,
        "agents": results,
    }

    print(json.dumps(output, indent=2))


def cmd_deregister(args: argparse.Namespace) -> None:
    """Remove an agent from the health registry."""
    name = args.name
    if _delete_agent(name):
        # Also remove lock
        lock_path = _agent_lock_file(name)
        if lock_path.exists():
            lock_path.unlink()
        print(json.dumps({"deregistered": name}))
    else:
        print(json.dumps({"error": f"Agent '{name}' not found"}), file=sys.stderr)
        sys.exit(1)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Claw Town Agent Lifecycle Manager",
    )
    sub = parser.add_subparsers(dest="command", help="Available commands")

    # register
    p_reg = sub.add_parser("register", help="Register a new agent")
    p_reg.add_argument("name", help="Agent name")
    p_reg.add_argument("--task-id", required=True, help="Task T-number")
    p_reg.add_argument("--window", help="tmux window name")
    p_reg.add_argument("--pid", type=int, help="Process ID")
    p_reg.add_argument("--agent-mode", default="headless", help="Agent mode (headless/cursor)")
    p_reg.add_argument("--role", default=None, help="Pipeline role (pm, tech-lead, intern, code-reviewer, perf-checker, qa-tester, design-auditor)")

    # heartbeat
    p_hb = sub.add_parser("heartbeat", help="Record a heartbeat")
    p_hb.add_argument("name", help="Agent name")

    # miss-heartbeat
    p_miss = sub.add_parser("miss-heartbeat", help="Increment missed heartbeat count")
    p_miss.add_argument("name", help="Agent name")

    # list
    p_list = sub.add_parser("list", help="List all agents with health")
    p_list.add_argument("--no-refresh", action="store_true", help="Skip health refresh")

    # status
    p_status = sub.add_parser("status", help="Detailed agent status")
    p_status.add_argument("name", help="Agent name")

    # kill
    p_kill = sub.add_parser("kill", help="Gracefully stop an agent")
    p_kill.add_argument("name", help="Agent name")

    # respawn
    p_respawn = sub.add_parser("respawn", help="Kill and respawn a dead/stuck agent")
    p_respawn.add_argument("name", help="Agent name")

    # check-health
    sub.add_parser("check-health", help="Check health of all agents")

    # deregister
    p_dereg = sub.add_parser("deregister", help="Remove agent from registry")
    p_dereg.add_argument("name", help="Agent name")

    return parser


COMMAND_MAP = {
    "register": cmd_register,
    "heartbeat": cmd_heartbeat,
    "miss-heartbeat": cmd_miss_heartbeat,
    "list": cmd_list,
    "status": cmd_status,
    "kill": cmd_kill,
    "respawn": cmd_respawn,
    "check-health": cmd_check_health,
    "deregister": cmd_deregister,
}


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

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
