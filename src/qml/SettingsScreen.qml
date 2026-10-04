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
