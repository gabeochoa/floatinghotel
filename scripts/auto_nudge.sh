#!/bin/bash
# Claw Town Auto-Nudge Monitor
# Loops forever, checks all tmux panes every 60s, auto-approves idle agents,
# sends queued messages from the outbox, and maintains agent heartbeats.
#
# Usage: scripts/auto_nudge.sh [--interval SECS] [--dry-run]

set -euo pipefail

SESSION="claw-town-floatinghotel"
PROJECT_DIR="/Users/gabeochoa/p/floatinghotel"
OUTBOX_PENDING="$PROJECT_DIR/.claw_town/outbox/pending"
OUTBOX_SENT="$PROJECT_DIR/.claw_town/outbox/sent"
OUTBOX_EXPIRED="$PROJECT_DIR/.claw_town/outbox/expired"
AGENTS_DIR="$PROJECT_DIR/.claw_town/agents"
AGENTS_SCRIPT="$PROJECT_DIR/scripts/claw_town_agents.py"
INTERVAL=60
DRY_RUN=false
CONTROL_PANE="control"
# Messages older than this (seconds) are expired
MESSAGE_TTL=600
# Directory to store previous pane snapshots for staleness detection
SNAPSHOTS_DIR="/tmp/claw_town_snapshots"
mkdir -p "$SNAPSHOTS_DIR"

# Parse args
while [ $# -gt 0 ]; do
    case "$1" in
        --interval) INTERVAL="$2"; shift 2 ;;
        --dry-run) DRY_RUN=true; shift ;;
        *) shift ;;
    esac
done

log() {
    echo "[$(date '+%H:%M:%S')] $*"
}

# Check if session exists
check_session() {
    tmux has-session -t "$SESSION" 2>/dev/null
}

# Get list of window names (excluding control)
get_agent_windows() {
    tmux list-windows -t "$SESSION" -F '#{window_name}' 2>/dev/null | grep -v "^${CONTROL_PANE}$" || true
}

# Capture last N lines from a window's pane
capture_pane() {
    local window="$1"
    local lines="${2:-20}"
    tmux capture-pane -t "$SESSION:$window" -p -S "-${lines}" 2>/dev/null || echo ""
}

# Check if claude process is running in a pane
is_claude_running() {
    local window="$1"
    local pane_pid
    pane_pid=$(tmux list-panes -t "$SESSION:$window" -F '#{pane_pid}' 2>/dev/null || echo "")
    if [ -z "$pane_pid" ]; then
        return 1
    fi
    # Check if any child process is claude or cursor
    if pgrep -P "$pane_pid" -f "claude" >/dev/null 2>&1; then
        return 0
    fi
    if pgrep -P "$pane_pid" -f "cursor" >/dev/null 2>&1; then
        return 0
    fi
    return 1
}

# ── HEARTBEAT: Update agent health registry ──
update_heartbeats() {
    local windows="$1"
    if [ -z "$windows" ]; then
        return
    fi

    mkdir -p "$AGENTS_DIR"

    for w in $windows; do
        local agent_file="$AGENTS_DIR/${w}.json"
        if [ ! -f "$agent_file" ]; then
            # Agent not registered — skip heartbeat
            continue
        fi

        if is_claude_running "$w"; then
            # Agent is alive — record heartbeat
            if [ "$DRY_RUN" = false ]; then
                python3 "$AGENTS_SCRIPT" heartbeat "$w" >/dev/null 2>&1 || true
            fi
        else
            # Agent not running — record missed heartbeat
            log "  [$w] Missed heartbeat (claude not running)"
            if [ "$DRY_RUN" = false ]; then
                python3 "$AGENTS_SCRIPT" miss-heartbeat "$w" >/dev/null 2>&1 || true
            fi
        fi
    done
}

# ── HEALTH CHECK: Detect stuck/dead agents ──
check_agent_health() {
    if [ "$DRY_RUN" = true ]; then
        return
    fi

    local health_json
    health_json=$(python3 "$AGENTS_SCRIPT" check-health 2>/dev/null || echo "{}")

    local stuck_count
    stuck_count=$(echo "$health_json" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d.get('stuck',0))" 2>/dev/null || echo "0")
    local dead_count
    dead_count=$(echo "$health_json" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d.get('dead',0))" 2>/dev/null || echo "0")

    if [ "$stuck_count" -gt 0 ]; then
        log "  WARNING: $stuck_count agent(s) stuck (missing 3+ heartbeats)"
    fi
    if [ "$dead_count" -gt 0 ]; then
        log "  WARNING: $dead_count agent(s) dead"
    fi
}

