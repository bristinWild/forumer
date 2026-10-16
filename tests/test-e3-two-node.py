#!/usr/bin/env python3
"""E3 integration: two isolated Forumer nodes, signed topic/reply, restart sync and A5 logs.

Run from the repo (after `nix build .#ui-dev -o result-e3-ui`):
    python3 tests/test-e3-two-node.py

Only synthetic accounts are used. Test profiles are deleted on exit.
No recovery phrase, password, full post ID, or persona key is printed.
"""

import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import socket
import sqlite3
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
LAUNCHER = ROOT / "result-e3-ui/bin/run-logos-standalone-ui"


def wait_for(description, check, seconds=120):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            result = check()
            if result:
                return result
        except (OSError, ValueError, sqlite3.Error, RuntimeError):
            pass
        time.sleep(2)
    raise RuntimeError(f"{description} timed out (no identifying details logged)")


class Node:
    def __init__(self, label, port, base):
        self.label = label
        self.port = port
        self.profile = base / label.lower()
        self.profile.mkdir(mode=0o700)
        self.process = None

    def start(self):
        # The ui-host subprocess can drop FORUMER_LOG_FILE; this marker enables
        # logging to the local test profile through the backend fallback.
        backend_dir = self.profile / "module_data/forumer"
        backend_dir.mkdir(parents=True, exist_ok=True)
        (backend_dir / "debug-log").touch()
        env = os.environ.copy()
        env.update(
            LOGOS_USER_DIR=str(self.profile),
            QML_INSPECTOR_PORT=str(self.port),
            QT_QPA_PLATFORM="offscreen",
            LOGOS_QML_HOT_RELOAD="0",
            FORUMER_NETWORK="logos.dev",
            FORUMER_LOG_FILE=str(backend_dir / "backend.log"),
        )
        with (self.profile / "host.log").open("ab") as log:
            self.process = subprocess.Popen(
                [str(LAUNCHER), "--user-dir", str(self.profile)],
                stdout=log,
                stderr=subprocess.STDOUT,
                env=env,
                start_new_session=True,
            )

    def eval(self, expression):
        if self.process is None or self.process.poll() is not None:
            raise RuntimeError(f"Node {self.label} not running")
        request = json.dumps({"id": 1, "command": "evaluate", "params": {"expression": expression}}).encode() + b"\n"
        with socket.create_connection(("127.0.0.1", self.port), timeout=5) as sock:
            sock.settimeout(10)
            sock.sendall(request)
            chunks = bytearray()
            while b"\n" not in chunks:
                data = sock.recv(32768)
                if not data:
                    raise RuntimeError("inspector connection closed")
                chunks.extend(data)
        response = json.loads(chunks.split(b"\n", 1)[0])
        if response.get("error"):
            # Evaluator errors can contain user-supplied strings: do not include
            # raw response in CI logs.
            raise RuntimeError("inspector evaluation error")
        return response.get("result")

    def stop(self):
        if self.process and self.process.poll() is None:
            os.killpg(self.process.pid, signal.SIGTERM)
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(self.process.pid, signal.SIGKILL)
                self.process.wait(timeout=10)
        self.process = None

    def find_post(self, *, kind, body=None, title=None, root=None):
        path = self.profile / "module_data/forumer/posts/posts.sqlite3"
        if not path.is_file():
            return None
        with sqlite3.connect(f"file:{path}?mode=ro", uri=True, timeout=2) as db:
            sql = "SELECT id, envelope FROM posts WHERE kind=? AND forum='public'"
            args = [kind]
            if root is not None:
                sql += " AND root=?"
                args.append(root)
            for post_id, encoded in db.execute(sql, args):
                post = json.loads(encoded)
                if title is not None and post.get("title") != title:
                    continue
                if body is not None and post.get("body") != body:
                    continue
                return post_id, post
        return None

    def topic(self, title):
        return self.find_post(kind=0, title=title)

    def reply(self, root, body):
        return self.find_post(kind=1, root=root, body=body)


def call_backend(node, expression):
    # The wrapper returns immediately and the Qt replica resolves the action
    # asynchronously. Subsequent on-disk assertions verify the actual result.
    node.eval(
        "(function(){store.call(" + expression + ","
        "function(){},function(e){store.lastError=String(e);});return true;})()"
    )


def confirm_same_post(left, right, description):
    left_id, left_envelope = left
    right_id, right_envelope = right
    if left_id != right_id or left_envelope != right_envelope:
        raise RuntimeError(f"{description}: signed envelope mismatch")


