#!/usr/bin/env node
// Tests for src/qml/links.js: shared links to threads, domains and private
// forum invites. Needs only Node:
//
//     node scripts/test-links.mjs
//
// Links are written by anyone, so everything parse() returns must be one of
// the known shapes with checked fields - or null.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const source = fs
    .readFileSync(path.join(here, "../src/qml/links.js"), "utf8")
    .replace(/^\.pragma library\s*$/m, "");
const L = new Function(source + "; return { parse, fromParams, topicLink, domainLink, b64urlEncode };")();

let failures = 0;
function check(name, ok, detail) {
    if (ok) return;
    ++failures;
    console.error("FAIL " + name + (detail ? "\n     " + detail : ""));
}
const show = (v) => JSON.stringify(v);

// What the backend's inviteLink() produces: ASCII JSON (non-ASCII as \uXXXX)
// in base64url without padding.
function backendInvite(key, name) {
    const json = '{"invite":' + JSON.stringify(key) + ',"name":' +
        JSON.stringify(name).replace(/[\u007f-￿]/g, (c) => "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0")) + "}";
    return "basecamp://intent/forumer.open?p=" + Buffer.from(json, "ascii").toString("base64url");
}

const key = "q3Xv0b5Jx9l2mQwErTyUiOpAsDfGhJkLzXcVbNm1234";   // 43 chars
check("key fixture is 43 chars", key.length === 43);

// ── Invites ──────────────────────────────────────────────────────────────────
{
    const r = L.parse(backendInvite(key, "Campus council"));
    check("invite parses", r && r.kind === "invite" && r.key === key && r.name === "Campus council", show(r));
}
{
    const name = "Café 🎉 सभा";
    const r = L.parse(backendInvite(key, name));
    check("non-ASCII name survives", r && r.name === name, show(r));
}
{
    const r = L.parse(backendInvite(key, "a\nb\u0007c"));
    check("control characters are cleaned", r && r.name === "a b c", show(r));
}
{
    const r = L.parse(backendInvite(key, "   "));
    check("empty name gets a default", r && r.name === "Private forum", show(r));
}
{
    const r = L.parse(backendInvite(key, "x".repeat(200)));
    check("long name is cut", r && r.name.length === 64, show(r && r.name.length));
}
check("short key is not an invite", L.fromParams({ invite: key.slice(1), name: "x" }) === null);
check("key with other characters is not an invite",
      L.fromParams({ invite: key.slice(1) + "+", name: "x" }) === null);
check("non-string key is not an invite", L.fromParams({ invite: 42 }) === null);
check("name of another type is ignored",
      (L.fromParams({ invite: key, name: { evil: 1 } }) || {}).name === "Private forum");

// ── Threads and domains still work ───────────────────────────────────────────
{
    const id = "0123456789abcdef0123456789abcdef";
    const r = L.parse(L.topicLink(id, 86400000 * 20000 + 5));
    check("topic link", r && r.kind === "topic" && r.topic === id && r.day === 20000, show(r));
    const d = L.parse(L.domainLink("privacy"));
    check("domain link", d && d.kind === "domain" && d.domain === "privacy", show(d));
    check("bare id", (L.parse(id) || {}).kind === "topic");
}
check("ordinary text is not a link", L.parse("hello world") === null);
check("garbage payload", L.parse("basecamp://intent/forumer.open?p=%%%") === null);
check("not JSON", L.parse("basecamp://intent/forumer.open?p=" + L.b64urlEncode("nope")) === null);

if (failures > 0) {
    console.error(failures + " failure(s)");
    process.exit(1);
}
console.log("links.js: all checks passed");
