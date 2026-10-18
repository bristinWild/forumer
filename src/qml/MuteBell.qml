import QtQuick

// A small bell toggle that mutes or unmutes one persona on this device.
// Shows "mute" (quiet outline) or "muted" (pink outline and bell).
// Hidden for anonymous posts and placeholders, where muting makes no sense.
//   MuteBell { store: thread.store; item: reply.item }
Item {
    id: bell

    property var store: null
    property var item: null

    readonly property bool shown: store !== null && item !== null && item !== undefined
                                  && item.placeholder !== true && store.canMute(item)
    readonly property bool muted: shown && store.isMuted(item.authorKey)

    visible: shown
    implicitWidth: row.implicitWidth + 20
    implicitHeight: 24

    Accessible.role: Accessible.Button
    Accessible.name: muted ? "unmute this persona" : "mute this persona"

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: height / 2
        color: bell.muted ? Qt.rgba(Ui.unread.r, Ui.unread.g, Ui.unread.b, Ui.dark ? 0.12 : 0.08)
                          : (mouse.containsMouse ? Ui.hover : "transparent")
        border.width: 1
        border.color: bell.muted ? Ui.unread : Ui.borderStrong
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "bell"
            size: 13
            strokeWidth: 2
            color: label.color
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: bell.muted ? "muted" : "mute"
            font.family: Ui.sans
            font.pixelSize: Ui.size.small
            color: bell.muted ? Ui.unread
                 : (mouse.containsMouse ? Ui.text : Ui.text2)
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: bell.store.setMuted(bell.item.authorKey, !bell.muted)
    }
}
