import QtQuick
import QtQuick.Layouts

// settings: this account on this device (name, password, lock, remove) and
// the network it is connected to.
Page {
    id: settings

    required property var store
    signal lockRequested()

    property string nameMessage: ""
    property string passwordMessage: ""
    property bool passwordOk: false
    property string removeMessage: ""
    property bool confirmRemove: false

    title: "settings"
    subtitle: "this account, on this device."

    Section {
        title: "account name"
        caption: "only you see it - it's never published"

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            FField {
                id: nameField
                Layout.preferredWidth: 320
                text: settings.store.myLabel
            }
            FButton {
                Layout.alignment: Qt.AlignBottom
                implicitHeight: 44
                text: "rename"
                enabled: nameField.text.trim().length > 0 && nameField.text !== settings.store.myLabel
                onClicked: settings.store.call(settings.store.backend.renameAccount(nameField.text),
                                               function () { settings.nameMessage = ""; },
                                               function (e) { settings.nameMessage = e; })
            }
            Item { Layout.fillWidth: true }
        }
        Notice { Layout.fillWidth: true; Layout.topMargin: 8; message: settings.nameMessage }
    }

    Section {
        title: "password"
        caption: "unlocks this account on this device"

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            FField { id: oldPw; Layout.preferredWidth: 360; label: "current password"; password: true }
            FField { id: newPw; Layout.preferredWidth: 360; label: "new password"; hint: "8+ characters"; password: true }
            FField { id: newPw2; Layout.preferredWidth: 360; label: "repeat new password"; password: true }
            FButton {
                text: "change password"
                enabled: oldPw.text.length > 0 && newPw.text.length > 0
                onClicked: {
                    settings.passwordOk = false;
                    if (newPw.text !== newPw2.text) { settings.passwordMessage = "the new passwords don't match"; return; }
                    settings.store.call(settings.store.backend.changePassword(oldPw.text, newPw.text), function () {
                        oldPw.text = ""; newPw.text = ""; newPw2.text = "";
                        settings.passwordOk = true;
                        settings.passwordMessage = "password changed";
                    }, function (e) { settings.passwordMessage = e; });
                }
            }
            Notice { Layout.fillWidth: true; error: !settings.passwordOk; message: settings.passwordMessage }
        }
    }

    Section {
        title: "lock"
        caption: "forget the key until you enter your password again"
        RowLayout {
            Layout.topMargin: 20
            FButton { text: "lock now"; icon: "lock"; onClicked: settings.lockRequested() }
        }
    }

    Section {
        title: "images"
        caption: "pictures in posts travel through Logos Storage"
        visible: settings.store.canAttachImages

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            Segmented {
                Layout.preferredWidth: 360
                options: [
                    { value: "on", label: "help keep them available" },
                    { value: "off", label: "just view" }
                ]
                value: settings.store.mediaRehost ? "on" : "off"
                onPicked: (v) => settings.store.chooseMediaRehost(v === "on")
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: settings.store.mediaRehost
                      ? "Images you open stay in this device's storage node and are offered to others, so a picture stays reachable while its author is offline. Others fetching it can see your node's address."
                      : "Images you open are fetched for you only and not offered to others."
                wrapMode: Text.WordWrap
                font.family: Ui.sans
                font.pixelSize: Ui.size.small
                color: Ui.text2
            }
            Segmented {
                Layout.preferredWidth: 360
                Layout.topMargin: 8
                options: [
                    { value: "on", label: "load full images" },
                    { value: "off", label: "previews only" }
                ]
                value: settings.store.mediaAutoFull ? "on" : "off"
                onPicked: (v) => settings.store.chooseMediaAutoFull(v === "on")
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: settings.store.mediaAutoFull
                      ? "A thread shows each picture's preview at once (it travels with the post) and fetches the full image from Storage in the background. Fetching connects to a storage node that holds it."
                      : "A thread shows the previews that travel with posts; a full image is fetched only when you click it."
                wrapMode: Text.WordWrap
                font.family: Ui.sans
                font.pixelSize: Ui.size.small
                color: Ui.text2
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: "storage node: " + settings.store.storageState
                      + (settings.store.storageState === "off" ? " (starts shortly after you unlock)" : "")
                      + (settings.store.media.detail ? " · " + settings.store.media.detail : "")
                font.family: Ui.mono
                font.pixelSize: Ui.size.caption
                color: Ui.text3
            }
        }
    }

    Section {
        title: "network"
        caption: settings.store.status

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            columns: 2
            columnSpacing: 24
            rowSpacing: 10
            Repeater {
                model: [
                    ["delivery network", settings.store.network.length > 0 ? settings.store.network : "-"],
                    ["forum topic", settings.store.topic],
                    ["sync", settings.store.syncInfo.length > 0 ? settings.store.syncInfo : "-"],
                    ["unsent", String(settings.store.unsentCount)],
                    ["version", settings.store.appVersion]
                ]
                delegate: Item {
                    required property var modelData
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    implicitHeight: 20
                    Text {
                        textFormat: Text.PlainText
                        width: 140
                        text: parent.modelData[0]
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.ui
                        color: Ui.text2
                    }
                    Text {
                        textFormat: Text.PlainText
                        x: 164
                        width: parent.width - x
                        text: parent.modelData[1]
                        elide: Text.ElideRight
                        font.family: Ui.mono
                        font.pixelSize: Ui.size.small
                        color: Ui.text
                    }
                }
            }
        }
    }

    Section {
        title: "choose network"
        caption: settings.store.networkNext !== settings.store.network && settings.store.networkNext.length > 0
                 ? "restart forumer to switch to " + settings.store.networkNext : ""
        note: "logos.dev works for everyone. logos.test is the official testnet, but it runs RLN rate limiting: a node starts there only with an active RLN membership registered on the LEZ testnet. the two are separate networks - you only see people on the same one."

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            spacing: 10
            Segmented {
                Layout.preferredWidth: 420
                options: [
                    { value: "logos.dev", label: "logos.dev · open" },
                    { value: "logos.test", label: "logos.test · RLN" }
                ]
                value: settings.store.networkNext.length > 0 ? settings.store.networkNext : settings.store.network
                onPicked: (v) => settings.store.call(settings.store.backend.chooseNetwork(v))
            }
        }
    }

    // A developer tool (review F17): it shares this machine's addresses.
    StorageProbe {
        visible: settings.store.devMode
        store: settings.store
    }

    Section {
    title: "muted personas"
    caption: "posts from these personas are hidden on this device. they are not blocked on the network."

    Column {
        Layout.fillWidth: true
        Layout.topMargin: 20
        spacing: 10
        Repeater {
            model: settings.store.mutedList()
            delegate: RowLayout {
                required property string modelData
                width: parent ? parent.width : 0
                spacing: 12
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: modelData
                    font.family: Ui.mono
                    font.pixelSize: Ui.size.reading
                    color: Ui.text
                }
                FButton {
                    implicitHeight: 36
                    text: "unmute"
                    onClicked: settings.store.setMuted(modelData, false)
                }
            }
        }
        Text {
            textFormat: Text.PlainText
            visible: settings.store.mutedList().length === 0
            text: "no one is muted."
            font.family: Ui.sans
            font.pixelSize: Ui.size.caption
            color: Ui.text3
        }
    }
}

    Section {
        title: "remove from this device"
        caption: "irreversible without the recovery phrase"

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: "deletes “" + settings.store.myLabel + "” from this device. posts already published stay on the network. make sure you have the recovery phrase first (backup & restore)."
                wrapMode: Text.WordWrap
                font.family: Ui.sans
                font.pixelSize: Ui.size.ui
                lineHeight: 1.35
                color: Ui.text2
            }
            FButton {
                visible: !settings.confirmRemove
                text: "remove account…"
                onClicked: { settings.confirmRemove = true; settings.removeMessage = ""; }
            }
            RowLayout {
                visible: settings.confirmRemove
                spacing: 12
                FField { id: removePw; Layout.preferredWidth: 300; placeholder: "password to confirm"; password: true }
                FButton { implicitHeight: 44; text: "cancel"; onClicked: { settings.confirmRemove = false; removePw.text = ""; } }
                FButton {
                    implicitHeight: 44
                    kind: "primary"
                    text: "remove"
                    enabled: removePw.text.length > 0
                    onClicked: settings.store.call(settings.store.backend.deleteAccount(removePw.text),
                                                   function () { settings.confirmRemove = false; removePw.text = ""; },
                                                   function (e) { settings.removeMessage = e; })
                }
            }
            Notice { Layout.fillWidth: true; message: settings.removeMessage }
        }
    }
}
