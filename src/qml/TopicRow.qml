import QtQuick

// One topic in a feed: title, a line of its opening message, and mono
// metadata (author · replies · age · #domains · delivery state).
Item {
    id: row

    property var item: ({})       // ForumStore.topicItem()
    signal clicked()

    implicitHeight: col.implicitHeight + 36

    Rectangle {   // hover wash, slightly wider than the text column
        anchors.fill: parent
        anchors.leftMargin: -12
        anchors.rightMargin: -12
        radius: Ui.radius.l
        color: mouse.containsMouse || row.activeFocus ? Ui.hover : "transparent"
    }

    Column {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Text {
            textFormat: Text.PlainText
            width: parent.width
            text: row.item.title || ""
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.family: Ui.sans
            font.pixelSize: Ui.size.title
            font.weight: Ui.weight.medium
            font.italic: row.item.placeholder === true
            color: row.item.placeholder ? Ui.text2 : Ui.text
            lineHeight: 1.15
        }
        Text {
            textFormat: Text.PlainText
            width: parent.width
            visible: text.length > 0
            text: row.item.excerpt || ""
            elide: Text.ElideRight
            maximumLineCount: 1
            font.family: Ui.sans
            font.pixelSize: Ui.size.ui
            color: Ui.text2
        }
        Row {
            width: parent.width
            spacing: 10
            Text {
                textFormat: Text.PlainText
                text: row.item.meta || ""
                elide: Text.ElideRight
                width: Math.min(implicitWidth, parent.width - (stateTag.visible ? stateTag.width + 10 : 0))
                font.family: Ui.mono
                font.pixelSize: Ui.size.caption
                color: Ui.text3
            }
            StateTag {
                id: stateTag
                delivery: row.item.delivery || ""
            }
        }
    }

    Rectangle {   // divider
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Ui.borderSoft
    }

    activeFocusOnTab: true
    Accessible.role: Accessible.Link
    Accessible.name: row.item.title || ""

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            row.clicked();
            event.accepted = true;
        }
    }
}
