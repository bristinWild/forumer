import QtQuick
import QtQuick.Layouts

// "new private forum": a name, then the invite link to share.
//
// A private forum is its key (forumer_core/forums.h): the link carries it,
// so whoever has the link can read and post, and nothing can take that back.
Sheet {
    id: sheet

    required property var store
    property string error: ""
    property bool busy: false
    property string invite: ""      // set once created
    property bool copied: false

    function create() {
        var name = nameField.text.trim();
        if (name.length === 0 || sheet.busy) return;
        sheet.error = "";
        sheet.busy = true;
        sheet.store.createPrivateForum(name, function (r) {
            sheet.busy = false;
            sheet.invite = r.invite;
        }, function (err) {
            sheet.busy = false;
            sheet.error = err;
        });
    }

    function copyInvite() {
        clipboard.text = sheet.invite;
        clipboard.selectAll();
        clipboard.copy();
        clipboard.text = "";
        sheet.copied = true;
        copiedTimer.restart();
    }

    title: sheet.invite.length > 0 ? "share the invite link" : "new private forum"
    panelWidth: 560

    onOpened: {
        sheet.error = "";
        sheet.invite = "";
        sheet.copied = false;
        nameField.text = "";
        nameField.focusInput();
    }

    FField {
        id: nameField
        Layout.fillWidth: true
        visible: sheet.invite.length === 0
        label: "name"
        hint: text.length > 0 ? sheet.store.sizeNote(text, 64) : "shown to everyone you invite"
        placeholder: "Campus council"
        onAccepted: sheet.create()
    }

    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: sheet.invite.length === 0
        text: "Only people with its invite link can read it: on the network everything in it is "
              + "encrypted with a key that only the link carries. Anyone who has the link can read and "
              + "post, and a link can't be taken back - if it reaches the wrong people, make a new forum."
        wrapMode: Text.WordWrap
        font.family: Ui.sans
        font.pixelSize: Ui.size.small
        color: Ui.text2
    }

    // ── Created ──────────────────────────────────────────────────────────────
    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: sheet.invite.length > 0
        text: "Send this link to the people you want in " + sheet.store.forumName(sheet.store.currentForum)
              + ", over a channel you trust. You can copy it again any time from the forum's page."
        wrapMode: Text.WordWrap
        font.family: Ui.sans
        font.pixelSize: Ui.size.small
        color: Ui.text2
    }
    Rectangle {
        Layout.fillWidth: true
        visible: sheet.invite.length > 0
        implicitHeight: linkText.implicitHeight + 24
        radius: Ui.radius.m
        color: Ui.field
        border.width: 1
        border.color: Ui.borderStrong
        Text {
            id: linkText
            textFormat: Text.PlainText
            anchors.fill: parent
            anchors.margins: 12
            text: sheet.invite
            wrapMode: Text.WrapAnywhere
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            color: Ui.text
        }
    }

    Notice { Layout.fillWidth: true; message: sheet.error }

    actions: [
        FButton {
            visible: sheet.invite.length === 0
            text: "cancel"
            onClicked: sheet.close()
        },
        FButton {
            visible: sheet.invite.length === 0
            kind: "primary"
            text: sheet.busy ? "creating…" : "create"
            enabled: nameField.text.trim().length > 0 && !sheet.busy
                     && sheet.store.utf8Bytes(nameField.text.trim()) <= 64
            onClicked: sheet.create()
        },
        FButton {
            visible: sheet.invite.length > 0
            icon: sheet.copied ? "check" : "link"
            text: sheet.copied ? "copied" : "copy invite link"
            onClicked: sheet.copyInvite()
        },
        FButton {
            visible: sheet.invite.length > 0
            kind: "primary"
            text: "done"
            onClicked: sheet.close()
        }
    ]

    Timer { id: copiedTimer; interval: 1500; onTriggered: sheet.copied = false }
    // Off-screen helper for copying (QML has no clipboard API of its own).
    TextEdit { id: clipboard; visible: false; textFormat: TextEdit.PlainText }
}
