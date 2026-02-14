#!/bin/bash
# Claw Town Agent Spawner - Spawns agents in tmux windows
# Usage: spawn_agent.sh <session> <window-name> <working-dir> <prompt-file> [options]
#
# Options:
#   --skip-permissions     Use --dangerously-skip-permissions with claude
#   --strict               Block spawn if prompt quality checks fail
#   --cursor               Force using Cursor agent (windowed, has display)
#   --headless             Force using Claude CLI (no display needed)
#
# Agent selection:
#   By default, tasks run headless via Claude CLI in tmux. If --cursor is passed,
#   or the prompt contains windowed-task markers (UI preview, screenshot, browser,
#   GUI interaction, image/design), Cursor agent is used instead since it has
#   display access.
#
# Features:
# - Spawns Claude (headless) or Cursor agent (windowed) in a new tmux window
# - Per-agent lock files (.claw_town/locks/<agent>.lock) allow multiple agents in same dir
# - Registers agents with the health registry (.claw_town/agents/<name>.json)
# - Prompt quality gate catches vague prompts before spawning
# - Auto-submits the prompt

set -e

# ── SCRIPTS_DIR: prefer local ./scripts/ if it exists ──
if [ -d "./scripts" ]; then
    SCRIPTS_DIR="./scripts"
elif [ -d "$HOME/.claude/skills/claw-town/scripts" ]; then
    SCRIPTS_DIR="$HOME/.claude/skills/claw-town/scripts"
else
    SCRIPTS_DIR="$(dirname "$0")"
fi
export SCRIPTS_DIR

SESSION="$1"
WINDOW="$2"
WORK_DIR="$3"
PROMPT_FILE="$4"
shift 4 || true

# Parse optional arguments
SKIP_PERMISSIONS=false
STRICT=false
FORCE_CURSOR=false
FORCE_HEADLESS=false
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-permissions)
            SKIP_PERMISSIONS=true
            shift
            ;;
        --strict)
            STRICT=true
            shift
            ;;
        --cursor)
            FORCE_CURSOR=true
            shift
            ;;
        --headless)
            FORCE_HEADLESS=true
            shift
            ;;
        --profile|--project|--diff)
            # Accepted for compatibility but ignored without Meta infra
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

if [ -z "$SESSION" ] || [ -z "$WINDOW" ] || [ -z "$WORK_DIR" ] || [ -z "$PROMPT_FILE" ]; then
    echo "Usage: spawn_agent.sh <session> <window-name> <working-dir> <prompt-file> [--skip-permissions] [--strict] [--cursor] [--headless]"
    exit 1
fi

# Validate WORK_DIR exists
if [ ! -d "$WORK_DIR" ]; then
    echo "ERROR: Working directory does not exist: $WORK_DIR"
    exit 1
fi

# ── AGENT SELECTION: Claude (headless) vs Cursor (windowed) ──
# Determines which agent runtime to use. Default is claude (headless).
# Cursor is used when --cursor is passed or the prompt contains markers
# indicating the task needs display access (UI preview, screenshots,
# browser automation, GUI interaction, image/design work).
detect_needs_display() {
    local prompt_file="$1"
    if [ "$FORCE_HEADLESS" = true ]; then
        echo "headless"
        return
    fi
    if [ "$FORCE_CURSOR" = true ]; then
        echo "cursor"
        return
    fi
    # Auto-detect: scan prompt for windowed-task markers
    if grep -qiE '(screenshot|browser|preview|visual|GUI|display|render|UI preview|open.*(app|window|browser)|image.*(review|audit|check|compare)|design.*(review|audit|check)|look.*(at|like)|appearance|pixel|layout.*(check|verify|review)|click|interact.*with|navigate.*to|open.*url)' "$prompt_file" 2>/dev/null; then
        echo "cursor"
        return
    fi
    echo "headless"
}

AGENT_MODE=$(detect_needs_display "$PROMPT_FILE")
echo "spawn_agent.sh: Agent mode: $AGENT_MODE (force_cursor=$FORCE_CURSOR, force_headless=$FORCE_HEADLESS)"

