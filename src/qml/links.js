.pragma library

// Shareable links to a thread or a domain.
//
//   basecamp://intent/forumer.open?p=<base64url JSON>
//
// Basecamp (0.3.1+) opens these from a browser or a chat app and hands the
// JSON to Forumer as the "forumer.open" intent (metadata.json "provides").
// Pasted into Forumer's search box they work on any Basecamp version.
//
// What a link carries, and nothing else:
//   thread:  {"topic": "<32-hex post id>", "day": <days since 1970, UTC>}
//   domain:  {"domain": "<domain>"}
// The post id is a hash of the post, so the link says nothing about who
// shared it. "day" lets a reader who doesn't hold the thread yet ask peers
// for that whole stretch of days, the ordinary history request, instead of
// asking for this one post: nobody learns which thread was opened.

var kPrefix = "basecamp://intent/forumer.open?p=";
var kDayMs = 86400000;
var kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// base64url without padding, of an ASCII string (all we ever encode).
function b64urlEncode(s) {
    var out = "";
    for (var i = 0; i < s.length; i += 3) {
        var a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2);
        var n = (a << 16) | ((b || 0) << 8) | (c || 0);
        out += kAlphabet[(n >> 18) & 63] + kAlphabet[(n >> 12) & 63];
        if (i + 1 < s.length) out += kAlphabet[(n >> 6) & 63];
        if (i + 2 < s.length) out += kAlphabet[n & 63];
    }
    return out;
}

// Accepts base64url or standard base64, with or without padding. Returns
// null on any character outside the alphabet.
function b64urlDecode(s) {
    s = s.replace(/=+$/, "").replace(/\+/g, "-").replace(/\//g, "_");
    var out = "", bits = 0, value = 0;
    for (var i = 0; i < s.length; ++i) {
        var v = kAlphabet.indexOf(s[i]);
        if (v < 0) return null;
        value = (value << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += String.fromCharCode((value >> bits) & 255);
        }
    }
    return out;
}

function isTopicId(s) { return typeof s === "string" && /^[0-9a-f]{32}$/.test(s); }

// The same rule as forumer_core's normalizeDomains for one tag: lower-case
// ASCII letters and digits, runs of space/-/_ become one "-", the rest is
// dropped; 2..32 characters, else "".
function normalizeDomain(raw) {
    if (typeof raw !== "string") return "";
    var clean = "", pendingDash = false;
    for (var i = 0; i < raw.length; ++i) {
        var ch = raw[i];
        if (ch === " " || ch === "\t" || ch === "\n" || ch === "-" || ch === "_") {
            pendingDash = clean.length > 0;
        } else if (/[A-Za-z0-9]/.test(ch)) {
            if (pendingDash) clean += "-";
            pendingDash = false;
            clean += ch.toLowerCase();
        }
    }
    return clean.length >= 2 && clean.length <= 32 ? clean : "";
}

function dayOf(tsMs) { return Math.floor(Number(tsMs) / kDayMs); }

function topicLink(id, tsMs) {
    return kPrefix + b64urlEncode(JSON.stringify({ topic: id, day: dayOf(tsMs) }));
}

function domainLink(domain) {
    return kPrefix + b64urlEncode(JSON.stringify({ domain: domain }));
}

// Checks an intent payload (or a decoded link). Returns
// { kind: "topic", topic, day } | { kind: "domain", domain } | null.
// Everything in it is untrusted: anyone can write a link.
function fromParams(p) {
    if (!p || typeof p !== "object") return null;
    if (isTopicId(p.topic)) {
        var day = Number(p.day);
        var ok = isFinite(day) && day > 0 && day < 1000000 && Math.floor(day) === day;
        return { kind: "topic", topic: p.topic, day: ok ? day : 0 };
    }
    var d = normalizeDomain(p.domain);
    if (d.length > 0) return { kind: "domain", domain: d };
    return null;
}

// Recognises what someone pasted: a Forumer link, or a bare topic id.
// Returns the same shape as fromParams, or null when it is ordinary search
// text.
function parse(text) {
    if (typeof text !== "string") return null;
    var t = text.trim();
    if (t.length > 2048) return null;
    if (isTopicId(t)) return { kind: "topic", topic: t, day: 0 };
    var at = t.indexOf("forumer.open?");
    if (t.indexOf("basecamp://intent/") !== 0 || at < 0) return null;
    var query = t.substring(at + "forumer.open?".length).split("#")[0];
    var parts = query.split("&");
    for (var i = 0; i < parts.length; ++i) {
        if (parts[i].indexOf("p=") !== 0) continue;
        var raw = parts[i].substring(2);
        try { raw = decodeURIComponent(raw); } catch (e) { return null; }
        var json = b64urlDecode(raw);
        if (json === null) return null;
        try { return fromParams(JSON.parse(json)); } catch (e) { return null; }
    }
    return null;
}