# Check if pane output has changed since last check.
# Returns 0 (true) if output is STALE (unchanged), 1 if fresh (changed).
is_pane_stale() {
    local window="$1"
    local current_screen
    current_screen=$(capture_pane "$window" 20)
    local snapshot_file="$SNAPSHOTS_DIR/${window}.snapshot"

    if [ ! -f "$snapshot_file" ]; then
        # First check — save snapshot, treat as fresh (not stale)
        echo "$current_screen" > "$snapshot_file"
        return 1
    fi

    local prev_screen
    prev_screen=$(cat "$snapshot_file")

    # Save current snapshot for next cycle
    echo "$current_screen" > "$snapshot_file"

    if [ "$current_screen" = "$prev_screen" ]; then
        return 0  # stale — nothing changed
    fi
    return 1  # fresh — output changed, agent is working
}

# Detect idle state and take action
nudge_window() {
    local window="$1"
    local screen
    screen=$(capture_pane "$window" 20)

    if [ -z "$screen" ]; then
        return
    fi

    # Check for permission prompts — auto-approve with 'y'
    if echo "$screen" | grep -qE "(Do you want to proceed|Yes, and don't ask again|Allow once|Allow for this session)"; then
        # Safety: block destructive commands outside project dir
        if echo "$screen" | grep -qE '(rm |del |unlink )' && ! echo "$screen" | grep -q "$PROJECT_DIR"; then
            log "  [$window] BLOCKED: Destructive command outside project dir"
            return
        fi
        log "  [$window] Permission prompt detected — sending 'y'"
        if [ "$DRY_RUN" = false ]; then
            tmux send-keys -t "$SESSION:$window" "y" Enter
        fi
        return
    fi

    # Check for Claude asking a question with numbered options — pick first
    if echo "$screen" | grep -qE "^[[:space:]]*1[\.\)][[:space:]]"; then
        # Only if the last non-empty line looks like it's waiting for input
        local last_line
        last_line=$(echo "$screen" | grep -v '^$' | tail -1)
        if echo "$last_line" | grep -qE "(choice|option|select|pick|which|>)"; then
            log "  [$window] Question with options detected — sending '1'"
            if [ "$DRY_RUN" = false ]; then
                tmux send-keys -t "$SESSION:$window" "1" Enter
            fi
            return
        fi
    fi

    # Check for idle prompt (just '>' waiting for input)
    local last_line
    last_line=$(echo "$screen" | grep -v '^$' | tail -1)
    if echo "$last_line" | grep -qE '^[[:space:]]*>[[:space:]]*$'; then
        log "  [$window] Idle prompt detected — sending 'continue'"
        if [ "$DRY_RUN" = false ]; then
            tmux send-keys -t "$SESSION:$window" "continue" Enter
        fi
        return
    fi

    # Check for TASK_COMPLETE signal
    if echo "$screen" | grep -qE "TASK_COMPLETE"; then
        log "  [$window] Task complete signal detected"
        return
    fi
}