# ── PRE-SPAWN PROMPT QUALITY GATE ──
# Bad prompts scale damage proportionally to agent count. Check quality before
# creating tmux windows. Non-blocking by default; --strict makes it blocking.
check_prompt_quality() {
    local prompt_file="$1"
    local strict="$2"
    local warnings=0

    if [ ! -f "$prompt_file" ]; then
        echo "[PROMPT-QUALITY] ERROR: Prompt file not found: $prompt_file"
        return 1
    fi

    local word_count
    word_count=$(wc -w < "$prompt_file" | tr -d ' ')

    # Word count heuristic — under 50 words is too vague for an agent
    if [ "$word_count" -lt 50 ]; then
        echo "[PROMPT-QUALITY] WARNING: Prompt is only $word_count words (recommended: 50+). Vague prompts scale damage across agents."
        warnings=$((warnings + 1))
    fi

    # Check for a clear task description (what to do)
    if ! grep -qiE '(## |task:|goal:|objective:|what to |implement |build |create |fix |add |update |refactor |investigate |analyze |migrate |design |remove |delete )' "$prompt_file"; then
        echo "[PROMPT-QUALITY] WARNING: No clear task description found. Include what the agent should do."
        warnings=$((warnings + 1))
    fi

    # Check for acceptance criteria or steps (how to know it's done)
    if ! grep -qiE '(steps:|acceptance.criteria|done when|definition.of.done|## steps|## acceptance|success.criteria|TASK_COMPLETE|- \[ \])' "$prompt_file"; then
        echo "[PROMPT-QUALITY] WARNING: No acceptance criteria or steps found. Include 'Steps:' or acceptance criteria so the agent knows when it's done."
        warnings=$((warnings + 1))
    fi

    # Check for file/path references (where to work)
    if ! grep -qiE '([a-zA-Z0-9_]+\.(py|ts|js|cpp|h|rs|sh|md|go|rb|java|swift|kt)|/[a-zA-Z_-]+/[a-zA-Z_-]+/)' "$prompt_file"; then
        echo "[PROMPT-QUALITY] WARNING: No file or path references found. Include specific files/directories so the agent knows where to work."
        warnings=$((warnings + 1))
    fi

    if [ "$warnings" -gt 0 ]; then
        echo "[PROMPT-QUALITY] $warnings warning(s) found. Consider improving the prompt before spawning agents."
        if [ "$strict" = true ]; then
            echo "[PROMPT-QUALITY] STRICT MODE: Blocking spawn due to prompt quality issues."
            return 1
        fi
    else
        echo "[PROMPT-QUALITY] Prompt looks good ($word_count words, has task description, criteria, and file references)."
    fi

    return 0
}

# Run prompt quality check before any resource allocation
if ! check_prompt_quality "$PROMPT_FILE" "$STRICT"; then
    exit 1
fi

# Use env var to override skip_permissions
if [ "${CLAW_TOWN_SKIP_PERMISSIONS:-}" = "1" ]; then
    SKIP_PERMISSIONS=true
fi

# ── LOCK: Per-agent lock prevents spawning the same agent twice ──
# Multiple agents can work in the same directory. Each agent gets its own lock
# in .claw_town/locks/<agent>.lock. The old per-directory lock is checked for
# backward compatibility but new locks are per-agent.
CLAW_TOWN_DIR="$(dirname "$WORK_DIR")/.claw_town"
if [ -d "$WORK_DIR/.claw_town" ]; then
    CLAW_TOWN_DIR="$WORK_DIR/.claw_town"
fi
LOCKS_DIR="$CLAW_TOWN_DIR/locks"
mkdir -p "$LOCKS_DIR"
LOCKFILE="$LOCKS_DIR/${WINDOW}.lock"
AGENTS_SCRIPT="$SCRIPTS_DIR/claw_town_agents.py"

