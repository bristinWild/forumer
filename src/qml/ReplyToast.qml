import QtQuick
import QtQuick.Layouts

// "night owl replied to you" — a quiet card in the corner when a reply to you
// arrives while the app is open. Click to open the thread; it goes away by
// itself after a few seconds.
Rectangle {
    id: toast

    required property var store
    readonly property var note: store.toast

    visible: note !== null
    width: 340
    height: col.implicitHeight + 28
    radius: Ui.radius.l
    color: Ui.surface
    border.width: 1
    border.color: Ui.borderStrong

    Timer {
        id: hide
        interval: 8000
        onTriggered: toast.store.toast = null
    }
    onNoteChanged: if (note !== null) hide.restart()

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: hide.stop()
        onExited: hide.restart()
        onClicked: {
            var topicId = toast.note.topicId;
            toast.store.toast = null;
            toast.store.openTopic(topicId);
        }
    }

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: close.left
        anchors.leftMargin: 16
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Row {
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 7; height: 7; radius: 3.5
                color: Ui.unread
            }
            Text {
                textFormat: Text.PlainText
                text: toast.note ? toast.note.text : ""
                font.family: Ui.sans
                font.pixelSize: Ui.size.ui
                font.weight: Ui.weight.semibold
                color: Ui.text
            }
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: toast.note ? toast.note.body : ""
            elide: Text.ElideRight
            font.family: Ui.sans
            font.pixelSize: Ui.size.small
            color: Ui.text2
        }
    }
    FButton {
        id: close
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        kind: "ghost"
        icon: "x"
        implicitHeight: 28
        tooltip: "Dismiss"
        onClicked: toast.store.toast = null
    }
}
