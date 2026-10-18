#!/usr/bin/env python3
"""E3 private forum test: member sync and non-member isolation.

Requires tests/test-e3-two-node.py and `nix build .#ui-dev -o result-e3-ui`.
Uses short-lived synthetic identities. Never prints invite keys, post IDs or content.
"""
import base64
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import sqlite3
import tempfile
import time
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / 'tests/test-e3-two-node.py'
spec = importlib.util.spec_from_file_location('forumer_e3_baseline', BASELINE)
e3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e3)


def evaluate_callback(node, call_expression):
    """Use an already-declared QML var property for asynchronous callback results."""
    node.eval('store.linkWait=null;store.lastError="";true')
    node.eval(
        '(function(){store.callJson(' + call_expression + ','
        'function(r){store.linkWait=r;},'
        'function(e){store.lastError="E3 operation failed";});return true;})()'
    )
    e3.wait_for('private forum operation',
                lambda: node.eval('store.linkWait !== null || store.lastError.length > 0'), 90)
    if node.eval('store.lastError.length > 0'):
        raise RuntimeError('private forum operation failed (details intentionally hidden)')
    result = node.eval('JSON.stringify(store.linkWait)')
    if not isinstance(result, str):
        raise RuntimeError('private forum response unavailable')
    return json.loads(result)


def decode_invite(url):
    """Extract synthetic invite secret only in memory, never print/log it."""
    encoded = parse_qs(urlparse(url).query).get('p', [])
    if len(encoded) != 1:
        raise RuntimeError('unexpected invite link shape')
    token = encoded[0]
    document = json.loads(base64.urlsafe_b64decode(token + '=' * (-len(token) % 4)))
    key = document.get('invite')
    name = document.get('name')
    if not isinstance(key, str) or not key or not isinstance(name, str):
        raise RuntimeError('invalid synthetic invite')
    return key, name


def find_visible_topic(node, title, body, forum):
    # Qt inspector JSON conversion of JS objects is inconsistent. Returning a
    # JSON string makes the round-trip explicit, and never logs post content.
    expr = '''(function(){
      var ids=Object.keys(store.topics);
      for(var i=0;i<ids.length;i++){
        var t=store.topics[ids[i]];
        if(t && t.title===%s && t.body===%s && t.forum===%s)
          return JSON.stringify({id:ids[i], title:t.title, body:t.body, forum:t.forum});
      }
      return "";
    })()''' % (json.dumps(title), json.dumps(body), json.dumps(forum))
    value = node.eval(expr)
    if isinstance(value, dict):
        return value
    if isinstance(value, str) and value:
        try:
            parsed = json.loads(value)
        except json.JSONDecodeError:
            return None
        return parsed if isinstance(parsed, dict) else None
    return None


def private_db_has_any(node, forum):
    path = node.profile / 'module_data/forumer/posts/posts.sqlite3'
    if not path.is_file():
        return False
    with sqlite3.connect(f'file:{path}?mode=ro', uri=True, timeout=2) as db:
        return db.execute('SELECT 1 FROM posts WHERE forum=? LIMIT 1',
                          (forum,)).fetchone() is not None


def private_db_has(node, forum, post_id):
    path = node.profile / 'module_data/forumer/posts/posts.sqlite3'
    if not path.is_file():
        return False
    with sqlite3.connect(f'file:{path}?mode=ro', uri=True, timeout=2) as db:
        return db.execute('SELECT 1 FROM posts WHERE forum=? AND id=?',
                          (forum, post_id)).fetchone() is not None


def create_identity(node):
    password = 'e3-private-' + secrets.token_hex(12)
    node.eval(
        '(function(){store.callJson(store.backend.createIdentity('
        + json.dumps('E3-' + node.label) + ',' + json.dumps(password)
        + '),function(r){store.pendingPhrase="";},'
        + 'function(e){store.lastError="E3 identity failed";});return true;})()'
    )
    e3.wait_for('synthetic identity', lambda: node.eval('store.unlocked'), 90)


def audit_logs(nodes, post_id):
    for node in nodes:
        log = node.profile / 'module_data/forumer/backend.log'
        if not log.exists() or not log.stat().st_size:
            raise RuntimeError('A5: expected backend log is unavailable')
        result = os.spawnv(os.P_WAIT, '/bin/bash',
                           ['/bin/bash', str(ROOT/'scripts/check-logs.sh'), str(log)])
        if result != 0 or post_id in log.read_text(errors='replace'):
            raise RuntimeError('A5 backend identifier audit failed')


def main():
    if not e3.LAUNCHER.is_file():
        raise RuntimeError('Build first: nix build .#ui-dev -o result-e3-ui')
    base = Path(tempfile.mkdtemp(prefix='forumer-e3-private-'))
    os.chmod(base, 0o700)
    a = e3.Node('A', 47981, base)
    b = e3.Node('B', 47982, base)
    c = e3.Node('C', 47983, base)  # never receives the invite
    nodes = [a, b, c]
    try:
        for n in nodes:
            n.start()
        for n in nodes:
            e3.wait_for('backend ready', lambda node=n: node.eval('store.nodeReady'), 120)
            create_identity(n)
        print('PASS: three independent synthetic identities ready')

        created = evaluate_callback(a, 'store.backend.createPrivateForum("E3 synthetic private forum")')
        forum = created.get('id')
        invite = created.get('invite')
        if not isinstance(forum, str) or not forum or not isinstance(invite, str):
            raise RuntimeError('private forum creation did not return expected response')
        key, name = decode_invite(invite)
        joined = evaluate_callback(b,
            'store.backend.joinPrivateForum(' + json.dumps(key) + ',' + json.dumps(name) + ')')
        if joined.get('id') != forum:
            raise RuntimeError('private forum membership id mismatch')
        del key, invite, joined, created
        print('PASS: Node B joined synthetic private forum')

        marker = secrets.token_hex(7)
        title = 'e3-private-' + marker
        body = 'Synthetic private discussion ' + marker
        e3.call_backend(a, 'store.backend.createTopicIn('
                        + json.dumps(forum) + ',' + json.dumps(title) + ','
                        + json.dumps(body) + ',"general","persona")')
        # A private post is intentionally stored sealed; prove durable local
        # creation first instead of assuming the QML store mirrors it.
        e3.wait_for('private post stored on sender',
                    lambda: private_db_has_any(a, forum), 120)
        print('PASS: Node A stored sealed private post')
        origin = e3.wait_for('private topic on sender',
            lambda: find_visible_topic(a, title, body, forum), 120)
        peer = e3.wait_for('private topic on recipient',
            lambda: find_visible_topic(b, title, body, forum), 240)
        if origin.get('id') != peer.get('id') or not private_db_has(b, forum, origin['id']):
            raise RuntimeError('private signed post mismatch')
        print('PASS: member B received identical private post id and plaintext')

        # A non-member must not see the post in its database or UI.
        if private_db_has(c, forum, origin['id']):
            raise RuntimeError('non-member stored private forum post')
        if find_visible_topic(c, title, body, forum):
            raise RuntimeError('non-member can see private post')
        print('PASS: non-member C cannot access private post')

        audit_logs(nodes, origin['id'])
        print('PASS: private-forum A5 identifier checks')
        print('PASS: E3 private-forum sync and non-member isolation')
    finally:
        for n in nodes:
            n.stop()
        shutil.rmtree(base, ignore_errors=True)


if __name__ == '__main__':
    main()