if [ -f "$LOCKFILE" ]; then
    LOCK_AGENT=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('agent',''))" "$LOCKFILE" 2>/dev/null || echo "")
    LOCK_SESSION=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('session',''))" "$LOCKFILE" 2>/dev/null || echo "")
    LOCK_SPAWN_PID=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('spawn_pid',''))" "$LOCKFILE" 2>/dev/null || echo "")
    LOCK_PANE_PID=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('pane_pid',''))" "$LOCKFILE" 2>/dev/null || echo "")

    LOCK_ALIVE=false
    # Check 1: Is the pane process still alive? (most reliable for running agents)
    if [ -n "$LOCK_PANE_PID" ] && kill -0 "$LOCK_PANE_PID" 2>/dev/null; then
        LOCK_ALIVE=true
    fi
    # Check 2: Is the spawner process still running? (covers mid-startup)
    if [ "$LOCK_ALIVE" = false ] && [ -n "$LOCK_SPAWN_PID" ] && kill -0 "$LOCK_SPAWN_PID" 2>/dev/null; then
        LOCK_ALIVE=true
    fi
    # Check 3: Does the agent's tmux window still exist? (covers running agent)
    if [ "$LOCK_ALIVE" = false ] && [ -n "$LOCK_AGENT" ] && [ -n "$LOCK_SESSION" ]; then
        if tmux list-windows -t "$LOCK_SESSION" -F '#{window_name}' 2>/dev/null | grep -q "^${LOCK_AGENT}$"; then
            LOCK_ALIVE=true
        fi
    fi

    if [ "$LOCK_ALIVE" = true ]; then
        echo "ERROR: Agent '$WINDOW' is already running (session: $LOCK_SESSION)"
        echo "Lock file: $LOCKFILE"
        echo "Use 'python3 scripts/claw_town_agents.py kill $WINDOW' to stop it first."
        exit 1
    else
        echo "WARNING: Removing stale lock from dead agent '$LOCK_AGENT'"
        rm -f "$LOCKFILE"
    fi
fi

# Write per-agent lock atomically: temp file + mv (rename is atomic on same filesystem)
LOCK_TMP="${LOCKFILE}.$$"
python3 -c "
import json, sys, time
with open(sys.argv[1], 'w') as f:
    json.dump({
        'agent': sys.argv[2],
        'session': sys.argv[3],
        'spawn_pid': int(sys.argv[4]),
        'working_dir': sys.argv[5],
        'timestamp': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    }, f)
" "$LOCK_TMP" "$WINDOW" "$SESSION" "$$" "$WORK_DIR"
mv "$LOCK_TMP" "$LOCKFILE"

# Verify we won the race
sleep 0.2
VERIFY_AGENT=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('agent',''))" "$LOCKFILE" 2>/dev/null || echo "")
VERIFY_PID=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('spawn_pid',''))" "$LOCKFILE" 2>/dev/null || echo "")
if [ "$VERIFY_AGENT" != "$WINDOW" ] || [ "$VERIFY_PID" != "$$" ]; then
    echo "ERROR: Lost lock race for agent '$WINDOW'"
    echo "  We are: $WINDOW (pid $$)"
    echo "  Winner: $VERIFY_AGENT (pid $VERIFY_PID)"
    exit 1
fi
echo "spawn_agent.sh: Acquired per-agent lock for '$WINDOW' (dir: $WORK_DIR)"

# Log working directory for debugging
echo "spawn_agent.sh: Using working directory: $WORK_DIR"

# ── Helper: check for a named process in a process tree (portable, no pstree) ──
# Uses pgrep -P to walk the tree recursively. Works on macOS and Linux.
has_process_in_tree() {
    local parent_pid="$1"
    local process_name="$2"
    local children
    children=$(pgrep -P "$parent_pid" 2>/dev/null) || return 1
    for child in $children; do
        local cmd
        cmd=$(ps -o comm= -p "$child" 2>/dev/null || echo "")
        if echo "$cmd" | grep -qi "$process_name"; then
            return 0
        fi
        # Recurse into children
        if has_process_in_tree "$child" "$process_name"; then
            return 0
        fi
    done
    return 1
}

# Create enhanced prompt with completion protocol
ENHANCED_PROMPT_FILE="/tmp/claw_town_enhanced_${WINDOW}_$$.txt"

