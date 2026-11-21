#!/usr/bin/env python3
# A scripted ACP agent for tests/unit/test_acp_client.cpp: speaks just
# enough of the protocol to prove initialize, session creation, streaming,
# fs read/write through the client, and one permission round trip.
import json
import sys


def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def update(update_kind, **fields):
    body = {"sessionUpdate": update_kind}
    body.update(fields)
    send({"jsonrpc": "2.0", "method": "session/update",
          "params": {"sessionId": "fake-1", "update": body}})


state = {"prompt_id": None, "read_content": None}

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        msg = json.loads(line)
    except json.JSONDecodeError:
        continue
    method = msg.get("method")
    mid = msg.get("id")

    if method == "initialize":
        send({"jsonrpc": "2.0", "id": mid, "result": {
            "protocolVersion": 1,
            "agentCapabilities": {"loadSession": False,
                                  "promptCapabilities": {"embeddedContext": True}},
            "agentInfo": {"name": "fake", "version": "0"},
            "authMethods": []}})
    elif method == "session/new":
        send({"jsonrpc": "2.0", "id": mid, "result": {
            "sessionId": "fake-1",
            "modes": {"currentModeId": "ask", "availableModes": [
                {"id": "ask", "name": "Ask", "description": "ask first"},
                {"id": "yolo", "name": "Yolo", "description": "no asking"}]}}})
    elif method == "session/prompt":
        state["prompt_id"] = mid
        cwd = msg["params"].get("_cwd", "")
        update("agent_thought_chunk", content={"type": "text", "text": "thinking"})
        update("agent_message_chunk", content={"type": "text", "text": "Hello "})
        update("agent_message_chunk", content={"type": "text", "text": "world"})
        # Ask the editor for the file, like a real agent without its own read.
        import os
        send({"jsonrpc": "2.0", "id": "read-1", "method": "fs/read_text_file",
              "params": {"sessionId": "fake-1",
                         "path": os.path.join(os.getcwd(), "input.txt")}})
    elif method == "session/cancel":
        if state["prompt_id"] is not None:
            send({"jsonrpc": "2.0", "id": state["prompt_id"],
                  "result": {"stopReason": "cancelled"}})
            state["prompt_id"] = None
    elif mid == "read-1" and "result" in msg:
        state["read_content"] = msg["result"].get("content", "")
        send({"jsonrpc": "2.0", "id": "perm-1", "method": "session/request_permission",
              "params": {"sessionId": "fake-1",
                         "toolCall": {"toolCallId": "t1", "title": "Uppercase the file"},
                         "options": [
                             {"optionId": "allow", "name": "Allow", "kind": "allow_once"},
                             {"optionId": "reject", "name": "Reject", "kind": "reject_once"}]}})
    elif mid == "perm-1" and "result" in msg:
        outcome = msg["result"].get("outcome", {})
        if outcome.get("outcome") == "selected" and outcome.get("optionId") == "allow":
            import os
            send({"jsonrpc": "2.0", "id": "write-1", "method": "fs/write_text_file",
                  "params": {"sessionId": "fake-1",
                             "path": os.path.join(os.getcwd(), "written.txt"),
                             "content": (state["read_content"] or "").upper()}})
        else:
            send({"jsonrpc": "2.0", "id": state["prompt_id"],
                  "result": {"stopReason": "refusal"}})
            state["prompt_id"] = None
    elif mid == "write-1" and "result" in msg:
        import os
        update("tool_call", toolCallId="t1", title="Uppercase the file",
               kind="edit", status="completed",
               content=[{"type": "diff",
                         "path": os.path.join(os.getcwd(), "written.txt"),
                         "oldText": state["read_content"] or "",
                         "newText": (state["read_content"] or "").upper()}])
        send({"jsonrpc": "2.0", "id": state["prompt_id"],
              "result": {"stopReason": "end_turn"}})
        state["prompt_id"] = None
    elif mid is not None and method is None:
        pass  # stray responses are not expected
    elif method is not None and mid is not None:
        send({"jsonrpc": "2.0", "id": mid,
              "error": {"code": -32601, "message": "unknown: " + method}})
