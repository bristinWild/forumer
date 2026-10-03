import QtQuick

// The view's single source of truth: mirrors the backend's properties, keeps
// every post the backend has shown us, and derives what each screen lists
// (feeds, a thread's reply tree, the outbox, missed posts). Screens read from
// here and call its actions; none of them talk to the backend directly.
//
// Posts are kept in plain JS maps and `rev` is bumped on every change; the
// derived lists are bindings on `rev`, so a new post re-renders whatever shows
// it. Fine for the hundreds of posts a seminar forum holds.
Item {
    id: store
    visible: false

    // The typed replica of the C++ backend (logos.module("forumer")).
    property var backend: null
    readonly property bool hasBackend: backend !== null && backend !== undefined

    function log(msg) { console.log("[forumer qml] " + msg); }

    // ── Backend state (PROPs from forumer.rep) ────────────────────────────────
    readonly property string status:            hasBackend ? backend.status            : ""
    readonly property bool   nodeReady:          hasBackend ? backend.nodeReady         : false
    readonly property string topic:              hasBackend ? backend.topic             : ""
    readonly property string appVersion:         hasBackend ? backend.appVersion        : ""
    readonly property string identityState:      hasBackend ? backend.identityState     : "none"
    readonly property string accountsJson:       hasBackend ? backend.accountsJson      : "[]"
    readonly property string selectedAccountId:  hasBackend ? backend.selectedAccountId : ""
    readonly property string myLabel:            hasBackend ? backend.myLabel           : ""
    readonly property string myPersona:          hasBackend ? backend.myPersona         : ""
    readonly property string rotationPolicy:     hasBackend ? backend.rotationPolicy    : "manual"
    readonly property string defaultDisclosure:  hasBackend ? backend.defaultDisclosure : "persona"
    readonly property string alias:              hasBackend ? backend.alias             : ""
    readonly property int    unsentCount:        hasBackend ? backend.unsentCount       : 0
    readonly property string syncInfo:           hasBackend ? backend.syncInfo          : ""

    readonly property bool unlocked: identityState === "unlocked"
    readonly property bool canPost: nodeReady && unlocked
    readonly property bool connected: status === "Connected" || status === "PartiallyConnected"

    // Nested replies need the backend's replyToPost (next step); until then
    // every reply answers the topic itself.
    readonly property bool canNestReplies: hasBackend && typeof backend.replyToPost === "function"

    readonly property var accounts: {
        try { return JSON.parse(store.accountsJson); } catch (e) { return []; }
    }

    // ── Navigation & UI state ─────────────────────────────────────────────────
    // "home" | "thread" | "missed" | "mine" | "chats" | "profile" | "settings" | "backup"
    property string screen: "home"
    property string selectedTopicId: ""
    property string replyTargetId: ""        // "" = reply to the topic itself
    property string searchText: ""
    property string domainFilter: ""         // "" = all domains
    property var followed: []                // followed domain names (saved per account next step)
    property string lastError: ""
    property string pendingPhrase: ""        // shown full-screen until confirmed
    readonly property real sessionStartMs: Date.now()

    function open(name) {
        store.screen = name;
        store.lastError = "";
    }

    function openTopic(id) {
        store.selectedTopicId = id;
        store.replyTargetId = "";
        store.open("thread");
    }

    // ── Posts ─────────────────────────────────────────────────────────────────
    property int rev: 0
    property var topics: ({})     // id -> { id, title, body, author, domains[], tsMs, delivery, placeholder, live, arrivedMs }
    property var replies: ({})    // id -> { id, topicId, parentId, body, author, tsMs, delivery, live, arrivedMs }
    property var deliveries: ({}) // id -> state, for posts whose state arrives before the post

    function nsToMs(ts) { return Math.floor(Number(ts) / 1000000); }

    function addTopic(id, title, body, author, domains, ts, live) {
        var existing = store.topics[id];
        if (existing && !existing.placeholder) return;
        store.topics[id] = {
            id: id, title: title, body: body, author: author || "",
            domains: (domains || "").split(",").filter(function (d) { return d.length > 0; }),
            tsMs: store.nsToMs(ts),
            delivery: store.deliveries[id] || "",
            placeholder: false,
            live: live === true,
            arrivedMs: Date.now()
        };
        store.rev++;
    }

    function addReply(id, topicId, parentId, body, author, ts, live) {
        if (store.replies[id]) return;
        store.replies[id] = {
            id: id, topicId: topicId, parentId: parentId || topicId, body: body, author: author || "",
            tsMs: store.nsToMs(ts),
            delivery: store.deliveries[id] || "",
            live: live === true,
            arrivedMs: Date.now()
        };
        // A reply can outrun its topic: stand in a placeholder until it arrives.
        if (!store.topics[topicId])
            store.topics[topicId] = {
                id: topicId, title: "", body: "", author: "", domains: [], tsMs: store.nsToMs(ts),
                delivery: "", placeholder: true, live: false, arrivedMs: Date.now()
            };
        store.rev++;
    }

    function setDelivery(id, state) {
        store.deliveries[id] = state;
        if (store.topics[id]) store.topics[id].delivery = state;
        if (store.replies[id]) store.replies[id].delivery = state;
        store.rev++;
    }

    property bool backlogLoaded: false
    function loadBacklog() {
        if (store.backlogLoaded || !store.hasBackend) return;
        store.backlogLoaded = true;
        logos.watch(store.backend.loadBacklog(), function (json) {
            var list = [];
            try { list = JSON.parse(json); } catch (e) {
                store.log("could not parse backlog: " + e);
                return;
            }
            for (var i = 0; i < list.length; ++i) {
                var e = list[i];
                if (e.state) store.deliveries[e.id] = e.state;
                if (e.kind === "topic")
                    store.addTopic(e.id, e.title, e.body, e.author, e.domains || "", e.ts, false);
                else if (e.kind === "reply")
                    store.addReply(e.id, e.topicId, e.parentId || e.topicId, e.body, e.author, e.ts, false);
            }
            store.log("backlog restored: " + list.length + " post(s)");
        }, function (err) {
            store.log("backlog load failed: " + err);
            store.backlogLoaded = false;
        });
    }

    Connections {
        target: store.backend
        ignoreUnknownSignals: true
        function onTopicReceived(id, title, body, author, domains, timestamp) {
            store.addTopic(id, title, body, author, domains, timestamp, true);
        }
        function onReplyReceived(id, topicId, body, author, timestamp) {
            store.addReply(id, topicId, topicId, body, author, timestamp, true);
        }
        function onMessageStateChanged(id, state, detail) {
            store.log("messageStateChanged -> " + id + " " + state + (detail.length > 0 ? " (" + detail + ")" : ""));
            store.setDelivery(id, state);
        }
    }

    // ── Time ──────────────────────────────────────────────────────────────────
    function ago(ms) {
        var s = Math.max(0, (Date.now() - ms) / 1000);
        if (s < 60) return "now";
        if (s < 3600) return Math.floor(s / 60) + "m";
        if (s < 86400) return Math.floor(s / 3600) + "h";
        if (s < 7 * 86400) return Math.floor(s / 86400) + "d";
        return Qt.formatDate(new Date(ms), "dd MMM");
    }
    function clock(ms) { return Qt.formatTime(new Date(ms), "hh:mm"); }

    // Re-render relative times ("12m") once a minute.
    property int tick: 0
    Timer { interval: 60000; running: true; repeat: true; onTriggered: store.tick++ }

    // ── Derived lists ─────────────────────────────────────────────────────────
    function replyCount(topicId) {
        var n = 0;
        for (var id in store.replies) if (store.replies[id].topicId === topicId) ++n;
        return n;
    }

    function domainsText(domains) {
        return domains.length > 0 ? "#" + domains.join(" #") : "";
    }

    function topicItem(t) {
        var n = store.replyCount(t.id);
        var parts = [t.author, n === 0 ? "no replies yet" : n === 1 ? "1 reply" : n + " replies",
                     store.ago(t.tsMs)];
        if (t.domains.length > 0) parts.push(store.domainsText(t.domains));
        return {
            id: t.id,
            title: t.placeholder ? "Topic still arriving…" : t.title,
            excerpt: t.placeholder ? "Its replies got here first; the topic will appear once it syncs."
                                   : t.body.replace(/\s+/g, " "),
            meta: t.placeholder ? (n + (n === 1 ? " reply" : " replies")) : parts.join(" · "),
            delivery: t.delivery,
            placeholder: t.placeholder
        };
    }

    function matches(t, query) {
        if (query.length === 0) return true;
        var hay = (t.title + " " + t.body + " " + t.author + " " + t.domains.join(" ")).toLowerCase();
        return hay.indexOf(query) >= 0;
    }

    function feed(followedOnly) {
        var query = store.searchText.trim().toLowerCase().replace(/^#/, "");
        var out = [];
        for (var id in store.topics) {
            var t = store.topics[id];
            if (store.domainFilter.length > 0 && t.domains.indexOf(store.domainFilter) < 0) continue;
            if (followedOnly && !t.domains.some(function (d) { return store.followed.indexOf(d) >= 0; })) continue;
            if (!store.matches(t, query)) continue;
            out.push(t);
        }
        out.sort(function (a, b) { return b.tsMs - a.tsMs; });
        return out.map(store.topicItem);
    }

    readonly property var allFeed: { store.rev; store.tick; store.searchText; store.domainFilter; return store.feed(false); }
    readonly property var followedFeed: { store.rev; store.tick; store.searchText; store.domainFilter; store.followed; return store.feed(true); }

    // Domains in use, most used first (the chip row), followed ones first.
    readonly property var knownDomains: {
        store.rev;
        var counts = {};
        for (var id in store.topics)
            store.topics[id].domains.forEach(function (d) { counts[d] = (counts[d] || 0) + 1; });
        var names = Object.keys(counts);
        names.sort(function (a, b) { return counts[b] - counts[a] || a.localeCompare(b); });
        return names;
    }

    function isFollowed(domain) { return store.followed.indexOf(domain) >= 0; }
    function toggleFollow(domain) {
        var list = store.followed.slice();
        var i = list.indexOf(domain);
        if (i >= 0) list.splice(i, 1); else list.push(domain);
        store.followed = list;
    }

    // The open topic and its replies as a depth-first list:
    // [{ id, author, body, time, depth, delivery, isOp, hasChildren }]
    readonly property var currentTopic: {
        store.rev; store.tick;
        var t = store.topics[store.selectedTopicId];
        return t ? t : null;
    }

    readonly property var thread: {
        store.rev; store.tick;
        var topicId = store.selectedTopicId;
        var t = store.topics[topicId];
        var children = {};
        for (var id in store.replies) {
            var r = store.replies[id];
            if (r.topicId !== topicId) continue;
            var parent = store.replies[r.parentId] ? r.parentId : topicId;
            (children[parent] = children[parent] || []).push(r);
        }
        var out = [];
        function walk(parentId, depth) {
            var list = children[parentId] || [];
            list.sort(function (a, b) { return a.tsMs - b.tsMs; });
            list.forEach(function (r) {
                out.push({
                    id: r.id, author: r.author, body: r.body, time: store.ago(r.tsMs),
                    depth: depth, delivery: r.delivery,
                    isOp: t !== undefined && !t.placeholder && r.author === t.author && r.author !== "Anonymous",
                    hasChildren: (children[r.id] || []).length > 0
                });
                walk(r.id, depth + 1);
            });
        }
        walk(topicId, 0);
        return out;
    }

    // Who's talking in the open thread, in order of first appearance.
    readonly property var participants: {
        var seen = [];
        var t = store.currentTopic;
        if (t && !t.placeholder) seen.push(t.author);
        store.thread.forEach(function (r) { if (seen.indexOf(r.author) < 0) seen.push(r.author); });
        return seen;
    }

    function replyAuthor(id) { return store.replies[id] ? store.replies[id].author : ""; }

    // Posts written on this device, newest first: [{ id, kind, title, body, delivery, time, topicId }]
    function ownPosts(unsentOnly) {
        var out = [];
        function consider(p, isTopic) {
            if (!p.delivery) return;
            if (unsentOnly && p.delivery === "sent") return;
            var topic = isTopic ? p : store.topics[p.topicId];
            out.push({
                id: p.id, kind: isTopic ? "topic" : "reply",
                title: isTopic ? p.title : "reply in “" + (topic && !topic.placeholder ? topic.title : "a topic") + "”",
                body: isTopic ? "" : p.body,
                delivery: p.delivery,
                time: store.ago(p.tsMs),
                tsMs: p.tsMs,
                topicId: isTopic ? p.id : p.topicId
            });
        }
        for (var a in store.topics) consider(store.topics[a], true);
        for (var b in store.replies) consider(store.replies[b], false);
        out.sort(function (x, y) { return y.tsMs - x.tsMs; });
        return out;
    }
    readonly property var outbox: { store.rev; store.tick; return store.ownPosts(true); }
    readonly property var myPosts: { store.rev; store.tick; return store.ownPosts(false); }

    // Posts that reached us during this session but were written before it
    // started — i.e. what we missed while away and caught up on.
    readonly property var missed: {
        store.rev; store.tick;
        var cutoff = store.sessionStartMs - 60000;
        var out = [];
        function consider(p, isTopic) {
            if (!p.live || p.tsMs >= cutoff || p.placeholder) return;
            var topic = isTopic ? p : store.topics[p.topicId];
            out.push({
                id: p.id,
                title: isTopic ? p.title : "reply in “" + (topic && !topic.placeholder ? topic.title : "a topic") + "”",
                meta: p.author + " · posted " + store.ago(p.tsMs) + " ago · arrived " + store.clock(p.arrivedMs)
                      + (isTopic && p.domains.length > 0 ? " · " + store.domainsText(p.domains) : ""),
                topicId: isTopic ? p.id : p.topicId,
                arrivedMs: p.arrivedMs
            });
        }
        for (var a in store.topics) consider(store.topics[a], true);
        for (var b in store.replies) consider(store.replies[b], false);
        out.sort(function (x, y) { return y.arrivedMs - x.arrivedMs; });
        return out;
    }

    // ── Actions ───────────────────────────────────────────────────────────────
    // Run a backend call that answers "" on success or an error string.
    function call(pending, onOk, onErr) {
        logos.watch(pending, function (err) {
            if (err) { if (onErr) onErr(err); else store.lastError = err; }
            else if (onOk) onOk();
        }, function (e) { if (onErr) onErr(String(e)); else store.lastError = String(e); });
    }

    // Run a backend call that answers JSON {"error", …}.
    function callJson(pending, onOk, onErr) {
        logos.watch(pending, function (json) {
            var r = {};
            try { r = JSON.parse(json); } catch (e) { r = { error: "Unexpected reply" }; }
            if (r.error) onErr(r.error); else onOk(r);
        }, function (e) { onErr(String(e)); });
    }

    function createTopic(title, body, domains, disclosure, onOk, onErr) {
        store.call(store.backend.createTopic(title, body, domains, disclosure), onOk, onErr);
    }

    function sendReply(body, disclosure, onOk, onErr) {
        if (store.canNestReplies && store.replyTargetId.length > 0)
            store.call(store.backend.replyToPost(store.replyTargetId, body, disclosure), onOk, onErr);
        else
            store.call(store.backend.replyToTopic(store.selectedTopicId, body, disclosure), onOk, onErr);
    }

    // "Post as" choices with what each means, for the composers.
    function disclosureOptions() {
        return [
            { value: "persona", label: "persona" },
            { value: "alias", label: "alias" },
            { value: "anonymous", label: "anonymous" }
        ];
    }
    function disclosureHint(d) {
        if (d === "alias")
            return store.alias.length > 0 ? "shows as " + store.alias + " · " + store.myPersona
                                          : "set an alias first, in profile";
        if (d === "anonymous")
            return "one-time key, never reused · 20-bit proof of work (about a second)";
        var rot = store.rotationPolicy === "auto" ? "a fresh persona for this post (auto rotation)"
                : "signs as " + store.myPersona + " · rotation: " + store.rotationPolicy;
        return rot;
    }
}
