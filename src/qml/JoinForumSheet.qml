import QtQuick
import QtQuick.Layouts

// Joining a private forum: from an invite link that was opened (Basecamp
// intent, or pasted into search) - store.pendingInvite - or pasted here.
// Never joins without asking: an invite can come from anyone.
Sheet {
    id: sheet

    required property var store
    property string error: ""
    property bool busy: false

    // The invite being looked at: { key, name } or null.
    readonly property var invite: {
        if (sheet.store.pendingInvite) return sheet.store.pendingInvite;
        var link = sheet.store.parseLink(linkField.text);
        return link && link.kind === "invite" ? { key: link.key, name: link.name } : null;
    }
    readonly property bool fromLink: sheet.store.pendingInvite !== null

    function join() {
        if (!sheet.invite || sheet.busy) return;
        var name = nameField.text.trim().length > 0 ? nameField.text.trim() : sheet.invite.name;
        sheet.error = "";
        sheet.busy = true;
        sheet.store.joinPrivateForum(sheet.invite.key, name, function () {
            sheet.busy = false;
            linkField.text = "";
            sheet.close();
        }, function (err) {
            sheet.busy = false;
            sheet.error = err;
        });
    }

    title: "join a private forum"
    panelWidth: 560

    onOpened: {
        sheet.error = "";
        nameField.text = sheet.invite ? sheet.invite.name : "";
        if (!sheet.fromLink) linkField.focusInput();
    }
    onInviteChanged: if (sheet.invite) nameField.text = sheet.invite.name
    onDismissed: sheet.store.pendingInvite = null
    onVisibleChanged: if (!visible) sheet.store.pendingInvite = null

    FField {
        id: linkField
        Layout.fillWidth: true
        visible: !sheet.fromLink
        label: "invite link"
        hint: text.trim().length === 0 ? "" : sheet.invite ? "an invite to “" + sheet.invite.name + "”" : "not an invite link"
        placeholder: "basecamp://intent/forumer.open?p=…"
        mono: true
        onAccepted: sheet.join()
    }

    FField {
        id: nameField
        Layout.fillWidth: true
        visible: sheet.invite !== null
        label: "name"
        hint: "as it shows in your sidebar - only on this device"
        placeholder: sheet.invite ? sheet.invite.name : ""
        onAccepted: sheet.join()
    }

    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        text: "Everyone with this link can read what is posted there and post themselves - including "
              + "anyone it was passed on to. Your posts there are signed like any other: as a persona, "
              + "an alias, or anonymously (members can recognise a persona you also use in public). The membership is kept with this account on this device; "
              + "leaving removes the forum's posts from it."
        wrapMode: Text.WordWrap
        font.family: Ui.sans
        font.pixelSize: Ui.size.small
        color: Ui.text2
    }

    Notice { Layout.fillWidth: true; message: sheet.error }

    actions: [
        FButton { text: "cancel"; onClicked: { sheet.close(); sheet.dismissed(); } },
        FButton {
            kind: "primary"
            text: sheet.busy ? "joining…" : "join"
            enabled: sheet.invite !== null && !sheet.busy
            onClicked: sheet.join()
        }
    ]
}