def audit_logs(nodes, posts):
    identifiers = []
    for post_id, envelope in posts:
        identifiers.append(post_id)
        pubkey = envelope.get("pk")
        if isinstance(pubkey, str) and len(pubkey) >= 20:
            identifiers.append(pubkey)
    for node in nodes:
        log = node.profile / "module_data/forumer/backend.log"
        if not log.exists() or log.stat().st_size == 0:
            raise RuntimeError(f"Node {node.label}: missing/empty backend log")
        checked = subprocess.run(
            ["bash", str(ROOT / "scripts/check-logs.sh"), str(log)],
            capture_output=True,
            text=True,
            check=False,
        )
        if checked.returncode != 0:
            raise RuntimeError(f"Node {node.label}: A5 log audit failed")
        contents = log.read_text(errors="replace")
        if any(value in contents for value in identifiers):
            raise RuntimeError(f"Node {node.label}: full signed identifier leaked into log")
        print(f"PASS: Node {node.label} backend log identifier audit")


def run():
    if not LAUNCHER.is_file():
        raise RuntimeError("Build launcher first: nix build .#ui-dev -o result-e3-ui")

    base = Path(tempfile.mkdtemp(prefix="forumer-e3-"))
    os.chmod(base, 0o700)
    a = Node("A", 47981, base)
    b = Node("B", 47982, base)
    nodes = [a, b]

    try:
        for node in nodes:
            node.start()
        for node in nodes:
            wait_for(f"Node {node.label} backend ready", lambda n=node: n.eval("store.nodeReady"), 120)
            print(f"PASS: Node {node.label} backend ready")

        for node in nodes:
            password = "e3-test-only-" + secrets.token_hex(12)
            label = f"E3-{node.label}"
            node.eval(
                "(function(){store.callJson(store.backend.createIdentity("
                f"{json.dumps(label)},{json.dumps(password)}),"
                'function(r){store.pendingPhrase="";},'
                "function(e){store.lastError=String(e);});return true;})()"
            )
            wait_for(f"Node {node.label} synthetic identity", lambda n=node: n.eval("store.unlocked"), 90)
            print(f"PASS: Node {node.label} synthetic account unlocked")

        marker = secrets.token_hex(7)
        title = f"e3-topic-{marker}"
        body = f"E3 signed topic {marker}"
        call_backend(
            a,
            f"store.backend.createTopic({json.dumps(title)},{json.dumps(body)},'general','persona')",
        )
        original = wait_for("Node A topic stored", lambda: a.topic(title), 120)
        print("PASS: Node A signed topic stored")
        mirrored = wait_for("Node B received signed topic", lambda: b.topic(title), 240)
        confirm_same_post(original, mirrored, "A-to-B topic")
        print("PASS: Node B received identical signed topic")

        # Opposite direction, using the actual topic ID obtained from SQLite.
        topic_id = original[0]
        reply_body = f"E3 signed reply {marker}"
        call_backend(
            b,
            f"store.backend.replyToTopic({json.dumps(topic_id)},{json.dumps(reply_body)},'persona')",
        )
        b_reply = wait_for("Node B reply stored", lambda: b.reply(topic_id, reply_body), 120)
        a_reply = wait_for("Node A received signed reply", lambda: a.reply(topic_id, reply_body), 240)
        confirm_same_post(b_reply, a_reply, "B-to-A reply")
        print("PASS: Node A received identical signed reply from Node B")

        # Node B must be offline while A publishes the second topic.
        b.stop()
        print("PASS: Node B stopped for offline catch-up")
        offline_title = f"e3-offline-{marker}"
        offline_body = f"E3 topic while peer offline {marker}"
        call_backend(
            a,
            f"store.backend.createTopic({json.dumps(offline_title)},{json.dumps(offline_body)},'general','persona')",
        )
        offline_origin = wait_for("Node A offline-window post stored", lambda: a.topic(offline_title), 120)
        if b.topic(offline_title) is not None:
            raise RuntimeError("Node B unexpectedly received post while stopped")
        print("PASS: Node A stored a topic while Node B was offline")

        b.start()
        wait_for("Node B restart", lambda: b.eval("store.nodeReady"), 150)
        if b.topic(title) is None or b.reply(topic_id, reply_body) is None:
            raise RuntimeError("Node B lost persisted topic or reply after restart")
        print("PASS: Node B restarted with persisted signed posts")

        automatic = True
        try:
            offline_peer = wait_for("Node B automatic catch-up", lambda: b.topic(offline_title), 110)
        except RuntimeError:
            automatic = False
            # Verify catch-up explicitly if the deployment's automatic
            # timer/discovery has not completed in this bounded window.
            call_backend(b, "store.backend.catchUp()")
            offline_peer = wait_for("Node B explicit catch-up", lambda: b.topic(offline_title), 240)
        confirm_same_post(offline_origin, offline_peer, "offline catch-up")
        print("PASS: Node B synchronized post created while offline"
              + (" (automatic)" if automatic else " (explicit catchUp)"))

        audit_logs(nodes, [original, b_reply, offline_origin])
        print("PASS: E3 two-node topic, reply, restart catch-up, initial A5 audit")

    finally:
        for node in nodes:
            node.stop()
        # No credentials, backend logs, or post metadata are saved as artifacts.
        shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    run()
