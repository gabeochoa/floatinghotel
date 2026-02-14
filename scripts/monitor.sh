#!/bin/bash
# Claw Town overnight monitor - checks agent status and approves safe commands
# Run periodically from the orchestrator

SESSION="claw-town-floatinghotel"
PROJECT_DIR="/Users/gabeochoa/p/floatinghotel"

echo "=== Monitor check at $(date) ==="

# List all windows
echo "Windows:"
tmux list-windows -t "$SESSION" -F '  #{window_index} #{window_name}' 2>/dev/null

echo ""

# For each agent window, check if Claude is running and if there's a pending prompt
for WINDOW in requirements fix-infra e2e-research fix-spawn; do
    PANE_PID=$(tmux list-panes -t "$SESSION:$WINDOW" -F '#{pane_pid}' 2>/dev/null || echo "")
    if [ -z "$PANE_PID" ]; then
        continue
    fi

    CLAUDE_RUNNING=$(ps -eo ppid,comm 2>/dev/null | grep "^ *${PANE_PID}.*claude" | wc -l | tr -d ' ')

    # Capture last 10 lines to check for prompts
    SCREEN=$(tmux capture-pane -t "$SESSION:$WINDOW" -p -S -10 2>/dev/null)

    # Check for permission prompts
    NEEDS_APPROVAL=0
    if echo "$SCREEN" | grep -q "Do you want to proceed"; then
        NEEDS_APPROVAL=1
    fi
    if echo "$SCREEN" | grep -q "Yes, and don't ask again"; then
        NEEDS_APPROVAL=1
    fi

    echo "Agent: $WINDOW | Claude running: $CLAUDE_RUNNING | Needs approval: $NEEDS_APPROVAL"

    if [ "$NEEDS_APPROVAL" -eq 1 ]; then
        # Check if the command involves deleting files outside the project
        if echo "$SCREEN" | grep -qE '(rm |del |unlink )' && ! echo "$SCREEN" | grep -q "$PROJECT_DIR"; then
            echo "  >> BLOCKED: Delete command outside project dir detected!"
        else
            echo "  >> Auto-approving command"
        fi
    fi
done

echo ""
echo "=== End monitor check ==="
