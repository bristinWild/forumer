import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

Item {
    id: root

    // One consistently-tagged line per QML lifecycle / callback. console.log is
    // routed to stderr, so these land in the same stream as the backend's logs.
    function log(msg) { console.log("[forumer qml] " + msg); }

    // Typed replica — auto-synced properties and callable slots.
    readonly property var backend: logos.module("forumer")
    property bool ready: false

    readonly property string monoFont: "monospace"

    // ── PROPs from forumer.rep ────────────────────────────────────────────────
    readonly property string status:     backend ? backend.status     : ""
    readonly property bool   nodeReady:  backend ? backend.nodeReady  : false
    readonly property string topic:      backend ? backend.topic      : ""
    readonly property string appVersion: backend ? backend.appVersion : ""
    readonly property int    unsentCount: backend ? backend.unsentCount : 0
    readonly property string syncInfo:   backend ? backend.syncInfo   : ""

    readonly property string identityState:     backend ? backend.identityState     : "none"
    readonly property string accountsJson:      backend ? backend.accountsJson      : "[]"
    readonly property string selectedAccountId: backend ? backend.selectedAccountId : ""
    readonly property string myLabel:           backend ? backend.myLabel           : ""
    readonly property string myPersona:         backend ? backend.myPersona         : ""
    readonly property string rotationPolicy:    backend ? backend.rotationPolicy    : "manual"
    readonly property string defaultDisclosure: backend ? backend.defaultDisclosure : "persona"
    readonly property string alias:             backend ? backend.alias             : ""

    readonly property bool unlocked: root.identityState === "unlocked"
    readonly property bool canPost: root.nodeReady && root.unlocked

    // Open thread (right pane) and transient error lines.
    property string selectedTopicId: ""
    property string selectedTitle: ""
    property string selectedBody: ""
    property string selectedMeta: ""
    property bool selectedIsPlaceholder: false
    property string lastError: ""
    property string identityError: ""     // onboarding / unlock screens
    property string dialogError: ""       // modal dialogs

    // A freshly created/revealed recovery phrase, shown on the full-screen
    // phrase step until the user confirms they have written it down.
    property string pendingPhrase: ""

    onStatusChanged: log("status -> \"" + status + "\"")
    onIdentityStateChanged: {
        log("identityState -> " + identityState);
        root.identityError = "";
    }

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "forumer")
                root.ready = isReady && root.backend !== null;
        }
    }

    // Verified posts, pushed by the backend (our own included, as they're saved).
    Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onTopicReceived(id, title, body, author, domains, timestamp) {
            root.addTopic(id, title, body, author, domains, timestamp);
        }
        function onReplyReceived(id, topicId, body, author, timestamp) {
            root.addReply(id, topicId, body, author, timestamp);
        }
        function onMessageStateChanged(id, state, detail) {
            root.log("messageStateChanged -> " + id + " " + state
                     + (detail.length > 0 ? " (" + detail + ")" : ""));
            root.setMessageState(id, state, detail);
        }
    }

    // Posts already in the local store, pulled once the replica is up (a
    // startup signal would reach no replica — see forumer.rep).
    property bool backlogLoaded: false

    function loadBacklog() {
        if (root.backlogLoaded || !root.ready || !root.backend) return;
        root.backlogLoaded = true;
        logos.watch(backend.loadBacklog(), function (json) {
            var list = [];
            try { list = JSON.parse(json); } catch (e) {
                root.log("could not parse backlog: " + e);
                return;
            }
            for (var i = 0; i < list.length; ++i) {
                var e = list[i];
                var ts = Number(e.ts);
                if (e.kind === "topic")
                    root.addTopic(e.id, e.title, e.body, e.author, e.domains || "", ts);
                else if (e.kind === "reply")
                    root.addReply(e.id, e.topicId, e.body, e.author, ts);
                if (e.state)
                    root.markDelivery(e.id, e.state);   // our own post: Live / Sending… / Failed
            }
            root.log("backlog restored: " + list.length + " post(s)");
        }, function (err) {
            root.log("backlog load failed: " + err);
            root.backlogLoaded = false;
        });
    }

    onReadyChanged: root.loadBacklog()

    Component.onCompleted: {
        log("Component.onCompleted — view created");
        root.ready = root.backend !== null && logos.isViewModuleReady("forumer");
        root.rebuildAccounts();
        root.loadBacklog();
    }

    // ── Models ────────────────────────────────────────────────────────────────
    ListModel { id: topicsModel }   // { tid, title, body, author, domains, ts, replies, placeholder, delivery }
    ListModel { id: repliesModel }  // { rid, topicId, body, author, ts, delivery }
    ListModel { id: threadModel }   // replies for selectedTopicId (display)
    ListModel { id: accountsModel } // { accountId, label }

    function rebuildAccounts() {
        accountsModel.clear();
        var list = [];
        try { list = JSON.parse(root.accountsJson); } catch (e) {
            root.log("could not parse accountsJson: " + e);
        }
        for (var i = 0; i < list.length; ++i)
            accountsModel.append({ accountId: list[i].id, label: list[i].label });
        root.syncAccountCombo();
    }

    function accountIndexOf(accountId) {
        for (var i = 0; i < accountsModel.count; ++i)
            if (accountsModel.get(i).accountId === accountId) return i;
        return -1;
    }

    function syncAccountCombo() {
        var i = root.accountIndexOf(root.selectedAccountId);
        if (unlockAccountCombo.currentIndex !== i)
            unlockAccountCombo.currentIndex = i;
    }

    onAccountsJsonChanged: root.rebuildAccounts()
    onSelectedAccountIdChanged: root.syncAccountCombo()

    // Timestamps are ns since the epoch (the author's clock).
    function formatTs(ts) {
        var d = ts ? new Date(Math.floor(ts / 1000000)) : new Date();
        return Qt.formatDateTime(d, "MM-dd hh:mm");
    }

    function findTopicIndex(tid) {
        for (var i = 0; i < topicsModel.count; ++i)
            if (topicsModel.get(i).tid === tid) return i;
        return -1;
    }

    function replyExists(rid) {
        for (var i = 0; i < repliesModel.count; ++i)
            if (repliesModel.get(i).rid === rid) return true;
        return false;
    }

    function countRepliesFor(tid) {
        var n = 0;
        for (var i = 0; i < repliesModel.count; ++i)
            if (repliesModel.get(i).topicId === tid) ++n;
        return n;
    }

    function topicMeta(t) {
        return t.ts + " · " + t.author + (t.domains ? " · #" + t.domains.split(",").join(" #") : "");
    }

    function addTopic(id, title, body, author, domains, ts) {
        var i = root.findTopicIndex(id);
        if (i >= 0) {
            if (topicsModel.get(i).placeholder)
                root.fillTopic(i, title, body, author, domains, ts);
            return;
        }
        topicsModel.append({
            tid: id, title: title, body: body, author: author || "", domains: domains || "",
            ts: root.formatTs(ts),
            replies: root.countRepliesFor(id),
            placeholder: false,
            delivery: ""
        });
    }

    // A reply can outrun its topic; show a placeholder until the topic arrives.
    function backfillTopic(topicId, ts) {
        topicsModel.append({
            tid: topicId, title: "⏳ " + topicId.substring(0, 8), body: "", author: "",
            domains: "", ts: root.formatTs(ts), replies: 0, placeholder: true, delivery: ""
        });
    }

    function fillTopic(i, title, body, author, domains, ts) {
        topicsModel.setProperty(i, "title", title);
        topicsModel.setProperty(i, "body", body);
        topicsModel.setProperty(i, "author", author || "");
        topicsModel.setProperty(i, "domains", domains || "");
        topicsModel.setProperty(i, "ts", root.formatTs(ts));
        topicsModel.setProperty(i, "placeholder", false);
        if (topicsModel.get(i).tid === root.selectedTopicId)
            root.openTopic(root.selectedTopicId);
    }

    function addReply(id, topicId, body, author, ts) {
        if (root.replyExists(id)) return;
        repliesModel.append({ rid: id, topicId: topicId, body: body, author: author || "",
                              ts: root.formatTs(ts), delivery: "" });
        var ti = root.findTopicIndex(topicId);
        if (ti < 0) {
            root.backfillTopic(topicId, ts);
            ti = root.findTopicIndex(topicId);
        }
        topicsModel.setProperty(ti, "replies", topicsModel.get(ti).replies + 1);
        if (topicId === root.selectedTopicId)
            threadModel.append({ rid: id, body: body, author: author || "",
                                 ts: root.formatTs(ts), delivery: "" });
    }

    function setMessageState(id, state, detail) {
        root.markDelivery(id, state);
        if (state === "failed")
            root.lastError = detail.length > 0 ? "Not delivered yet (will retry): " + detail
                                               : "Not delivered yet (will retry)";
    }

    function markDelivery(id, state) {
        var i = root.findTopicIndex(id);
        if (i >= 0) topicsModel.setProperty(i, "delivery", state);
        for (var j = 0; j < repliesModel.count; ++j)
            if (repliesModel.get(j).rid === id) { repliesModel.setProperty(j, "delivery", state); break; }
        for (var k = 0; k < threadModel.count; ++k)
            if (threadModel.get(k).rid === id) { threadModel.setProperty(k, "delivery", state); break; }
    }

    // Post status, as agreed: Sending… → Live, or Failed to publish.
    function deliveryMark(state) {
        if (state === "pending" || state === "propagated") return " · Sending…";
        if (state === "sent") return " · Live";
        if (state === "failed") return " · ⚠ Failed to publish";
        return "";
    }

    function openTopic(tid) {
        var i = root.findTopicIndex(tid);
        if (i < 0) return;
        var t = topicsModel.get(i);
        root.selectedTopicId = tid;
        root.selectedTitle = t.title;
        root.selectedBody = t.body;
        root.selectedMeta = t.placeholder ? "" : root.topicMeta(t);
        root.selectedIsPlaceholder = t.placeholder === true;
        threadModel.clear();
        for (var j = 0; j < repliesModel.count; ++j) {
            var r = repliesModel.get(j);
            if (r.topicId === tid)
                threadModel.append({ rid: r.rid, body: r.body, author: r.author, ts: r.ts, delivery: r.delivery });
        }
    }

    // ── Posting actions ───────────────────────────────────────────────────────
    // "" = the account's default disclosure; otherwise persona/alias/anonymous.
    readonly property var disclosureValues: ["", "persona", "alias", "anonymous"]
    function chosenDisclosure() { return root.disclosureValues[postAsCombo.currentIndex] || ""; }

    function disclosureLabel(d) {
        if (d === "alias") return "Alias" + (root.alias.length > 0 ? " (" + root.alias + ")" : "");
        if (d === "anonymous") return "Anonymous";
        return "Persona";
    }

    function createTopic() {
        if (!root.canPost || titleField.text.length === 0) return;
        root.lastError = "";
        logos.watch(backend.createTopic(titleField.text, bodyField.text, domainsField.text,
                                        root.chosenDisclosure()), function (err) {
            if (err) { root.lastError = err; root.log("createTopic error -> " + err); }
            else { titleField.text = ""; bodyField.text = ""; domainsField.text = ""; }
        }, function (e) { root.lastError = e; });
    }

    function sendReply() {
        if (!root.canPost || root.selectedTopicId.length === 0 || replyField.text.length === 0) return;
        root.lastError = "";
        logos.watch(backend.replyToTopic(root.selectedTopicId, replyField.text,
                                         root.chosenDisclosure()), function (err) {
            if (err) { root.lastError = err; root.log("reply error -> " + err); }
            else { replyField.text = ""; }
        }, function (e) { root.lastError = e; });
    }

    // ── Identity actions ──────────────────────────────────────────────────────
    function createIdentity() {
        root.identityError = "";
        if (createPasswordField.text !== createConfirmField.text) {
            root.identityError = "The passwords don't match";
            return;
        }
        logos.watch(backend.createIdentity(createNameField.text, createPasswordField.text), function (json) {
            var r = {};
            try { r = JSON.parse(json); } catch (e) { r = { error: "Unexpected reply: " + json }; }
            if (r.error) { root.identityError = r.error; return; }
            createPasswordField.text = ""; createConfirmField.text = ""; createNameField.text = "";
            root.pendingPhrase = r.phrase || "";
            if (root.pendingPhrase.length === 0)
                root.identityError = "Identity created, but the phrase didn't come through — use ⋯ → Show recovery phrase.";
        }, function (e) { root.identityError = e; });
    }

    function restoreIdentity() {
        root.identityError = "";
        if (restorePasswordField.text !== restoreConfirmField.text) {
            root.identityError = "The passwords don't match";
            return;
        }
        logos.watch(backend.restoreIdentity(restorePhraseField.text, restoreNameField.text,
                                            restorePasswordField.text), function (err) {
            if (err) { root.identityError = err; return; }
            restorePhraseField.text = ""; restorePasswordField.text = "";
            restoreConfirmField.text = ""; restoreNameField.text = "";
            root.onboardingMode = "create";
        }, function (e) { root.identityError = e; });
    }

    function unlockIdentity() {
        root.identityError = "";
        var i = unlockAccountCombo.currentIndex;
        if (i < 0 || i >= accountsModel.count) { root.identityError = "Choose an account"; return; }
        logos.watch(backend.unlockIdentity(accountsModel.get(i).accountId, unlockPasswordField.text), function (err) {
            if (err) { root.identityError = err; return; }
            unlockPasswordField.text = "";
        }, function (e) { root.identityError = e; });
    }

    function simpleCall(pending, onOk) {
        logos.watch(pending, function (err) {
            if (err) root.lastError = err;
            else if (onOk) onOk();
        }, function (e) { root.lastError = e; });
    }

    // LogosButton is pointer-only; this makes it a proper Tab stop with a focus
    // ring and Enter/Space activation.
    component FocusButton: LogosButton {
        id: btn
        activeFocusOnTab: true
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space) {
                btn.clicked();
                event.accepted = true;
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: btn.radius
            border.width: 2
            border.color: Theme.palette.overlayOrange
            visible: btn.activeFocus
        }
    }

    // A text field whose input is masked.
    component PasswordField: LogosTextField {
        Component.onCompleted: {
            textInput.activeFocusOnTab = true;
            textInput.echoMode = TextInput.Password;
        }
    }

    component ErrorLine: LogosText {
        property string message: ""
        Layout.fillWidth: true
        visible: message.length > 0
        text: "⚠ " + message
        color: Theme.palette.error
        font.pixelSize: Theme.typography.secondaryText
        wrapMode: Text.WordWrap
    }

    component Hint: LogosText {
        Layout.fillWidth: true
        color: Theme.palette.textSecondary
        font.pixelSize: Theme.typography.secondaryText
        wrapMode: Text.WordWrap
    }

    // A modal panel of our own. Used instead of LogosDialog, whose body slot
    // doesn't render here (title and actions do, contents don't).
    //   Sheet { title: "…"; <content items>; actions: [ FocusButton {…}, … ] }
    component Sheet: Rectangle {
        id: sheet
        property string title: ""
        default property alias content: sheetBody.data
        property alias actions: sheetActions.data
        signal opened()
        function open() { sheet.visible = true; sheet.opened(); }
        function close() { sheet.visible = false; }

        anchors.fill: parent
        visible: false
        z: 100
        color: "#99000000"

        // Swallow clicks so the forum underneath can't be used while open.
        MouseArea { anchors.fill: parent; hoverEnabled: true }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.spacing.large, 420)
            implicitHeight: sheetColumn.implicitHeight + 2 * Theme.spacing.large
            height: implicitHeight
            color: Theme.palette.backgroundSecondary
            border.color: Theme.palette.borderHairline
            border.width: 1
            radius: Theme.spacing.radiusMedium

            ColumnLayout {
                id: sheetColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacing.large
                spacing: Theme.spacing.medium

                LogosText {
                    text: sheet.title
                    color: Theme.palette.text
                    font.pixelSize: Theme.typography.subtitleText
                    font.weight: Theme.typography.weightBold
                }
                ColumnLayout {
                    id: sheetBody
                    Layout.fillWidth: true
                    spacing: Theme.spacing.small
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.small
                    Item { Layout.fillWidth: true }
                    RowLayout {
                        id: sheetActions
                        spacing: Theme.spacing.small
                    }
                }
            }
        }
    }

    // ── Identity menu + dialogs ───────────────────────────────────────────────
    LogosMenu {
        id: identityMenu
        LogosMenuItem { text: "Catch up now"; onTriggered: root.simpleCall(backend.catchUp()) }
        LogosMenuItem {
            text: "Retry unsent (" + root.unsentCount + ")"
            enabled: root.unsentCount > 0
            onTriggered: root.simpleCall(backend.retryUnsent())
        }
        LogosMenuItem { text: "Show recovery phrase…"; onTriggered: { revealPasswordField.text = ""; revealDialog.open(); } }
        LogosMenuItem { text: "Rename account…"; onTriggered: { renameField.text = root.myLabel; renameDialog.open(); } }
        LogosMenuItem { text: "Change password…"; onTriggered: passwordDialog.open() }
        LogosMenuItem { text: "Lock"; onTriggered: root.simpleCall(backend.lockIdentity()) }
        LogosMenuItem { text: "Remove from this device…"; onTriggered: { deletePasswordField.text = ""; deleteDialog.open(); } }
    }

    Sheet {
        id: revealDialog
        title: "Show recovery phrase"
        onOpened: root.dialogError = ""

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small
            Hint { text: "Enter your password to show the phrase." }
            PasswordField { id: revealPasswordField; Layout.fillWidth: true; placeholderText: "Password" }
            ErrorLine { message: root.dialogError }
        }
        actions: [
            FocusButton { text: "Cancel"; implicitWidth: 88; implicitHeight: 36; onClicked: revealDialog.close() },
            FocusButton {
                text: "Show"; implicitWidth: 88; implicitHeight: 36
                onClicked: logos.watch(backend.revealPhrase(revealPasswordField.text), function (json) {
                    var r = {};
                    try { r = JSON.parse(json); } catch (e) { r = { error: "Unexpected reply" }; }
                    if (r.error) { root.dialogError = r.error; return; }
                    revealPasswordField.text = "";
                    revealDialog.close();
                    root.pendingPhrase = r.phrase || "";
                }, function (e) { root.dialogError = e; })
            }
        ]
    }

    Sheet {
        id: renameDialog
        title: "Rename account"
        onOpened: root.dialogError = ""

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small
            Hint { text: "Only you see this name — it's never published." }
            LogosTextField {
                id: renameField; Layout.fillWidth: true; placeholderText: "Account name"
                Component.onCompleted: textInput.activeFocusOnTab = true
            }
            ErrorLine { message: root.dialogError }
        }
        actions: [
            FocusButton { text: "Cancel"; implicitWidth: 88; implicitHeight: 36; onClicked: renameDialog.close() },
            FocusButton {
                text: "Rename"; implicitWidth: 88; implicitHeight: 36
                enabled: renameField.text.length > 0
                onClicked: logos.watch(backend.renameAccount(renameField.text), function (err) {
                    if (err) root.dialogError = err; else renameDialog.close();
                }, function (e) { root.dialogError = e; })
            }
        ]
    }

    Sheet {
        id: passwordDialog
        title: "Change password"
        onOpened: { root.dialogError = ""; oldPasswordField.text = ""; newPasswordField.text = ""; newPasswordConfirm.text = ""; }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small
            PasswordField { id: oldPasswordField; Layout.fillWidth: true; placeholderText: "Current password" }
            PasswordField { id: newPasswordField; Layout.fillWidth: true; placeholderText: "New password (8+ characters)" }
            PasswordField { id: newPasswordConfirm; Layout.fillWidth: true; placeholderText: "Repeat new password" }
            ErrorLine { message: root.dialogError }
        }
        actions: [
            FocusButton { text: "Cancel"; implicitWidth: 88; implicitHeight: 36; onClicked: passwordDialog.close() },
            FocusButton {
                text: "Change"; implicitWidth: 88; implicitHeight: 36
                onClicked: {
                    if (newPasswordField.text !== newPasswordConfirm.text) { root.dialogError = "The new passwords don't match"; return; }
                    logos.watch(backend.changePassword(oldPasswordField.text, newPasswordField.text), function (err) {
                        if (err) root.dialogError = err; else passwordDialog.close();
                    }, function (e) { root.dialogError = e; });
                }
            }
        ]
    }

    Sheet {
        id: deleteDialog
        title: "Remove account from this device?"
        onOpened: root.dialogError = ""

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small
            LogosText {
                Layout.fillWidth: true
                text: "This deletes “" + root.myLabel + "” from this device. Without its recovery phrase it can never be recovered. Posts already published stay on the network."
                color: Theme.palette.warning
                font.pixelSize: Theme.typography.secondaryText
                wrapMode: Text.WordWrap
            }
            PasswordField { id: deletePasswordField; Layout.fillWidth: true; placeholderText: "Password to confirm" }
            ErrorLine { message: root.dialogError }
        }
        actions: [
            FocusButton { text: "Cancel"; implicitWidth: 88; implicitHeight: 36; onClicked: deleteDialog.close() },
            FocusButton {
                text: "Remove"; implicitWidth: 88; implicitHeight: 36
                onClicked: logos.watch(backend.deleteAccount(deletePasswordField.text), function (err) {
                    if (err) root.dialogError = err; else deleteDialog.close();
                }, function (e) { root.dialogError = e; })
            }
        ]
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.palette.background
    }

    // ── Forum (visible once unlocked) ─────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing.large
        spacing: Theme.spacing.small
        visible: root.unlocked && root.pendingPhrase.length === 0

        LogosText {
            text: "Forumer" + (root.appVersion.length > 0 ? " v" + root.appVersion : "")
            font.pixelSize: Theme.typography.panelTitleText
            font.weight: Theme.typography.weightBold
            color: Theme.palette.text
        }
        LogosText {
            text: "Forum topic: " + (root.topic.length > 0 ? root.topic : "—")
            color: Theme.palette.textSecondary
            font.pixelSize: Theme.typography.secondaryText
            font.family: root.monoFont
        }
        LogosText {
            text: (root.nodeReady ? "● " : "○ ") + (root.status.length > 0 ? root.status : "Connecting to backend…")
                  + (root.syncInfo.length > 0 ? " · " + root.syncInfo : "")
                  + (root.unsentCount > 0 ? " · " + root.unsentCount + " unsent" : "")
            color: root.nodeReady ? Theme.palette.success : Theme.palette.warning
            font.pixelSize: Theme.typography.secondaryText
        }

        // Identity bar: account, current persona, rotation, default disclosure.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small

            LogosText {
                text: root.myLabel + " · " + root.myPersona
                color: Theme.palette.text
                font.pixelSize: Theme.typography.secondaryText
                font.family: root.monoFont
            }
            LogosText {
                text: "Rotation"
                color: Theme.palette.textTertiary
                font.pixelSize: Theme.typography.secondaryText
            }
            LogosComboBox {
                id: rotationCombo
                Layout.preferredWidth: 150
                model: ["Keep", "Manual", "Auto (per post)"]
                readonly property var values: ["keep", "manual", "auto"]
                currentIndex: Math.max(0, values.indexOf(root.rotationPolicy))
                onActivated: function (index) {
                    root.simpleCall(backend.chooseRotationPolicy(rotationCombo.values[index]));
                }
            }
            FocusButton {
                text: "Rotate now"
                visible: root.rotationPolicy === "manual"
                implicitWidth: 110
                implicitHeight: 32
                onClicked: root.simpleCall(backend.rotatePersona())
            }
            LogosText {
                text: "Post as"
                color: Theme.palette.textTertiary
                font.pixelSize: Theme.typography.secondaryText
            }
            LogosComboBox {
                id: postAsCombo
                Layout.preferredWidth: 200
                model: ["Default (" + root.disclosureLabel(root.defaultDisclosure) + ")",
                        "Persona", root.disclosureLabel("alias"), "Anonymous"]
            }
            FocusButton {
                id: identityMenuButton
                text: "⋯"
                implicitWidth: 40
                implicitHeight: 32
                onClicked: identityMenu.popup(identityMenuButton, 0, identityMenuButton.height)
            }
            Item { Layout.fillWidth: true }
        }

        // Alias + default disclosure settings, inline for now (Settings screen later).
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.small
            LogosText {
                text: "Alias"
                color: Theme.palette.textTertiary
                font.pixelSize: Theme.typography.secondaryText
            }
            LogosTextField {
                id: aliasField
                Layout.preferredWidth: 180
                placeholderText: "e.g. night owl"
                text: root.alias
                Component.onCompleted: textInput.activeFocusOnTab = true
            }
            FocusButton {
                text: "Save alias"
                implicitWidth: 100
                implicitHeight: 32
                enabled: aliasField.text !== root.alias
                onClicked: root.simpleCall(backend.chooseAlias(aliasField.text))
            }
            LogosText {
                text: "Default"
                color: Theme.palette.textTertiary
                font.pixelSize: Theme.typography.secondaryText
            }
            LogosComboBox {
                id: defaultDisclosureCombo
                Layout.preferredWidth: 150
                model: ["Persona", "Alias", "Anonymous"]
                readonly property var values: ["persona", "alias", "anonymous"]
                currentIndex: Math.max(0, values.indexOf(root.defaultDisclosure))
                onActivated: function (index) {
                    root.simpleCall(backend.chooseDefaultDisclosure(defaultDisclosureCombo.values[index]));
                }
            }
            Item { Layout.fillWidth: true }
        }

        ErrorLine { message: root.lastError }

        // Two-pane forum: topics (left) | thread (right)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing.medium

            // ── Left: composer + topic list ──────────────────────────────────
            ColumnLayout {
                Layout.preferredWidth: 300
                Layout.minimumWidth: 240
                Layout.fillHeight: true
                spacing: Theme.spacing.small

                LogosText {
                    text: "Topics"
                    color: Theme.palette.text
                    font.pixelSize: Theme.typography.primaryText
                    font.weight: Theme.typography.weightBold
                }
                LogosTextField {
                    id: titleField
                    Layout.fillWidth: true
                    placeholderText: "New topic title…"
                    enabled: root.canPost
                    Component.onCompleted: textInput.activeFocusOnTab = true
                }
                LogosTextField {
                    id: bodyField
                    Layout.fillWidth: true
                    placeholderText: "Opening message (optional)…"
                    enabled: root.canPost
                    Component.onCompleted: textInput.activeFocusOnTab = true
                }
                LogosTextField {
                    id: domainsField
                    Layout.fillWidth: true
                    placeholderText: "Domains, comma-separated (e.g. privacy, campus)"
                    enabled: root.canPost
                    Component.onCompleted: textInput.activeFocusOnTab = true
                }
                Connections {
                    target: domainsField.textInput
                    function onAccepted() { root.createTopic() }
                }
                FocusButton {
                    text: "Create topic"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    implicitHeight: 40
                    enabled: root.canPost && titleField.text.length > 0
                    onClicked: root.createTopic()
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.palette.backgroundInset
                    border.color: Theme.palette.borderHairline
                    border.width: 1
                    radius: Theme.spacing.radiusMedium

                    ListView {
                        anchors.fill: parent
                        anchors.margins: Theme.spacing.tiny
                        clip: true
                        spacing: Theme.spacing.tiny
                        model: topicsModel

                        delegate: Rectangle {
                            width: ListView.view ? ListView.view.width : 0
                            implicitHeight: tcol.implicitHeight + Theme.spacing.medium
                            radius: Theme.spacing.radiusSmall
                            color: model.tid === root.selectedTopicId ? Theme.palette.overlayOrange : Theme.palette.backgroundSecondary
                            border.width: 1
                            border.color: model.tid === root.selectedTopicId ? Theme.palette.primary : Theme.palette.borderHairline

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openTopic(model.tid)
                            }

                            ColumnLayout {
                                id: tcol
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.spacing.small
                                spacing: 2

                                LogosText {
                                    Layout.fillWidth: true
                                    text: model.title
                                    color: model.placeholder ? Theme.palette.textSecondary : Theme.palette.text
                                    font.pixelSize: Theme.typography.primaryText
                                    font.weight: Theme.typography.weightBold
                                    font.italic: model.placeholder === true
                                    elide: Text.ElideRight
                                }
                                LogosText {
                                    Layout.fillWidth: true
                                    text: model.placeholder
                                          ? model.replies + (model.replies === 1 ? " reply · awaiting topic…" : " replies · awaiting topic…")
                                          : model.replies + (model.replies === 1 ? " reply · " : " replies · ")
                                              + root.topicMeta(model) + root.deliveryMark(model.delivery)
                                    color: Theme.palette.textTertiary
                                    font.pixelSize: Theme.typography.secondaryText
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            // ── Right: open thread + reply composer ──────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacing.small
                visible: root.selectedTopicId.length > 0

                TextEdit {
                    Layout.fillWidth: true
                    text: root.selectedTitle
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    color: Theme.palette.text
                    selectionColor: Theme.palette.primary
                    font.family: Theme.typography.publicSans
                    font.pixelSize: Theme.typography.subtitleText
                    font.weight: Theme.typography.weightBold
                    wrapMode: TextEdit.WordWrap
                }
                LogosText {
                    Layout.fillWidth: true
                    visible: root.selectedMeta.length > 0
                    text: root.selectedMeta
                    color: Theme.palette.textTertiary
                    font.pixelSize: Theme.typography.secondaryText
                    elide: Text.ElideRight
                }
                LogosText {
                    Layout.fillWidth: true
                    visible: root.selectedIsPlaceholder
                    text: "This topic hasn't arrived yet — its replies did. It will appear once it syncs."
                    color: Theme.palette.textSecondary
                    font.pixelSize: Theme.typography.secondaryText
                    wrapMode: Text.WordWrap
                }
                TextEdit {
                    Layout.fillWidth: true
                    visible: root.selectedBody.length > 0
                    text: root.selectedBody
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    color: Theme.palette.textSecondary
                    selectionColor: Theme.palette.primary
                    font.family: Theme.typography.publicSans
                    font.pixelSize: Theme.typography.primaryText
                    wrapMode: TextEdit.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.palette.backgroundInset
                    border.color: Theme.palette.borderHairline
                    border.width: 1
                    radius: Theme.spacing.radiusMedium

                    ListView {
                        anchors.fill: parent
                        anchors.margins: Theme.spacing.small
                        clip: true
                        spacing: Theme.spacing.small
                        model: threadModel

                        delegate: ColumnLayout {
                            width: ListView.view ? ListView.view.width : 0
                            spacing: 1
                            LogosText {
                                text: model.ts + " · " + model.author + root.deliveryMark(model.delivery)
                                color: Theme.palette.textTertiary
                                font.pixelSize: Theme.typography.secondaryText
                                font.family: root.monoFont
                            }
                            TextEdit {
                                Layout.fillWidth: true
                                text: model.body
                                readOnly: true
                                selectByMouse: true
                                textFormat: TextEdit.PlainText
                                color: Theme.palette.text
                                selectionColor: Theme.palette.primary
                                font.family: Theme.typography.publicSans
                                font.pixelSize: Theme.typography.primaryText
                                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.small
                    LogosTextField {
                        id: replyField
                        Layout.fillWidth: true
                        placeholderText: root.canPost ? "Write a reply…" : "Preparing local store…"
                        enabled: root.canPost
                        Component.onCompleted: textInput.activeFocusOnTab = true
                    }
                    Connections {
                        target: replyField.textInput
                        function onAccepted() { root.sendReply() }
                    }
                    FocusButton {
                        text: "Reply"
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 40
                        implicitWidth: 88
                        implicitHeight: 40
                        enabled: root.canPost && replyField.text.length > 0
                        onClicked: root.sendReply()
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.selectedTopicId.length === 0
                LogosText {
                    anchors.centerIn: parent
                    text: topicsModel.count > 0 ? "Select a topic to open it" : "No topics yet — create one"
                    color: Theme.palette.textTertiary
                    font.pixelSize: Theme.typography.primaryText
                }
            }
        }
    }

    // ── Onboarding / unlock (visible until unlocked) ──────────────────────────
    // "create" | "restore" — which card the first-run screen shows. When
    // accounts already exist, the unlock card is shown instead unless the user
    // asks to create or restore.
    property string onboardingMode: "create"
    property bool showOnboardingWhileLocked: false

    Item {
        anchors.fill: parent
        visible: !root.unlocked && root.pendingPhrase.length === 0

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.spacing.large, 460)
            spacing: Theme.spacing.small

            LogosText {
                text: "Forumer"
                font.pixelSize: Theme.typography.panelTitleText
                font.weight: Theme.typography.weightBold
                color: Theme.palette.text
            }
            Hint {
                text: "A private forum with no accounts on any server. Your identity lives only on this device, protected by your password."
            }

            // ── Unlock ──────────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.small
                visible: root.identityState === "locked" && !root.showOnboardingWhileLocked

                LogosText {
                    text: "Unlock"
                    font.pixelSize: Theme.typography.subtitleText
                    font.weight: Theme.typography.weightBold
                    color: Theme.palette.text
                }
                LogosComboBox {
                    id: unlockAccountCombo
                    Layout.fillWidth: true
                    model: accountsModel
                    textRole: "label"
                    visible: accountsModel.count > 1
                    onActivated: function (index) {
                        root.simpleCall(backend.selectAccount(accountsModel.get(index).accountId));
                    }
                }
                LogosText {
                    visible: accountsModel.count === 1
                    text: root.myLabel
                    color: Theme.palette.textSecondary
                    font.pixelSize: Theme.typography.primaryText
                }
                PasswordField { id: unlockPasswordField; Layout.fillWidth: true; placeholderText: "Password" }
                Connections {
                    target: unlockPasswordField.textInput
                    function onAccepted() { root.unlockIdentity() }
                }
                FocusButton {
                    text: "Unlock"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    enabled: unlockPasswordField.text.length > 0
                    onClicked: root.unlockIdentity()
                }
                ErrorLine { message: root.identityError }
                RowLayout {
                    spacing: Theme.spacing.small
                    FocusButton {
                        text: "Forgot password? Restore"
                        implicitWidth: 200; implicitHeight: 32
                        onClicked: { root.onboardingMode = "restore"; root.showOnboardingWhileLocked = true; root.identityError = ""; }
                    }
                    FocusButton {
                        text: "New account"
                        implicitWidth: 120; implicitHeight: 32
                        onClicked: { root.onboardingMode = "create"; root.showOnboardingWhileLocked = true; root.identityError = ""; }
                    }
                }
            }

            // ── Create / restore ────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.small
                visible: root.identityState === "none" || root.showOnboardingWhileLocked

                RowLayout {
                    spacing: Theme.spacing.small
                    FocusButton {
                        text: "Create identity"
                        implicitWidth: 150; implicitHeight: 32
                        onClicked: { root.onboardingMode = "create"; root.identityError = ""; }
                    }
                    FocusButton {
                        text: "Restore from phrase"
                        implicitWidth: 170; implicitHeight: 32
                        onClicked: { root.onboardingMode = "restore"; root.identityError = ""; }
                    }
                    FocusButton {
                        text: "Back"
                        visible: root.showOnboardingWhileLocked
                        implicitWidth: 72; implicitHeight: 32
                        onClicked: { root.showOnboardingWhileLocked = false; root.identityError = ""; }
                    }
                }

                // Create
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.small
                    visible: root.onboardingMode === "create"
                    Hint { text: "Creates a master identity on this device. Others only ever see the personas it generates - never the identity itself." }
                    LogosTextField {
                        id: createNameField; Layout.fillWidth: true
                        placeholderText: "Account name, only you see it (optional)"
                        Component.onCompleted: textInput.activeFocusOnTab = true
                    }
                    PasswordField { id: createPasswordField; Layout.fillWidth: true; placeholderText: "Password (8+ characters)" }
                    PasswordField { id: createConfirmField; Layout.fillWidth: true; placeholderText: "Repeat password" }
                    Hint { text: "There is no password reset. If you forget it, only your recovery phrase can restore this identity." }
                    FocusButton {
                        text: "Create identity"
                        Layout.fillWidth: true
                        implicitHeight: 40
                        enabled: createPasswordField.text.length > 0
                        onClicked: root.createIdentity()
                    }
                }

                // Restore
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.small
                    visible: root.onboardingMode === "restore"
                    Hint { text: "Enter your 24-word recovery phrase and choose a new password for this device." }
                    LogosTextField {
                        id: restorePhraseField; Layout.fillWidth: true
                        placeholderText: "24 words, separated by spaces"
                        Component.onCompleted: textInput.activeFocusOnTab = true
                    }
                    LogosTextField {
                        id: restoreNameField; Layout.fillWidth: true
                        placeholderText: "Account name (optional)"
                        Component.onCompleted: textInput.activeFocusOnTab = true
                    }
                    PasswordField { id: restorePasswordField; Layout.fillWidth: true; placeholderText: "New password (8+ characters)" }
                    PasswordField { id: restoreConfirmField; Layout.fillWidth: true; placeholderText: "Repeat password" }
                    FocusButton {
                        text: "Restore identity"
                        Layout.fillWidth: true
                        implicitHeight: 40
                        enabled: restorePhraseField.text.length > 0 && restorePasswordField.text.length > 0
                        onClicked: root.restoreIdentity()
                    }
                }

                ErrorLine { message: root.identityError }
            }
        }
    }

    // ── Recovery phrase step (after create, or "Show recovery phrase") ────────
    // A full screen rather than a popup: this is the one thing the user must
    // not skim past.
    Rectangle {
        anchors.fill: parent
        visible: root.pendingPhrase.length > 0
        color: Theme.palette.background

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.spacing.large, 620)
            spacing: Theme.spacing.medium

            LogosText {
                text: "Your recovery phrase"
                font.pixelSize: Theme.typography.panelTitleText
                font.weight: Theme.typography.weightBold
                color: Theme.palette.text
            }
            LogosText {
                Layout.fillWidth: true
                text: "Write these 24 words down, in order, and keep them somewhere safe and offline. "
                      + "They are the only way to recover this identity if you forget your password or lose this device. "
                      + "Anyone who sees them can post as you."
                color: Theme.palette.textSecondary
                font.pixelSize: Theme.typography.primaryText
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: wordGrid.implicitHeight + 2 * Theme.spacing.medium
                color: Theme.palette.backgroundInset
                border.color: Theme.palette.borderHairline
                border.width: 1
                radius: Theme.spacing.radiusMedium

                GridLayout {
                    id: wordGrid
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.medium
                    columns: 4
                    rowSpacing: Theme.spacing.small
                    columnSpacing: Theme.spacing.medium

                    Repeater {
                        model: root.pendingPhrase.length > 0 ? root.pendingPhrase.split(" ") : []
                        delegate: LogosText {
                            Layout.fillWidth: true
                            text: (index + 1) + ". " + modelData
                            color: Theme.palette.text
                            font.family: root.monoFont
                            font.pixelSize: Theme.typography.primaryText
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.small
                Item { Layout.fillWidth: true }
                FocusButton {
                    text: "I've written it down"
                    implicitWidth: 200
                    implicitHeight: 40
                    onClicked: root.pendingPhrase = ""
                }
            }
        }
    }

    // Leaving the locked screen's create/restore detour once unlocked.
    onUnlockedChanged: if (root.unlocked) root.showOnboardingWhileLocked = false
}
