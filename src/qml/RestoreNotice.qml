import QtQuick
import QtQuick.Layouts

// Shown in the composers while a just-restored account's history is still
// arriving (store.restorePending): posting now could sign with a persona the
// user had rotated away from, so posting waits - unless they choose to go on.
//   RestoreNotice { Layout.fillWidth: true; store: composer.store }
ColumnLayout {
    id: notice

    required property var store

    visible: notice.store.restorePending
    spacing: 8

    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        text: "Your account was just restored and its posts are still arriving from the network. "
              + "Posting waits until they have, so you don't sign as a persona you had rotated away from. "
              + "This can take a few minutes (progress: missed → history)."
        wrapMode: Text.WordWrap
        font.family: Ui.sans
        font.pixelSize: Ui.size.small
        color: Ui.text2
    }
    FButton {
        text: "post anyway"
        compact: true
        tooltip: "Resume posting now. A persona you rotated away from may be used again."
        onClicked: notice.store.postAfterRestore()
    }
}
