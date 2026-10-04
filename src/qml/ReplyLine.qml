import QtQuick
import QtQuick.Layouts

// One row of the replies screen: who answered, where, what they said. Unread
// rows carry a dot and brighter text.
Item {
    id: line

    property var item: ({})     // ForumStore.inbox entry
    signal clicked()

    readonly property bool unread: item.read === false

    implicitHeight: col.implicitHeight + 36

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -12
        anchors.rightMargin: -12
        radius: Ui.radius.l
        color: mouse.containsMouse ? Ui.hover : "transparent"
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: line.clicked()
    }

    Rectangle {   // unread dot
        visible: line.unread
        x: -2
        y: 24
        width: 7
        height: 7
        radius: 3.5
        color: Ui.unread
    }

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: (line.item.author || "") + (line.item.direct ? " replied to you"
                                              : line.item.inMyTopic ? " replied in your topic"
                                              : " replied in a thread you joined")
            elide: Text.ElideRight
            font.family: Ui.sans
            font.pixelSize: Ui.size.reading
            font.weight: line.unread ? Ui.weight.semibold : Ui.weight.medium
            color: Ui.text
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: (line.item.body || "").replace(/\s+/g, " ")
            elide: Text.ElideRight
            maximumLineCount: 2
            wrapMode: Text.WordWrap
            font.family: Ui.sans
            font.pixelSize: Ui.size.ui
            color: line.unread ? Ui.textBody : Ui.text2
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: "in “" + (line.item.title || "") + "” · " + (line.item.time || "")
            elide: Text.ElideRight
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            color: Ui.text3
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Ui.borderSoft
    }
}
