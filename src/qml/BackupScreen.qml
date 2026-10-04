import QtQuick
import QtQuick.Layouts

// backup & restore: show the recovery phrase (behind the password), and
// restore or add another account from a phrase.
Page {
    id: backup

    required property var store
    signal restoreRequested()

    property string message: ""

    title: "backup & restore"
    subtitle: "your 24-word recovery phrase is the whole identity. with it you can restore every persona on a new device."

    Section {
        title: "recovery phrase"
        caption: "write it down, keep it offline"

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 12
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: "enter your password to show the phrase. anyone who sees it can post as you, so check nobody is looking."
                wrapMode: Text.WordWrap
                font.family: Ui.sans
                font.pixelSize: Ui.size.ui
                lineHeight: 1.35
                color: Ui.text2
            }
            RowLayout {
                spacing: 12
                FField {
                    id: pw
                    Layout.preferredWidth: 320
                    placeholder: "password"
                    password: true
                    onAccepted: reveal.clicked()
                }
                FButton {
                    id: reveal
                    implicitHeight: 44
                    text: "show phrase"
                    icon: "key"
                    enabled: pw.text.length > 0
                    onClicked: backup.store.callJson(backup.store.backend.revealPhrase(pw.text), function (r) {
                        pw.text = "";
                        backup.message = "";
                        backup.store.pendingPhrase = r.phrase || "";
                    }, function (e) { backup.message = e; })
                }
            }
            Notice { Layout.fillWidth: true; message: backup.message }
        }
    }

    Section {
        title: "restore"
        caption: "from a recovery phrase"
        note: "restoring adds the account to this device under a new password. it also works when you've forgotten your password: lock, then choose “forgot password? restore”."

        RowLayout {
            Layout.topMargin: 16
            FButton { text: "restore an account…"; onClicked: backup.restoreRequested() }
        }
    }
}
