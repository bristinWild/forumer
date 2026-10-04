import QtQuick
import QtQuick.Layouts

// A post in a list outside the feed (outbox, my posts, missed): title, an
// optional line of text, mono metadata or a delivery state, an optional
// action on the right. Clicking opens its thread.
Item {
    id: line

    property string title: ""
    property string body: ""
    property string meta: ""
    property string delivery: ""
    property string actionText: ""
    signal clicked()
    signal action()

    implicitHeight: row.implicitHeight + 36

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

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: line.title
                elide: Text.ElideRight
                font.family: Ui.sans
                font.pixelSize: Ui.size.reading
                font.weight: Ui.weight.medium
                color: Ui.text
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: line.body.length > 0
                text: line.body.replace(/\s+/g, " ")
                elide: Text.ElideRight
                font.family: Ui.sans
                font.pixelSize: Ui.size.ui
                color: Ui.text2
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: line.meta.length > 0
                text: line.meta
                elide: Text.ElideRight
                font.family: Ui.mono
                font.pixelSize: Ui.size.caption
                color: Ui.text3
            }
            StateTag { delivery: line.delivery }
        }
        FButton {
            visible: line.actionText.length > 0
            kind: "primary"
            implicitHeight: 36
            text: line.actionText
            onClicked: line.action()
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Ui.borderSoft
    }
}