# ── MESSAGE ROUTING: Send pending outbox messages with priority and targeting ──
send_pending_messages() {
    if [ ! -d "$OUTBOX_PENDING" ]; then
        return
    fi

    local pending_files
    pending_files=$(ls -1 "$OUTBOX_PENDING"/*.json 2>/dev/null || true)
    if [ -z "$pending_files" ]; then
        return
    fi

    # Sort by priority (extract priority from JSON, sort ascending = higher priority first)
    # Priority 1 = highest, default = 5
    local sorted_files
    sorted_files=$(for f in $pending_files; do
        local pri
        pri=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('priority',5))" "$f" 2>/dev/null || echo "5")
        echo "$pri $f"
    done | sort -n | awk '{print $2}')

    # Process each message
    for msg_file in $sorted_files; do
        if [ ! -f "$msg_file" ]; then
            continue
        fi

        # Check message expiry
        local queued_at
        queued_at=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('queued_at',0))" "$msg_file" 2>/dev/null || echo "0")
        local now_ts
        now_ts=$(python3 -c "import time; print(time.time())" 2>/dev/null || echo "0")
        local age
        age=$(python3 -c "print(int(float(sys.argv[1]) - float(sys.argv[2])))" "$now_ts" "$queued_at" 2>/dev/null || echo "0")

        if [ "$age" -gt "$MESSAGE_TTL" ] && [ "$queued_at" != "0" ]; then
            log "  Message expired (age=${age}s > TTL=${MESSAGE_TTL}s): $(basename "$msg_file")"
            if [ "$DRY_RUN" = false ]; then
                mkdir -p "$OUTBOX_EXPIRED"
                mv "$msg_file" "$OUTBOX_EXPIRED/"
            fi
            continue
        fi

        local msg_content
        msg_content=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('content',''))" "$msg_file" 2>/dev/null || echo "")

        # Check for target_agent field (new) first, then target_window (legacy), then target
        local target_agent
        target_agent=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('target_agent',''))" "$msg_file" 2>/dev/null || echo "")
        local target_window
        target_window=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('target_window',''))" "$msg_file" 2>/dev/null || echo "")
        local target
        target=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('target',''))" "$msg_file" 2>/dev/null || echo "")

        # Resolve the target window name
        local resolved_window=""
        if [ -n "$target_agent" ]; then
            # target_agent is the agent name — use it directly as the tmux window name
            resolved_window="$target_agent"
        elif [ -n "$target_window" ]; then
            resolved_window="$target_window"
        elif [ -n "$target" ]; then
            # Legacy format: might be "session:window.pane" — extract window
            resolved_window=$(echo "$target" | sed 's/.*://; s/\..*//')
        fi

        if [ -z "$msg_content" ]; then
            log "  Skipping empty message: $(basename "$msg_file")"
            if [ "$DRY_RUN" = false ]; then
                mkdir -p "$OUTBOX_EXPIRED"
                mv "$msg_file" "$OUTBOX_EXPIRED/"
            fi
            continue
        fi

        # Determine delivery target
        local delivery_target=""
        if [ -n "$resolved_window" ]; then
            # Check if the target window exists
            if tmux list-windows -t "$SESSION" -F '#{window_name}' 2>/dev/null | grep -q "^${resolved_window}$"; then
                delivery_target="$SESSION:$resolved_window"
            else
                log "  Target window '$resolved_window' not found — routing to control"
                delivery_target="$SESSION:$CONTROL_PANE"
            fi
        else
            delivery_target="$SESSION:$CONTROL_PANE"
        fi

        log "  Sending message to $delivery_target (priority=$(python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(d.get('priority',5))" "$msg_file" 2>/dev/null || echo "?"))"
        if [ "$DRY_RUN" = false ]; then
            local tmp_msg="/tmp/claw_nudge_msg_$$.txt"
            echo "$msg_content" > "$tmp_msg"
            tmux load-buffer "$tmp_msg"
            tmux paste-buffer -t "$delivery_target"
            sleep 0.5
            tmux send-keys -t "$delivery_target" Enter
            rm -f "$tmp_msg"

            # Move to sent with acknowledgment timestamp
            mkdir -p "$OUTBOX_SENT"
            # Add delivered_at to the message before archiving
            python3 -c "
import json, sys, time
f = sys.argv[1]
d = json.load(open(f))
d['delivered_at'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
d['delivered_to'] = sys.argv[2]
json.dump(d, open(f, 'w'), indent=2)
" "$msg_file" "$delivery_target" 2>/dev/null || true
            mv "$msg_file" "$OUTBOX_SENT/"
        fi
    done
}

# Check if all agents are done (all show TASK_COMPLETE and no pending messages)
all_done() {
    local windows
    windows=$(get_agent_windows)
    if [ -z "$windows" ]; then
        return 0
    fi

    for w in $windows; do
        local screen
        screen=$(capture_pane "$w" 30)
        if ! echo "$screen" | grep -qE "TASK_COMPLETE"; then
            return 1
        fi
    done

    # Check for pending messages
    local pending_count
    pending_count=$(ls -1 "$OUTBOX_PENDING"/*.json 2>/dev/null | wc -l | tr -d ' ')
    if [ "$pending_count" -gt 0 ]; then
        return 1
    fi

    return 0
}

# ── Main loop ──

log "Auto-nudge monitor started (interval=${INTERVAL}s, dry_run=$DRY_RUN)"
log "Session: $SESSION"
log "Agent health registry: $AGENTS_DIR"

# Ensure directories exist
mkdir -p "$AGENTS_DIR" "$OUTBOX_PENDING" "$OUTBOX_SENT" "$OUTBOX_EXPIRED"

while true; do
    if ! check_session; then
        log "Session '$SESSION' not found. Waiting..."
        sleep "$INTERVAL"
        continue
    fi

    log "=== Check cycle ==="

    WINDOWS=$(get_agent_windows)

    # Only monitor the control (orchestrator) pane — it handles agents itself
    if is_claude_running "$CONTROL_PANE"; then
        if is_pane_stale "$CONTROL_PANE"; then
            log "  [orchestrator] Idle (no output change) — nudging..."
            nudge_window "$CONTROL_PANE"
        else
            log "  [orchestrator] Actively working"
        fi
    else
        log "  [orchestrator] Claude NOT running"
    fi

        # Update heartbeats for all agent windows (still track health)
        if [ -n "$WINDOWS" ]; then
            update_heartbeats "$WINDOWS"
        fi

    # Check agent health (detect stuck/dead)
    check_agent_health

    # Send any pending outbox messages (sorted by priority)
    send_pending_messages

    # Check if all done
    if all_done; then
        log "All agents complete and no pending messages. Exiting."
        exit 0
    fi

    sleep "$INTERVAL"
done