# Write the enhanced prompt: completion protocol FIRST, then task content
{
    echo "## COMPLETION PROTOCOL"
    echo ""
    echo "When you have finished the task:"
    echo ""
    echo "1. Run this bash command to print the completion signal:"
    echo ""
    echo '```bash'
    echo "echo 'Task complete.'"
    echo '```'
    echo ""
    echo "2. Say: TASK_COMPLETE: <one-line summary of what you did>"
    echo ""
    echo "### Do NOT"
    echo ""
    echo "- Write completion notes to files"
    echo "- Create agent_results/, findings/, or notes/ directories"
    echo "- Say TASK_COMPLETE without actually finishing the work"
    echo ""
    echo "---"
    echo ""
} > "$ENHANCED_PROMPT_FILE"

# Append original task prompt
cat "$PROMPT_FILE" >> "$ENHANCED_PROMPT_FILE"

# ── SPAWN AGENT ──
# Two paths: headless (claude CLI in tmux) or windowed (cursor agent in tmux)

if [ "$AGENT_MODE" = "cursor" ]; then
    # ── CURSOR AGENT PATH (windowed — has display access) ──
    echo "spawn_agent.sh: Using Cursor agent for windowed task"

    TEMP_WINDOW="init-$$-$(date +%s)"
    tmux new-window -d -t "$SESSION" -n "$TEMP_WINDOW"
    sleep 2

    CURSOR_CMD="cd \"$WORK_DIR\" && cursor agent --workspace \"$WORK_DIR\""
    if [ "$SKIP_PERMISSIONS" = true ]; then
        CURSOR_CMD="$CURSOR_CMD --force"
    fi
    # Pass the enhanced prompt as a quoted argument
    CURSOR_CMD="$CURSOR_CMD \"\$(cat '$ENHANCED_PROMPT_FILE')\""

    tmux send-keys -t "$SESSION:$TEMP_WINDOW" "$CURSOR_CMD" Enter

    # Wait for cursor agent to start
    MAX_WAIT=90
    ELAPSED=0
    echo "Waiting for Cursor agent to start in $SESSION:$TEMP_WINDOW..."
    while [ $ELAPSED -lt $MAX_WAIT ]; do
        PANE_PID=$(tmux list-panes -t "$SESSION:$TEMP_WINDOW" -F '#{pane_pid}' 2>/dev/null || echo "")
        if [ -n "$PANE_PID" ]; then
            if has_process_in_tree "$PANE_PID" "cursor"; then
                echo "Cursor agent process detected after ${ELAPSED}s"
                PANE_PID_FOR_LOCK="$PANE_PID"
                python3 -c "
import json, sys, time
with open(sys.argv[1], 'w') as f:
    json.dump({
        'agent': sys.argv[2],
        'session': sys.argv[3],
        'pane_pid': int(sys.argv[4]),
        'spawn_pid': int(sys.argv[5]),
        'agent_mode': 'cursor',
        'working_dir': sys.argv[6],
        'timestamp': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    }, f)
" "$LOCKFILE" "$WINDOW" "$SESSION" "$PANE_PID_FOR_LOCK" "$$" "$WORK_DIR"
                # Register with health registry
                python3 "$AGENTS_SCRIPT" register "$WINDOW" --task-id "${WINDOW}" --window "$SESSION:$WINDOW" --pid "$PANE_PID_FOR_LOCK" --agent-mode cursor 2>/dev/null || true
                sleep 3
                break
            fi
        fi
        sleep 1
        ELAPSED=$((ELAPSED + 1))
    done

    if [ $ELAPSED -ge $MAX_WAIT ]; then
        echo "ERROR: Cursor agent did not start within ${MAX_WAIT}s"
        echo "Pane content:"
        tmux capture-pane -t "$SESSION:$TEMP_WINDOW" -p -S -15
        rm -f "$LOCKFILE"
        python3 "$AGENTS_SCRIPT" deregister "$WINDOW" 2>/dev/null || true
        tmux rename-window -t "$SESSION:$TEMP_WINDOW" "$WINDOW"
        exit 1
    fi

    # Rename to real window name
    tmux rename-window -t "$SESSION:$TEMP_WINDOW" "$WINDOW"

