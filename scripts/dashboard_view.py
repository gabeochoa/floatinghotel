#!/usr/bin/env python3
# (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
"""
Dashboard View Layer — TUI rendering, colors, layout, keyboard/mouse.

This module contains all terminal/visual concerns: ANSI colors, box drawing,
terminal setup/teardown, and keyboard/mouse input handling.

Separated from data logic so the data layer can be tested without terminal deps.
"""
# @noautodeps

from __future__ import annotations

import re
import sys
import termios
import tty

from dashboard_data import AgentStatus


# ═══════════════════════════════════════════════════════════════════════════════
# ANSI Colors and Box Drawing
# ═══════════════════════════════════════════════════════════════════════════════


class Theme:
    """Color theme for Claw Town dashboard."""

    # Reset
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    ITALIC = "\033[3m"
    UNDERLINE = "\033[4m"
    BLINK = "\033[5m"
    REVERSE = "\033[7m"

    # Foreground colors (bright versions for better visibility)
    BLACK = "\033[30m"
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    MAGENTA = "\033[95m"
    CYAN = "\033[96m"
    WHITE = "\033[97m"
    GRAY = "\033[90m"
    ORANGE = "\033[38;5;208m"
    LOBSTER_RED = "\033[38;5;160m"

    # Background colors
    BG_BLACK = "\033[40m"
    BG_RED = "\033[41m"
    BG_GREEN = "\033[42m"
    BG_YELLOW = "\033[43m"
    BG_BLUE = "\033[44m"
    BG_MAGENTA = "\033[45m"
    BG_CYAN = "\033[46m"
    BG_WHITE = "\033[47m"
    BG_ORANGE = "\033[48;5;208m"
    BG_LOBSTER_RED = "\033[48;5;160m"

    # Box drawing characters (Unicode)
    BOX_TL = "\u256d"
    BOX_TR = "\u256e"
    BOX_BL = "\u2570"
    BOX_BR = "\u256f"
    BOX_H = "\u2500"
    BOX_V = "\u2502"
    BOX_T = "\u252c"
    BOX_B = "\u2534"
    BOX_L = "\u251c"
    BOX_R = "\u2524"
    BOX_X = "\u253c"

    # Sharp corners for headers
    SHARP_TL = "\u250c"
    SHARP_TR = "\u2510"
    SHARP_BL = "\u2514"
    SHARP_BR = "\u2518"

    # Double lines for emphasis
    DBL_H = "\u2550"
    DBL_V = "\u2551"
    DBL_TL = "\u2554"
    DBL_TR = "\u2557"
    DBL_BL = "\u255a"
    DBL_BR = "\u255d"

    # Tree characters
    TREE_L = "\u2514"
    TREE_T = "\u251c"
    TREE_H = "\u2500"
    TREE_V = "\u2502"

    # Arrows and symbols
    ARROW_R = "\u2192"
    ARROW_L = "\u2190"
    ARROW_U = "\u2191"
    ARROW_D = "\u2193"
    BULLET = "\u2022"
    CHECK = "\u2713"
    CROSS = "\u2717"
    STAR = "\u2605"
    DIAMOND = "\u25c6"
    CIRCLE = "\u25cb"
    CIRCLE_FILLED = "\u25cf"
    CIRCLE_DOT = "\u25c9"

    # Status icons (using emoji for clarity)
    ICON_WORKING = "\u2699\ufe0f "
    ICON_IDLE = "\U0001f4a4"
    ICON_FREE = "\u2713 "
    ICON_ERROR = "\u26a0\ufe0f "
    ICON_BLOCKED = "\U0001f512"
    ICON_READY = "\u25c9"
    ICON_WAITING_HUMAN = "\U0001f464"
    ICON_WAITING_AGENT = "\u23f3"
    ICON_WAITING_SUB = "\U0001f504"
    ICON_SUB_ORCH = "\U0001f4e6"


T = Theme


# ═══════════════════════════════════════════════════════════════════════════════
# Text Utilities
# ═══════════════════════════════════════════════════════════════════════════════


def strip_ansi(text: str) -> int:
    """Get display width of text without ANSI codes."""
    return len(re.sub(r"\033\[[0-9;]*m", "", str(text)))


def ansi_pad(text: str, width: int) -> str:
    """Pad text to width, accounting for ANSI escape codes."""
    visible_len = strip_ansi(text)
    if visible_len >= width:
        return text
    return text + " " * (width - visible_len)


