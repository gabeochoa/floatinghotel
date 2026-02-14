#!/usr/bin/env python3
"""
Claw Town Paths - Central path resolution for all project file operations.

Instead of hardcoding ~/projects/<project>/.claw_town/, all scripts should
call these functions to resolve project directories. This enables storing
project files in alternative locations (e.g., a notes repo).

Configuration is stored in ~/.claw-town/config.json under "project_bases":
{
    "project_bases": {
        "my-project": "/path/to/notes-repo",
        "default": "~/projects"
    }
}
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

GLOBAL_CONFIG_PATH = Path.home() / ".claw-town" / "config.json"
DEFAULT_BASE = Path.home() / "projects"


def load_global_config() -> dict:
    """Load the global claw-town config."""
    if not GLOBAL_CONFIG_PATH.exists():
        return {}
    try:
        return json.loads(GLOBAL_CONFIG_PATH.read_text())
    except (json.JSONDecodeError, IOError):
        return {}


def save_global_config(config: dict) -> None:
    """Save the global claw-town config."""
    GLOBAL_CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    GLOBAL_CONFIG_PATH.write_text(json.dumps(config, indent=2) + "\n")


def get_project_base(project: str) -> Path:
    """Get the base directory for a project (where project dir lives)."""
    config = load_global_config()
    bases = config.get("project_bases", {})
    base = bases.get(project, bases.get("default", str(DEFAULT_BASE)))
    return Path(base).expanduser()


def get_project_dir(project: str) -> Path:
    """Get the project directory (e.g., ~/projects/<project>)."""
    return get_project_base(project) / project


def get_state_dir(project: str) -> Path:
    """Get the .claw_town state directory for a project."""
    return get_project_dir(project) / ".claw_town"


def set_project_base(project: str, base_path: str) -> None:
    """Set a custom base directory for a project (e.g., notes repo path)."""
    config = load_global_config()
    if "project_bases" not in config:
        config["project_bases"] = {}
    config["project_bases"][project] = str(Path(base_path).expanduser().resolve())
    save_global_config(config)


def remove_project_base(project: str) -> None:
    """Remove a custom base directory, reverting to default."""
    config = load_global_config()
    bases = config.get("project_bases", {})
    if project in bases:
        del bases[project]
        save_global_config(config)


def is_notes_repo_enabled(project: str) -> bool:
    """Check if a project has a custom base path (notes repo)."""
    config = load_global_config()
    bases = config.get("project_bases", {})
    return project in bases


def get_notes_repo_path(project: str) -> Optional[Path]:
    """Get the notes repo path for a project, if configured."""
    config = load_global_config()
    bases = config.get("project_bases", {})
    if project in bases:
        return Path(bases[project]).expanduser()
    return None
