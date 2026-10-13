import QtQuick
import "markdown.js" as Md
import "links.js" as Links

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
    readonly property string followedJson:       hasBackend ? backend.followedDomains   : "[]"
    readonly property string network:            hasBackend && backend.network ? backend.network : ""
    readonly property string networkNext:        hasBackend && backend.networkNext ? backend.networkNext : ""
    readonly property string quotaJson:          hasBackend && backend.quotaJson ? backend.quotaJson : "{}"
    readonly property string inboxJson:          hasBackend && backend.inboxJson ? backend.inboxJson : "[]"
    readonly property string historyJson:        hasBackend && backend.historyJson ? backend.historyJson : "{}"
    // The unlocked account's own posts and their delivery state; "{}" while
    // locked. The one source of "my posts" (see applyOwnStates).
    readonly property string ownStatesJson:      hasBackend && backend.ownStatesJson ? backend.ownStatesJson : "{}"
    readonly property bool   restorePending:     hasBackend && backend.restorePending === true
    readonly property real   awaySinceMs:        hasBackend && backend.awaySinceMs ? Number(backend.awaySinceMs) : 0
    readonly property bool   devMode:            hasBackend && backend.devMode === true
    readonly property string forumsJson:         hasBackend && backend.forumsJson ? backend.forumsJson : "[]"
    readonly property string mediaJson:          hasBackend && backend.mediaJson ? backend.mediaJson : "{}"
    readonly property bool   mediaRehost:        hasBackend && backend.mediaRehost !== false
    readonly property bool   mediaAutoFull:      hasBackend && backend.mediaAutoFull !== false
    // Fetching older posts: see historyJson in forumer.rep.
    readonly property var history: {
        try { return JSON.parse(store.historyJson); } catch (e) { return {}; }
    }
    readonly property string storageJson:        hasBackend && backend.storageJson ? backend.storageJson : "{}"
    // The storage probe (Settings): see storageJson in forumer.rep.
    readonly property var storage: {
        try { return JSON.parse(store.storageJson); } catch (e) { return {}; }
    }

    // What's left of this account's hourly limits:
    //   { topics: {left, max, waitMin}, replies: {left, max, waitMin} }
    // Until the backend reports, the full limits (forumer_core/flood.h) - so
    // the quota line is there from the first look, not a moment later (F15).
    readonly property var quota: {
        const full = { topics: { left: 10, max: 10, waitMin: 0 }, replies: { left: 60, max: 60, waitMin: 0 } };
        try {
            const q = JSON.parse(quotaJson);
            return { topics: q.topics || full.topics, replies: q.replies || full.replies };
        } catch (e) {
            return full;
        }
    }
    readonly property bool topicsLeft:  quota.topics.left  === undefined || quota.topics.left  > 0
    readonly property bool repliesLeft: quota.replies.left === undefined || quota.replies.left > 0

    readonly property bool unlocked: identityState === "unlocked"
    // canCompose: the composers open (the restore notice lives in them);
    // canPost: posting itself, which also waits for a just-restored account.
    readonly property bool canCompose: nodeReady && unlocked
    readonly property bool canPost: canCompose && !restorePending
    readonly property bool connected: status === "Connected" || status === "PartiallyConnected"

    // Replying to a reply needs the backend's replyToPost; an older backend
    // only answers topics.
    readonly property bool canNestReplies: hasBackend && typeof backend.replyToPost === "function"

    // Followed domains, saved with the unlocked account by the backend.
    readonly property var followed: {
        try { return JSON.parse(store.followedJson); } catch (e) { return []; }
    }

    readonly property var accounts: {
        try { return JSON.parse(store.accountsJson); } catch (e) { return []; }
    }

    // ── Forums ────────────────────────────────────────────────────────────────
    // "public", or one of the unlocked account's private forums (forums.h in
    // forumer_core): [{ id, name, joinedMs }]. Home, the composer and the
    // domain chips show the current one.
    readonly property bool canUsePrivateForums: hasBackend && typeof backend.createPrivateForum === "function"
    readonly property var forums: {
        try { return JSON.parse(store.forumsJson); } catch (e) { return []; }
    }
    property string currentForum: "public"
    readonly property bool inPrivateForum: currentForum !== "public"

    function forumName(id) {
        if (!id || id === "public") return "public";
        for (var i = 0; i < store.forums.length; ++i)
            if (store.forums[i].id === id) return store.forums[i].name;
        return "a private forum";
    }
    function isJoined(id) {
        if (id === "public") return true;
        return store.forums.some(function (f) { return f.id === id; });
    }
    // `fresh`: just created or joined - the list naming it may arrive a
    // moment after the reply that says so.
    function openForum(id, fresh) {
        store.currentForum = fresh === true || store.isJoined(id) ? id : "public";
        store.domainFilter = "";
        store.open("home");
    }

    // Lock, switch account or leave: posts of forums no longer listed go
    // from the view (the backend can't open them any more either).
    // A private forum's post can reach the view a moment before the list
    // that names its forum: it waits here until the list changes, then it is
    // shown or dropped.
    property var parked: []
    function park(kind, args) {
        if (store.parked.length < 5000)
            store.parked.push({ kind: kind, args: Array.prototype.slice.call(args) });
    }
    onForumsChanged: {
        var waiting = store.parked;
        store.parked = [];
        waiting.forEach(function (p) {
            if (!store.isJoined(p.args[8])) return;    // forum: the 9th argument of both
            if (p.kind === "topic") store.addTopic.apply(null, p.args);
            else store.addReply.apply(null, p.args);
        });
        var dropped = 0;
        for (var a in store.topics)
            if (!store.isJoined(store.topics[a].forum)) { delete store.topics[a]; ++dropped; }
        for (var b in store.replies)
            if (!store.isJoined(store.replies[b].forum)) { delete store.replies[b]; ++dropped; }
        if (!store.isJoined(store.currentForum)) store.currentForum = "public";
        if (dropped > 0) store.rev++;
    }

    // The invite link waiting to be joined (JoinForumSheet): { key, name }.
    property var pendingInvite: null

    function createPrivateForum(name, onOk, onErr) {
        store.callJson(store.backend.createPrivateForum(name), function (r) {
            store.openForum(r.id, true);
            onOk(r);
        }, onErr);
    }
    function joinPrivateForum(key, name, onOk, onErr) {
        store.callJson(store.backend.joinPrivateForum(key, name), function (r) {
            store.pendingInvite = null;
            store.openForum(r.id, true);
            onOk(r);
        }, onErr);
    }
    function leavePrivateForum(id, onOk, onErr) {
        store.call(store.backend.leavePrivateForum(id), function () {
            store.openForum("public");
            if (onOk) onOk();
        }, onErr);
    }
    function privateForumInvite(id, onOk, onErr) {
        store.callJson(store.backend.privateForumInvite(id), onOk, onErr);
    }

    // ── Images (forumer_core/media.h) ─────────────────────────────────────────
    readonly property bool canAttachImages: hasBackend && typeof backend.attachImage === "function"
    // { storage: "off"|"starting"|"running"|"failed", detail, attachments: [...] }
    readonly property var media: {
        try { return JSON.parse(store.mediaJson); } catch (e) { return {}; }
    }
    readonly property string storageState: store.media.storage || "off"
    // Attachments of one composer ("topic" | "reply").
    function attachments(group) {
        return (store.media.attachments || []).filter(function (a) { return a.group === group; });
    }
    // Whether a composer's images let it post: none, or all uploaded.
    function attachmentsReady(group) {
        return store.attachments(group).every(function (a) { return a.state === "ready"; });
    }
    function attachmentNote(group) {
        var list = store.attachments(group);
        if (list.length === 0) return "";
        var waiting = list.filter(function (a) { return a.state !== "ready"; }).length;
        if (list.some(function (a) { return a.state === "failed"; }))
            return "an image couldn't be uploaded - retry or remove it";
        if (waiting === 0) return list.length === 1 ? "image ready" : list.length + " images ready";
        if (store.storageState === "starting" || store.storageState === "off")
            return "starting the storage node (about 2 minutes the first time) - images upload once it runs";
        if (store.storageState === "failed")
            return "the storage node didn't start: " + (store.media.detail || "") + " - press retry on an image";
        return "uploading " + waiting + (waiting === 1 ? " image" : " images") + " - you can post once done";
    }
    function attachImage(group, forum, path, onErr) {
        store.call(store.backend.attachImage(group, forum, String(path)), null, onErr);
    }
    function removeAttachment(group, id) { store.call(store.backend.removeAttachment(group, id)); }
    function retryAttachment(group, id) { store.call(store.backend.retryAttachment(group, id)); }
    function clearAttachments(group) {
        if (store.canAttachImages) store.call(store.backend.clearAttachments(group));
    }
    function chooseMediaRehost(on) { store.call(store.backend.chooseMediaRehost(on)); }
    function chooseMediaAutoFull(on) { store.call(store.backend.chooseMediaAutoFull(on)); }

    // Images in posts: "<postId>:<index>" -> { state: "idle"|"preview"|"starting"|"fetching"|"done"|"failed",
    // detail, startedMs, previewUrl }. showMedia (a thread on screen) shows
    // what this device has - the full image or its preview - and fetches the
    // rest; loadMedia (a click) fetches the full image.
    property var mediaStates: ({})
    property int mediaRev: 0
    function mediaState(postId, index) {
        store.mediaRev;
        return store.mediaStates[postId + ":" + index] || { state: "idle", detail: "" };
    }
    function loadMedia(postId, index) {
        var key = postId + ":" + index;
        var prev = store.mediaStates[key] || {};
        store.mediaStates[key] = { state: "starting", detail: "", startedMs: Date.now(), previewUrl: prev.previewUrl };
        store.mediaRev++;
        store.call(store.backend.loadMedia(postId, index), null, function (err) {
            store.mediaStates[key] = { state: "failed", detail: String(err), previewUrl: prev.previewUrl };
            store.mediaRev++;
        });
    }
    function showMedia(postId, index) {
        var key = postId + ":" + index;
        store.log("media " + postId.substring(0, 8) + ":" + index + " on screen" + (store.mediaStates[key] ? " (already " + store.mediaStates[key].state + ")" : ""));
        if (store.mediaStates[key] || !store.hasBackend || typeof store.backend.showMedia !== "function") return;
        store.mediaStates[key] = { state: "idle", detail: "" };
        store.mediaRev++;
        store.call(store.backend.showMedia(postId, index), null, function (e) { store.log("showMedia: " + e); });
    }
    function parseMedia(json) {
        try { var list = JSON.parse(json || "[]"); return Array.isArray(list) ? list : []; }
        catch (e) { return []; }
    }
    function mediaNote(list) {
        return list && list.length > 0 ? (list.length === 1 ? "1 image" : list.length + " images") : "";
    }
    function sizeText(bytes) {
        var n = Number(bytes) || 0;
        if (n >= 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + " MB";
        return Math.max(1, Math.round(n / 1024)) + " KB";
    }

    // ── Navigation & UI state ─────────────────────────────────────────────────
    // "home" | "thread" | "replies" | "missed" | "mine" | "chats" | "profile" | "settings" | "backup"
    property string screen: "home"
    property string selectedTopicId: ""
    property string replyTargetId: ""        // "" = reply to the topic itself
    property string searchText: ""
    property string domainFilter: ""         // "" = all domains
    property string lastError: ""
    property string pendingPhrase: ""        // shown full-screen until confirmed
    readonly property real sessionStartMs: Date.now()

    function open(name) {
        store.screen = name;
        store.lastError = "";
    }

    // ── Shareable links (links.js) ───────────────────────────────────────────
    // Domains offered to newcomers next to the ones already in use.
    readonly property var suggestedDomains: ["logosdevs", "lambdabuilders", "testnet-help", "privacy", "tech"]

    // A link opened before the account was unlocked waits here.
    property var pendingLink: null
    // A thread link this device doesn't hold yet: { topic, day, startedMs, error }.
    property var linkWait: null

    function topicLink(id) {
        var t = store.topics[id];
        return Links.topicLink(id, t ? t.tsMs : Date.now());
    }
    function domainLink(domain) { return Links.domainLink(domain); }
    function parseLink(text) { return Links.parse(text); }
    function linkFromParams(params) { return Links.fromParams(params); }

    // Opens a parsed link: { kind: "topic", topic, day } | { kind: "domain", domain }
    // | { kind: "invite", key, name }.
    function openLink(link) {
        if (!link) return false;
        if (!store.unlocked) { store.pendingLink = link; return true; }
        store.pendingLink = null;
        store.searchText = "";
        if (link.kind === "invite") {
            // Never joined silently: JoinForumSheet asks first.
            store.pendingInvite = { key: link.key, name: link.name };
            return true;
        }
        if (link.kind === "domain") {
            store.currentForum = "public";
            store.domainFilter = link.domain;
            store.open("home");
            return true;
        }
        var held = store.topics[link.topic];
        store.linkWait = held && !held.placeholder ? null
                       : { topic: link.topic, day: link.day, startedMs: Date.now(), error: "" };
        store.openTopic(link.topic);
        if (store.linkWait) store.askAroundLink();
        return true;
    }

    // Ask peers for the thread behind linkWait: the days around its date, or
    // (a bare id without a date) an ordinary catch-up.
    function askAroundLink() {
        var w = store.linkWait;
        if (!w || !store.hasBackend) return;
        var fail = function (e) {
            if (store.linkWait && store.linkWait.topic === w.topic) {
                var copy = Object.assign({}, store.linkWait);
                copy.error = String(e);
                store.linkWait = copy;
            }
        };
        if (w.day > 0 && typeof store.backend.fetchAround === "function")
            store.call(store.backend.fetchAround(w.day), null, fail);
        else
            store.call(store.backend.catchUp(), null, fail);
    }

    function openTopic(id) {
        store.selectedTopicId = id;
        var t = store.topics[id];
        if (t && store.isJoined(t.forum)) store.currentForum = t.forum;
        store.replyTargetId = "";
        store.open("thread");
        // Replies to you in this thread: highlight them, and they are now read.
        var fresh = {};
        var ids = [];
        store.inboxRaw.forEach(function (it) {
            if (it.topicId === id && !it.read) { fresh[it.id] = true; ids.push(it.id); }
        });
        store.freshIds = fresh;
        store.rev++;
        store.markRepliesRead(ids);
    }

    // ── Posts ─────────────────────────────────────────────────────────────────
    property int rev: 0
    property var topics: ({})     // id -> { id, title, body, author, authorKey, domains[], tsMs, delivery, placeholder, live, arrivedMs, forum }
    property var replies: ({})    // id -> { id, topicId, parentId, body, author, authorKey, tsMs, delivery, live, arrivedMs, forum }
    property var deliveries: ({}) // id -> state, for posts whose state arrives before the post

    function nsToMs(ts) { return Math.floor(Number(ts) / 1000000); }

    function addTopic(id, title, body, author, domains, ts, live, authorKey, forum, media) {
        var existing = store.topics[id];
        if (existing && !existing.placeholder) return;
        forum = forum || "public";
        if (!store.isJoined(forum)) { store.park("topic", arguments); return; }
        store.topics[id] = {
            forum: forum,
            media: store.parseMedia(media),
            id: id, title: title, body: body, author: author || "", authorKey: authorKey || "",
            domains: (domains || "").split(",").filter(function (d) { return d.length > 0; }),
            tsMs: store.nsToMs(ts),
            delivery: store.deliveries[id] || "",
            placeholder: false,
            live: live === true,
            arrivedMs: Date.now()
        };
        if (store.linkWait && store.linkWait.topic === id) store.linkWait = null;
        store.rev++;
    }

    function addReply(id, topicId, parentId, body, author, ts, live, authorKey, forum, media) {
        if (store.replies[id]) return;
        forum = forum || "public";
        if (!store.isJoined(forum)) { store.park("reply", arguments); return; }
        store.replies[id] = {
            forum: forum,
            media: store.parseMedia(media),
            id: id, topicId: topicId, parentId: parentId || topicId, body: body, author: author || "",
            authorKey: authorKey || "",
            tsMs: store.nsToMs(ts),
            delivery: store.deliveries[id] || "",
            live: live === true,
            arrivedMs: Date.now()
        };
        // A reply can outrun its topic: stand in a placeholder until it arrives.
        if (!store.topics[topicId])
            store.topics[topicId] = {
                forum: forum, media: [],
                id: topicId, title: "", body: "", author: "", authorKey: "", domains: [], tsMs: store.nsToMs(ts),
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

    // Posts already on this device. Asked for only once the backend has opened
    // its post log (nodeReady): asked any earlier, the answer is an empty list
    // and the forum would look empty until restart. Safe to repeat — posts are
    // de-duplicated by id.
    property bool backlogLoaded: false
    property bool viewReady: false          // set by Main once the replica is connected
    function addBacklogEntries(list) {
        for (var i = 0; i < list.length; ++i) {
            var e = list[i];
            if (e.kind === "topic")
                store.addTopic(e.id, e.title, e.body, e.author, e.domains || "", e.ts, false, e.authorKey, e.forum, e.media);
            else if (e.kind === "reply")
                store.addReply(e.id, e.topicId, e.parentId || e.topicId, e.body, e.author, e.ts, false, e.authorKey, e.forum, e.media);
        }
    }
    // A page at a time (review F18), each one a short trip, so a big log
    // never crosses as one huge string and the view stays usable meanwhile.
    function loadBacklog() {
        if (store.backlogLoaded || !store.hasBackend || !store.viewReady || !store.nodeReady) return;
        store.backlogLoaded = true;
        var total = 0;
        var paged = typeof store.backend.loadBacklogPage === "function";
        function next(cursor) {
            logos.watch(store.backend.loadBacklogPage(cursor), function (json) {
                var page = {};
                try { page = JSON.parse(json); } catch (e) {
                    store.log("could not parse a backlog page: " + e);
                    return;
                }
                store.addBacklogEntries(page.posts || []);
                total += (page.posts || []).length;
                if (!page.done && page.cursor !== cursor) { next(page.cursor); return; }
                store.applyOwnStates();
                store.log("backlog restored: " + total + " post(s)");
            }, function (err) {
                store.log("backlog load failed: " + err);
                store.backlogLoaded = false;
            });
        }
        if (paged) { next(""); return; }
        // An older backend without pages.
        logos.watch(store.backend.loadBacklog(), function (json) {
            var list = [];
            try { list = JSON.parse(json); } catch (e) { store.log("could not parse backlog: " + e); return; }
            store.addBacklogEntries(list);
            store.applyOwnStates();
            store.log("backlog restored: " + list.length + " post(s)");
        }, function (err) {
            store.log("backlog load failed: " + err);
            store.backlogLoaded = false;
        });
    }

    onViewReadyChanged: store.loadBacklog()
    onNodeReadyChanged: store.loadBacklog()

    // Which posts are "mine" belongs to the unlocked account: on lock, unlock
    // or switch, replace every delivery mark with that account's list, so one
    // account never sees another's posts in "My posts" or the outbox.
    function applyOwnStates() {
        var map = {};
        try { map = JSON.parse(store.ownStatesJson); } catch (e) { map = {}; }
        var deliveries = {};
        for (var id in map) {
            // A live "propagated" waypoint outranks the stored "pending".
            var live = store.deliveries[id];
            deliveries[id] = (map[id] === "pending" && live === "propagated") ? live : map[id];
        }
        store.deliveries = deliveries;
        for (var a in store.topics) store.topics[a].delivery = deliveries[a] || "";
        for (var b in store.replies) store.replies[b].delivery = deliveries[b] || "";
        store.rev++;
    }
    onOwnStatesJsonChanged: store.applyOwnStates()

    Connections {
        target: store.backend
        ignoreUnknownSignals: true
        // `forum` is missing from an older backend's signals: "public".
        function onTopicReceived(id, title, body, author, domains, timestamp, authorKey, forum, media) {
            store.addTopic(id, title, body, author, domains, timestamp, true, authorKey, forum, media);
        }
        function onReplyReceived(id, topicId, parentId, body, author, timestamp, authorKey, forum, media) {
            store.addReply(id, topicId, parentId, body, author, timestamp, true, authorKey, forum, media);
        }
        function onMediaLoaded(postId, index, state, detail) {
            var key = postId + ":" + index;
            var prev = store.mediaStates[key] || {};
            store.log("media " + key.substring(0, 8) + ":" + index + " -> " + state
                      + (state === "preview" || state === "done" ? " (" + String(detail).length + " chars)" : " " + detail)
                      + " · was " + (prev.state || "-") + (prev.previewUrl ? " +preview" : ""));
            if (state === "preview") {
                // Shown while the full image is on its way (or not asked for).
                var keep = prev.state === "starting" || prev.state === "fetching" || prev.state === "done";
                store.mediaStates[key] = { state: keep ? prev.state : "preview", detail: keep ? prev.detail : "",
                                           startedMs: prev.startedMs, previewUrl: detail };
            } else {
                var busy = state === "starting" || state === "fetching";
                store.mediaStates[key] = { state: state, detail: detail,
                                           startedMs: busy ? (prev.startedMs || Date.now()) : prev.startedMs,
                                           previewUrl: prev.previewUrl };
            }
            store.mediaRev++;
        }
        function onPreviewReady(postId, index) {
            var key = postId + ":" + index;
            var st = store.mediaStates[key];
            if (!st || st.previewUrl || st.state === "done") return;
            store.call(store.backend.showMedia(postId, index), null, function (e) { store.log("showMedia: " + e); });
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
        if (t.media && t.media.length > 0) parts.push(store.mediaNote(t.media));
        if (t.domains.length > 0) parts.push(store.domainsText(t.domains));
        return {
            id: t.id,
            title: t.placeholder ? "Topic still arriving…" : t.title,
            excerpt: t.placeholder ? "Its replies got here first; the topic will appear once it syncs."
                                   : Md.plain(t.body),
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
            if (t.forum !== store.currentForum) continue;
            if (store.domainFilter.length > 0 && t.domains.indexOf(store.domainFilter) < 0) continue;
            if (followedOnly && !t.domains.some(function (d) { return store.followed.indexOf(d) >= 0; })) continue;
            if (!store.matches(t, query)) continue;
            out.push(t);
        }
        out.sort(function (a, b) { return b.tsMs - a.tsMs; });
        return out.map(store.topicItem);
    }

    readonly property var allFeed: { store.rev; store.tick; store.searchText; store.domainFilter; store.currentForum; return store.feed(false); }
    readonly property var followedFeed: { store.rev; store.tick; store.searchText; store.domainFilter; store.currentForum; store.followed; return store.feed(true); }

    // Domains in use, most used first (the chip row), followed ones first.
    readonly property var knownDomains: {
        store.rev; store.currentForum;
        var counts = {};
        for (var id in store.topics)
            if (store.topics[id].forum === store.currentForum)
                store.topics[id].domains.forEach(function (d) { counts[d] = (counts[d] || 0) + 1; });
        var names = Object.keys(counts);
        names.sort(function (a, b) { return counts[b] - counts[a] || a.localeCompare(b); });
        return names;
    }

    function isFollowed(domain) { return store.followed.indexOf(domain) >= 0; }
    function toggleFollow(domain) {
        if (store.isFollowed(domain)) store.call(store.backend.unfollowDomain(domain));
        else store.call(store.backend.followDomain(domain));
    }

    // The open topic and its replies, in reading order. Threads are two levels
    // deep (forumer_core/thread.h): level-1 replies answer the topic (depth 0
    // here), level-2 replies sit in the sub-thread under one (depth 1). A reply
    // whose parent hasn't arrived yet shows at level 1 until it does.
    // [{ id, author, body, time, depth, delivery, isOp, subthread }]
    readonly property var currentTopic: {
        store.rev; store.tick;
        var t = store.topics[store.selectedTopicId];
        return t ? t : null;
    }

    // The level-1 reply a reply belongs under ("" = it is level 1 itself).
    function subthreadOf(r, topicId) {
        var seen = 0;
        var cur = r;
        while (cur.parentId !== topicId && store.replies[cur.parentId] && seen++ < 16)
            cur = store.replies[cur.parentId];
        return cur === r ? "" : cur.id;
    }

    readonly property var thread: {
        store.rev; store.tick;
        var topicId = store.selectedTopicId;
        var t = store.topics[topicId];
        var level1 = [];
        var under = {};      // level-1 id -> [level-2 replies]
        for (var id in store.replies) {
            var r = store.replies[id];
            if (r.topicId !== topicId) continue;
            var top = store.subthreadOf(r, topicId);
            if (top === "") level1.push(r);
            else (under[top] = under[top] || []).push(r);
        }
        var byTime = function (a, b) { return a.tsMs - b.tsMs; };
        level1.sort(byTime);
        var out = [];
        function item(r, depth, subthread) {
            return {
                id: r.id, author: r.author, body: r.body, time: store.ago(r.tsMs), media: r.media || [],
                depth: depth, delivery: r.delivery, subthread: subthread,
                fresh: store.freshIds[r.id] === true,
                // Same signing key as the topic: display text can be imitated (F5).
                isOp: t !== undefined && !t.placeholder && t.authorKey !== "" && r.authorKey === t.authorKey
            };
        }
        level1.forEach(function (r) {
            out.push(item(r, 0, r.id));
            (under[r.id] || []).sort(byTime).forEach(function (c) { out.push(item(c, 1, r.id)); });
        });
        return out;
    }

    // What the composer says it is answering.
    function replyTargetNote(id) {
        var r = store.replies[id];
        if (!r) return "";
        var top = store.subthreadOf(r, r.topicId);
        if (top === "") return "starts a sub-thread under this reply";
        var head = store.replies[top];
        return "joins the sub-thread under " + (head ? head.author : "this reply");
    }

    // Who's talking in the open thread, in order of first appearance.
    readonly property var participants: {
        var seen = [];
        var t = store.currentTopic;
        if (t && !t.placeholder) seen.push(t.author);
        store.thread.forEach(function (r) { if (seen.indexOf(r.author) < 0) seen.push(r.author); });
        return seen;
    }

    // " · in <forum>" for a private forum's post in lists that mix forums.
    function forumNote(p) {
        return p && p.forum && p.forum !== "public" ? " · in " + store.forumName(p.forum) : "";
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
                title: (isTopic ? p.title : "reply in “" + (topic && !topic.placeholder ? topic.title : "a topic") + "”")
                       + store.forumNote(p),
                body: isTopic ? "" : Md.plain(p.body),
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
    // started — i.e. what we missed while away and caught up on. Older
    // history (fetched week by week) isn't "missed": it shows in the feed.
    readonly property var missed: {
        store.rev; store.tick;
        var cutoff = store.sessionStartMs - 60000;
        // Written while this device was away: since it last synced, at most
        // a week back. A new device (never synced) missed nothing (F15).
        if (store.awaySinceMs <= 0) return [];
        var oldest = Math.max(store.sessionStartMs - 7 * 24 * 3600 * 1000, store.awaySinceMs - 60000);
        var out = [];
        function consider(p, isTopic) {
            if (!p.live || p.tsMs >= cutoff || p.tsMs < oldest || p.placeholder) return;
            var topic = isTopic ? p : store.topics[p.topicId];
            out.push({
                id: p.id,
                title: isTopic ? p.title : "reply in “" + (topic && !topic.placeholder ? topic.title : "a topic") + "”",
                meta: p.author + " · posted " + store.ago(p.tsMs) + " ago · arrived " + store.clock(p.arrivedMs)
                      + (isTopic && p.domains.length > 0 ? " · " + store.domainsText(p.domains) : "")
                      + store.forumNote(p),
                topicId: isTopic ? p.id : p.topicId,
                arrivedMs: p.arrivedMs
            });
        }
        for (var a in store.topics) consider(store.topics[a], true);
        for (var b in store.replies) consider(store.replies[b], false);
        out.sort(function (x, y) { return y.arrivedMs - x.arrivedMs; });
        return out.slice(0, 100);
    }

    // "12 Sep" / "12 Sep 2025" for history dates.
    function day(ms) {
        var d = new Date(ms);
        return Qt.formatDate(d, d.getFullYear() === new Date().getFullYear() ? "dd MMM" : "dd MMM yyyy");
    }

    // ── Replies to you ────────────────────────────────────────────────────────
    // The backend works out, on this device only, which replies answer this
    // account's posts or sit in its topics (inboxJson); here they are joined
    // with the posts we hold. Nothing about it goes on the network.
    readonly property var inboxRaw: {
        try { return JSON.parse(store.inboxJson); } catch (e) { return []; }
    }
    readonly property int unreadReplies: {
        var n = 0;
        store.inboxRaw.forEach(function (it) { if (!it.read) ++n; });
        return n;
    }
    // [{ id, topicId, author, body, title, time, direct, read }], newest first.
    readonly property var inbox: {
        store.rev; store.tick;
        var out = [];
        store.inboxRaw.forEach(function (it) {
            var r = store.replies[it.id];
            if (!r) return;                       // not shown to the view yet
            var t = store.topics[it.topicId];
            out.push({
                id: it.id, topicId: it.topicId, author: r.author, body: Md.plain(r.body),
                title: t && !t.placeholder ? t.title : "a topic",
                forumNote: store.forumNote(r),
                time: store.ago(r.tsMs), direct: it.direct, inMyTopic: it.inMyTopic === true, read: it.read
            });
        });
        return out;
    }

    function inboxVerb(it) {
        return it.direct ? " replied to you"
             : it.inMyTopic === true ? " replied in your topic"
             : " replied in a thread you joined";
    }

    // Replies highlighted in the open thread (unread when it was opened).
    property var freshIds: ({})

    function markRepliesRead(ids) {
        if (!store.hasBackend || !store.unlocked || ids.length === 0) return;
        store.call(store.backend.markRepliesRead(ids.join(",")), null, function (e) { store.log(e); });
    }
    function markAllRepliesRead() {
        if (!store.hasBackend || !store.unlocked) return;
        store.call(store.backend.markRepliesRead(""), null, function (e) { store.log(e); });
    }

    // A small notice for a reply that arrives while the app is open:
    // { id, topicId, text, body }, or null.
    property var toast: null
    property var inboxKnown: ({})      // ids already in the inbox (no notice for those)
    property bool inboxPrimed: false   // the first inbox after unlock is history, not news

    onIdentityStateChanged: {
        // The inbox for the newly unlocked account arrives before this change:
        // what's in it is history, not news.
        var known = {};
        if (store.unlocked) store.inboxRaw.forEach(function (it) { known[it.id] = true; });
        store.inboxKnown = known;
        store.inboxPrimed = store.unlocked;
        store.toast = null;
        store.freshIds = {};
    }
    onInboxRawChanged: {
        var known = store.inboxKnown;
        var newest = null;
        var readNow = [];
        store.inboxRaw.forEach(function (it) {
            if (known[it.id]) return;
            known[it.id] = true;
            if (!store.inboxPrimed || it.read) return;
            if (store.screen === "thread" && store.selectedTopicId === it.topicId) {
                // Already looking at it: highlight, and it's read.
                store.freshIds[it.id] = true;
                readNow.push(it.id);
            } else if (!newest) {
                newest = it;               // inboxRaw is newest first
            }
        });
        store.inboxKnown = known;
        if (store.unlocked) store.inboxPrimed = true;
        if (readNow.length > 0) { store.rev++; store.markRepliesRead(readNow); }
        if (newest) {
            var r = store.replies[newest.id];
            store.toast = {
                id: newest.id, topicId: newest.topicId,
                text: (r ? r.author : "someone") + store.inboxVerb(newest),
                body: r ? Md.plain(r.body) : ""
            };
        }
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

    // ── Limits (forumer_core/post.h), counted the same way: bytes of UTF-8 ──
    readonly property int maxTitleBytes: 200
    readonly property int maxBodyBytes: 10000
    function utf8Bytes(s) {
        var n = 0;
        s = String(s || "");
        for (var i = 0; i < s.length; ++i) {
            var c = s.charCodeAt(i);
            if (c < 0x80) n += 1;
            else if (c < 0x800) n += 2;
            else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length) { n += 4; ++i; }  // surrogate pair
            else n += 3;
        }
        return n;
    }
    // "123 / 200", or how far over.
    function sizeNote(s, max) {
        var n = store.utf8Bytes(s);
        return n > max ? (n - max) + " over the limit" : n + " / " + max;
    }

    // What the backend will make of typed domains (post::normalizeDomains):
    // { kept: ["privacy", "caf"], changed: ["café → caf"] } - so the composer
    // can say what a domain becomes, or that it can't be one, before posting.
    function domainPreview(text) {
        var kept = [], changed = [];
        String(text || "").split(",").forEach(function (raw) {
            var tag = raw.trim();
            if (tag.length === 0) return;
            var clean = "", dash = false;
            for (var i = 0; i < tag.length; ++i) {
                var ch = tag[i];
                if (/\s|-|_/.test(ch)) dash = clean.length > 0;
                else if (/[A-Za-z0-9]/.test(ch)) {
                    if (dash) clean += "-";
                    dash = false;
                    clean += ch.toLowerCase();
                }
            }
            if (clean.length < 2 || clean.length > 32) { changed.push(tag + " can't be a domain"); return; }
            if (clean !== tag.toLowerCase().replace(/^#/, "").replace(/[\s_]+/g, "-"))
                changed.push(tag + " → " + clean);
            if (kept.indexOf(clean) < 0 && kept.length < 3) kept.push(clean);
        });
        return { kept: kept, changed: changed };
    }

    function postAfterRestore() {
        if (typeof store.backend.postAfterRestore === "function")
            store.call(store.backend.postAfterRestore());
    }

    // In the current forum. Replies need no forum: they follow their thread.
    function createTopic(title, body, domains, disclosure, onOk, onErr) {
        if (store.canAttachImages && store.attachments("topic").length > 0)
            store.call(store.backend.postTopic(store.currentForum, title, body, domains, disclosure, "topic"), onOk, onErr);
        else if (store.inPrivateForum)
            store.call(store.backend.createTopicIn(store.currentForum, title, body, domains, disclosure), onOk, onErr);
        else
            store.call(store.backend.createTopic(title, body, domains, disclosure), onOk, onErr);
    }

    function sendReply(body, disclosure, onOk, onErr) {
        if (store.canAttachImages && store.attachments("reply").length > 0)
            store.call(store.backend.postReply(store.replyTargetId.length > 0 ? store.replyTargetId
                                                                               : store.selectedTopicId,
                                               body, disclosure, "reply"), onOk, onErr);
        else if (store.canNestReplies)
            store.call(store.backend.replyToPost(store.replyTargetId.length > 0 ? store.replyTargetId
                                                                                  : store.selectedTopicId,
                                                 body, disclosure), onOk, onErr);
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
            return "one-time key, never reused · 20-bit proof of work (a second or so; longer for long posts)";
        var rot = store.rotationPolicy === "auto" ? "a fresh persona for this post (auto rotation)"
                : "signs as " + store.myPersona + " · rotation: " + store.rotationPolicy;
        return rot;
    }
}