def truncate(text: str, width: int, suffix: str = "\u2026") -> str:
    """Truncate text to width, accounting for ANSI codes."""
    if strip_ansi(text) <= width:
        return text

    result = []
    visible_len = 0
    in_escape = False

    for char in text:
        if char == "\033":
            in_escape = True
            result.append(char)
        elif in_escape:
            result.append(char)
            if char == "m":
                in_escape = False
        else:
            if visible_len < width - len(suffix):
                result.append(char)
                visible_len += 1
            else:
                result.append(suffix)
                break

    result.append(T.RESET)
    return "".join(result)


# ═══════════════════════════════════════════════════════════════════════════════
# Box Drawing Functions
# ═══════════════════════════════════════════════════════════════════════════════


def draw_box(
    title: str,
    content: list[str],
    width: int,
    color: str = T.CYAN,
) -> list[str]:
    """Draw a box with title and content."""
    lines = []
    inner_width = width - 2

    # Top border with title
    title_display = f" {title} "
    remaining = inner_width - strip_ansi(title_display)
    left_border = T.BOX_H * 2
    right_border = T.BOX_H * max(0, remaining - 2)
    lines.append(
        f"{color}{T.BOX_TL}{left_border}{T.RESET}"
        f"{color}{T.BOLD}{title_display}{T.RESET}"
        f"{color}{right_border}{T.BOX_TR}{T.RESET}"
    )

    # Content lines
    for line in content:
        visible_width = strip_ansi(line)
        padding = max(0, inner_width - visible_width)
        lines.append(
            f"{color}{T.BOX_V}{T.RESET}{line}{' ' * padding}{color}{T.BOX_V}{T.RESET}"
        )

    # Bottom border
    lines.append(f"{color}{T.BOX_BL}{T.BOX_H * inner_width}{T.BOX_BR}{T.RESET}")

    return lines


def draw_progress_bar(
    current: int, total: int, width: int = 20, color: str = T.GREEN
) -> str:
    """Draw a progress bar."""
    if total == 0:
        pct = 0
        filled = 0
    else:
        pct = int((current / total) * 100)
        filled = int(width * current / total)

    empty = width - filled
    filled_char = "\u2588"
    empty_char = "\u2591"
    bar = f"{color}{filled_char * filled}{T.DIM}{empty_char * empty}{T.RESET}"
    return f"[{bar}] {pct}%"


def get_status_display(status: str) -> tuple[str, str]:
    """Get icon and color for agent status."""
    if status == AgentStatus.WORKING:
        return T.ICON_WORKING, T.YELLOW
    elif status == AgentStatus.NEEDS_HUMAN:
        return "\U0001f464 ", T.MAGENTA
    elif status == AgentStatus.NEEDS_ORCHESTRATOR:
        return "\U0001f3af ", T.CYAN
    elif status == AgentStatus.NEEDS_AGENT:
        return "\u23f3 ", T.RED
    elif status == AgentStatus.SLEEPING:
        return "\U0001f4a4 ", T.GRAY
    elif status == AgentStatus.NEEDS_INPUT:
        return T.ICON_IDLE, T.MAGENTA
    elif status == AgentStatus.FREE:
        return T.ICON_FREE, T.GREEN
    elif status == AgentStatus.COMPLETED:
        return T.ICON_FREE, T.GREEN
    else:
        return "? ", T.GRAY


# ═══════════════════════════════════════════════════════════════════════════════
# Terminal Setup / Teardown
# ═══════════════════════════════════════════════════════════════════════════════


def setup_terminal() -> None:
    """Enable alternate screen buffer.

    Mouse tracking is enabled separately in Dashboard.run() so that
    scroll events reach this pane even when it is not focused.
    """
    sys.stdout.write(
        "\033[?1049h"  # alternate screen buffer
    )
    sys.stdout.flush()


def cleanup_terminal() -> None:
    """Restore normal screen buffer."""
    sys.stdout.write("\033[?1049l")
    sys.stdout.flush()


def query_cursor_row() -> int | None:
    """Query the terminal for the current cursor row using ANSI DSR.

    Returns the 1-based row number, or None if query fails.
    """
    import select
    import time

    if not sys.stdout.isatty() or not sys.stdin.isatty():
        return None
    try:
        old_settings = termios.tcgetattr(sys.stdin)
        try:
            tty.setcbreak(sys.stdin.fileno())
            sys.stdout.flush()
            sys.stdout.write("\033[6n")
            sys.stdout.flush()
            response = ""
            deadline = time.time() + 0.1
            while time.time() < deadline:
                remaining = max(0.001, deadline - time.time())
                if select.select([sys.stdin], [], [], remaining)[0]:
                    ch = sys.stdin.read(1)
                    response += ch
                    if ch == "R":
                        break
                else:
                    break
            m = re.search(r"\033\[(\d+);(\d+)R", response)
            if m:
                return int(m.group(1))
        finally:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
    except Exception:
        pass
    return None