else
    # ── CLAUDE CLI PATH (headless — default) ──
    TEMP_WINDOW="init-$$-$(date +%s)"
    tmux new-window -d -t "$SESSION" -n "$TEMP_WINDOW"

    # Brief pause to let the window initialize
    sleep 2

    # Start Claude
    if [ "$SKIP_PERMISSIONS" = true ]; then
        tmux send-keys -t "$SESSION:$TEMP_WINDOW" "cd \"$WORK_DIR\" && claude --dangerously-skip-permissions" Enter
    else
        tmux send-keys -t "$SESSION:$TEMP_WINDOW" "cd \"$WORK_DIR\" && claude" Enter
    fi

    # Wait for Claude to be ready by checking the process tree for a running claude
    # process. We can't rely on prompt character detection because Claude Code
    # uses an alternate screen buffer that tmux capture-pane doesn't always show.
    MAX_WAIT=90
    ELAPSED=0
    echo "Waiting for Claude to start in $SESSION:$TEMP_WINDOW..."
    while [ $ELAPSED -lt $MAX_WAIT ]; do
        PANE_PID=$(tmux list-panes -t "$SESSION:$TEMP_WINDOW" -F '#{pane_pid}' 2>/dev/null || echo "")
        if [ -n "$PANE_PID" ]; then
            if has_process_in_tree "$PANE_PID" "claude"; then
                echo "Claude process detected after ${ELAPSED}s"
                # Update lock with pane PID now that Claude is running
                PANE_PID_FOR_LOCK="$PANE_PID"
                python3 -c "
import json, sys, time
with open(sys.argv[1], 'w') as f:
    json.dump({
        'agent': sys.argv[2],
        'session': sys.argv[3],
        'pane_pid': int(sys.argv[4]),
        'spawn_pid': int(sys.argv[5]),
        'agent_mode': 'headless',
        'working_dir': sys.argv[6],
        'timestamp': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    }, f)
" "$LOCKFILE" "$WINDOW" "$SESSION" "$PANE_PID_FOR_LOCK" "$$" "$WORK_DIR"
                # Register with health registry
                python3 "$AGENTS_SCRIPT" register "$WINDOW" --task-id "${WINDOW}" --window "$SESSION:$WINDOW" --pid "$PANE_PID_FOR_LOCK" --agent-mode headless 2>/dev/null || true
                # Give Claude a moment to fully initialize its TUI
                sleep 3
                break
            fi
        fi
        sleep 1
        ELAPSED=$((ELAPSED + 1))
    done

    if [ $ELAPSED -ge $MAX_WAIT ]; then
        echo "ERROR: Claude did not start within ${MAX_WAIT}s"
        echo "Pane content:"
        tmux capture-pane -t "$SESSION:$TEMP_WINDOW" -p -S -15
        # Clean up lock and health registry since agent never started
        rm -f "$LOCKFILE"
        python3 "$AGENTS_SCRIPT" deregister "$WINDOW" 2>/dev/null || true
        # Rename window so it can be debugged
        tmux rename-window -t "$SESSION:$TEMP_WINDOW" "$WINDOW"
        exit 1
    fi

    # Now rename the window to the actual task name
    tmux rename-window -t "$SESSION:$TEMP_WINDOW" "$WINDOW"

    # Load enhanced prompt from file and paste it
    tmux load-buffer "$ENHANCED_PROMPT_FILE"
    tmux paste-buffer -t "$SESSION:$WINDOW"

    # CRITICAL: Send Enter separately to submit the pasted prompt
    sleep 0.5
    tmux send-keys -t "$SESSION:$WINDOW" Enter
fi

echo "Agent $WINDOW spawned via $AGENT_MODE in $WORK_DIR"

# Cleanup temp file after a delay (in background)
(sleep 60 && rm -f "$ENHANCED_PROMPT_FILE") &
