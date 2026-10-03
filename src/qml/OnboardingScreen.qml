import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// Before the forum: unlock an account on this device, create a new identity,
// or restore one from its recovery phrase.
Rectangle {
    id: onboarding

    required property var store
    // "unlock" | "create" | "restore"
    property string mode: store.identityState === "none" ? "create" : "unlock"
    property bool canGoBack: false          // shown over an unlocked app (restore from backup)
    property string error: ""
    property bool busy: false
    signal back()

    function reset(m) { onboarding.mode = m; onboarding.error = ""; }

    color: Ui.bg

    readonly property var accounts: store.accounts
    readonly property string selectedLabel: {
        for (var i = 0; i < accounts.length; ++i)
            if (accounts[i].id === store.selectedAccountId) return accounts[i].label;
        return accounts.length > 0 ? accounts[0].label : "";
    }

    function unlock() {
        if (pw.text.length === 0 || onboarding.busy) return;
        onboarding.error = "";
        onboarding.busy = true;
        var id = onboarding.store.selectedAccountId.length > 0 ? onboarding.store.selectedAccountId
               : (onboarding.accounts.length > 0 ? onboarding.accounts[0].id : "");
        onboarding.store.call(onboarding.store.backend.unlockIdentity(id, pw.text),
                              function () { onboarding.busy = false; pw.text = ""; },
                              function (e) { onboarding.busy = false; onboarding.error = e; });
    }

    function create() {
        onboarding.error = "";
        if (createPw.text !== createPw2.text) { onboarding.error = "the passwords don't match"; return; }
        onboarding.busy = true;
        onboarding.store.callJson(onboarding.store.backend.createIdentity(createName.text, createPw.text), function (r) {
            onboarding.busy = false;
            createName.text = ""; createPw.text = ""; createPw2.text = "";
            onboarding.store.pendingPhrase = r.phrase || "";
        }, function (e) { onboarding.busy = false; onboarding.error = e; });
    }

    function restore() {
        onboarding.error = "";
        if (restorePw.text !== restorePw2.text) { onboarding.error = "the passwords don't match"; return; }
        onboarding.busy = true;
        onboarding.store.call(onboarding.store.backend.restoreIdentity(phrase.text, restoreName.text, restorePw.text), function () {
            onboarding.busy = false;
            phrase.text = ""; restoreName.text = ""; restorePw.text = ""; restorePw2.text = "";
            onboarding.back();
        }, function (e) { onboarding.busy = false; onboarding.error = e; });
    }

    MouseArea { anchors.fill: parent }   // nothing behind is reachable

    B.ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: card.implicitHeight + 160
        clip: true

        Item {
            width: scroller.availableWidth
            height: Math.max(scroller.height, card.implicitHeight + 160)

            ColumnLayout {
                id: card
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 440)
                spacing: 28

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text {
                        text: "forumer"
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.page
                        font.weight: Ui.weight.bold
                        font.letterSpacing: -0.9
                        color: Ui.text
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "a private forum with no accounts on any server. your identity lives only on this device, locked with your password."
                        wrapMode: Text.WordWrap
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.reading
                        lineHeight: 1.35
                        color: Ui.text2
                    }
                }

                // ── Unlock ────────────────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: onboarding.mode === "unlock"
                    spacing: 16

                    Text {
                        text: "unlock"
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.section
                        font.weight: Ui.weight.semibold
                        color: Ui.text
                    }
                    // Accounts on this device (pick one when there are several).
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: onboarding.accounts.length > 1
                        Repeater {
                            model: onboarding.accounts
                            delegate: Chip {
                                required property var modelData
                                mono: false
                                text: modelData.label
                                selected: modelData.id === onboarding.store.selectedAccountId
                                onClicked: onboarding.store.call(onboarding.store.backend.selectAccount(modelData.id))
                            }
                        }
                    }
                    Text {
                        visible: onboarding.accounts.length === 1
                        text: onboarding.selectedLabel
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.reading
                        color: Ui.text2
                    }
                    FField {
                        id: pw
                        Layout.fillWidth: true
                        placeholder: "password"
                        password: true
                        onAccepted: onboarding.unlock()
                        Component.onCompleted: if (onboarding.mode === "unlock") focusInput()
                    }
                    FButton {
                        Layout.fillWidth: true
                        implicitHeight: 44
                        kind: "primary"
                        text: onboarding.busy ? "unlocking…" : "unlock"
                        enabled: pw.text.length > 0 && !onboarding.busy
                        onClicked: onboarding.unlock()
                    }
                    Notice { Layout.fillWidth: true; message: onboarding.error }
                    Row {
                        spacing: 16
                        FButton { compact: true; kind: "ghost"; text: "forgot password? restore"; onClicked: onboarding.reset("restore") }
                        FButton { compact: true; kind: "ghost"; text: "new account"; onClicked: onboarding.reset("create") }
                    }
                }

                // ── Create ────────────────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: onboarding.mode === "create"
                    spacing: 16

                    Text {
                        text: "create an identity"
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.section
                        font.weight: Ui.weight.semibold
                        color: Ui.text
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "others only ever see the personas it makes — never the identity itself."
                        wrapMode: Text.WordWrap
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.ui
                        color: Ui.text2
                    }
                    FField { id: createName; Layout.fillWidth: true; label: "account name"; hint: "only you see it"; placeholder: "e.g. seminar" }
                    FField { id: createPw; Layout.fillWidth: true; label: "password"; hint: "8+ characters"; password: true }
                    FField { id: createPw2; Layout.fillWidth: true; label: "repeat password"; password: true; onAccepted: onboarding.create() }
                    Text {
                        Layout.fillWidth: true
                        text: "there is no password reset. if you forget it, only your recovery phrase can bring this identity back."
                        wrapMode: Text.WordWrap
                        font.family: Ui.mono
                        font.pixelSize: Ui.size.caption
                        color: Ui.text3
                    }
                    FButton {
                        Layout.fillWidth: true
                        implicitHeight: 44
                        kind: "primary"
                        text: onboarding.busy ? "creating…" : "create identity"
                        enabled: createPw.text.length > 0 && !onboarding.busy
                        onClicked: onboarding.create()
                    }
                    Notice { Layout.fillWidth: true; message: onboarding.error }
                    Row {
                        spacing: 16
                        FButton { compact: true; kind: "ghost"; text: "restore from a phrase instead"; onClicked: onboarding.reset("restore") }
                        FButton {
                            compact: true; kind: "ghost"; text: "back to unlock"
                            visible: onboarding.store.identityState !== "none" && !onboarding.canGoBack
                            onClicked: onboarding.reset("unlock")
                        }
                    }
                }

                // ── Restore ───────────────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: onboarding.mode === "restore"
                    spacing: 16

                    Text {
                        text: "restore from recovery phrase"
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.section
                        font.weight: Ui.weight.semibold
                        color: Ui.text
                    }
                    FArea {
                        id: phrase
                        Layout.fillWidth: true
                        label: "24 words, in order"
                        placeholder: "word1 word2 word3 …"
                        minHeight: 96
                    }
                    FField { id: restoreName; Layout.fillWidth: true; label: "account name"; hint: "optional" }
                    FField { id: restorePw; Layout.fillWidth: true; label: "new password"; hint: "8+ characters"; password: true }
                    FField { id: restorePw2; Layout.fillWidth: true; label: "repeat password"; password: true; onAccepted: onboarding.restore() }
                    FButton {
                        Layout.fillWidth: true
                        implicitHeight: 44
                        kind: "primary"
                        text: onboarding.busy ? "restoring…" : "restore identity"
                        enabled: phrase.text.trim().length > 0 && restorePw.text.length > 0 && !onboarding.busy
                        onClicked: onboarding.restore()
                    }
                    Notice { Layout.fillWidth: true; message: onboarding.error }
                    Row {
                        spacing: 16
                        FButton {
                            compact: true; kind: "ghost"; text: "create a new identity instead"
                            visible: !onboarding.canGoBack
                            onClicked: onboarding.reset("create")
                        }
                        FButton {
                            compact: true; kind: "ghost"; text: "back to unlock"
                            visible: onboarding.store.identityState === "locked"
                            onClicked: onboarding.reset("unlock")
                        }
                        FButton {
                            compact: true; kind: "ghost"; text: "back"
                            visible: onboarding.canGoBack
                            onClicked: onboarding.back()
                        }
                    }
                }
            }
        }
    }
}
