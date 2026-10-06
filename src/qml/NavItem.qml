import QtQuick

// One row of the sidebar: icon, label, optional badge. The active row sits on
// a rounded block; the others are quiet grey text.
Rectangle {
    id: item

    property string text: ""
    property string icon: ""
    property string badge: ""
    property string badgeTone: ""      // "unread": a filled pill that stands out
    property bool active: false
    property bool mono: false
    signal clicked()

    implicitHeight: 40
    radius: Ui.radius.l
    color: active ? Ui.active : (mouse.containsMouse ? Ui.hover : "transparent")
    border.width: activeFocus ? 1 : 0
    border.color: Ui.focus
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: item.text

    // The active row's edge marker, like a cursor in a terminal.
    Rectangle {
        visible: item.active
        width: 2
        height: parent.height - 16
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        color: Ui.accent
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: item.icon.length > 0
            name: item.icon
            size: 17
            strokeWidth: 1.6
            color: item.active ? Ui.text : Ui.text2
        }
        Text {
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter
            text: item.text
            font.family: item.mono ? Ui.mono : Ui.sans
            font.pixelSize: item.mono ? Ui.size.small : Ui.size.body
            color: item.active ? Ui.text : Ui.text2
        }
    }
    Text {
        textFormat: Text.PlainText
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        visible: item.badgeTone !== "unread"
        text: item.badge
        font.family: Ui.mono
        font.pixelSize: Ui.size.caption
        color: Ui.text3
    }
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        visible: item.badgeTone === "unread" && item.badge.length > 0
        width: Math.max(20, pill.implicitWidth + 12)
        height: 20
        radius: Ui.radius.m
        color: Ui.unread
        Text {
            textFormat: Text.PlainText
            id: pill
            anchors.centerIn: parent
            text: item.badge
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            font.weight: Ui.weight.semibold
            color: "#ffffff"
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: item.clicked()
    }
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            item.clicked();
            event.accepted = true;
        }
    }
}
