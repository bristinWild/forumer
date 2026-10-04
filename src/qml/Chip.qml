import QtQuick

// A pill: domain filters, domain suggestions, "replying to …".
//   Chip { text: "#privacy"; selected: true; onClicked: … }
// `dashed: true` is the fainter "+ suggestion" look; `closable` adds an × (onClosed).
Rectangle {
    id: chip

    property string text: ""
    property bool selected: false
    property bool dashed: false
    property bool closable: false
    property bool mono: true
    signal clicked()
    signal closed()

    implicitHeight: 32
    implicitWidth: label.implicitWidth + 28 + (closable ? 22 : 0)
    radius: Ui.radius.pill
    color: selected ? Ui.inverse : (mouse.containsMouse ? Ui.hover : "transparent")
    border.width: dashed ? 0 : 1
    border.color: selected ? Ui.inverse : (activeFocus ? Ui.focus : Ui.borderStrong)
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: chip.text

    // The "suggestion" look: a fainter border (Qt borders can't be dashed).
    Rectangle {
        anchors.fill: parent
        visible: chip.dashed
        radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: Ui.border
    }

    Row {
        anchors.centerIn: parent
        spacing: 6
        Text {
            textFormat: Text.PlainText
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            font.family: chip.mono ? Ui.mono : Ui.sans
            font.pixelSize: Ui.size.small
            color: chip.selected ? Ui.inverseText : Ui.text2
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.closable
            name: "x"
            size: 12
            strokeWidth: 2.2
            color: Ui.text3
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: chip.closed()
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.rightMargin: chip.closable ? 26 : 0
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.clicked()
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            chip.clicked();
            event.accepted = true;
        }
    }
}
