import QtQuick

// One button of the formatting toolbar: a short label ("H1", "B") or an icon.
Rectangle {
    id: tool

    property string label: ""
    property string icon: ""
    property string tooltip: ""
    property bool bold: false
    property bool italic: false
    property bool mono: false
    signal clicked()

    implicitWidth: Math.max(28, labelText.implicitWidth + 14)
    implicitHeight: 28
    radius: Ui.radius.m
    color: mouse.containsMouse ? Ui.hover : "transparent"
    opacity: enabled ? 1 : 0.4

    Accessible.role: Accessible.Button
    Accessible.name: tool.tooltip

    Text {
        textFormat: Text.PlainText
        id: labelText
        anchors.centerIn: parent
        visible: tool.icon.length === 0
        text: tool.label
        font.family: tool.mono ? Ui.mono : Ui.sans
        font.pixelSize: Ui.size.small
        font.weight: tool.bold ? Ui.weight.bold : Ui.weight.medium
        font.italic: tool.italic
        color: mouse.containsMouse ? Ui.text : Ui.text2
    }
    Icon {
        anchors.centerIn: parent
        visible: tool.icon.length > 0
        name: tool.icon
        size: 15
        color: mouse.containsMouse ? Ui.text : Ui.text2
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tool.clicked()
    }

    // Plain tooltip: the shortcut, shown after a moment.
    Rectangle {
        visible: mouse.containsMouse && tool.tooltip.length > 0 && hoverTimer.done
        y: -height - 6
        anchors.horizontalCenter: parent.horizontalCenter
        width: tipText.implicitWidth + 16
        height: tipText.implicitHeight + 10
        radius: Ui.radius.s
        color: Ui.raised
        border.width: 1
        border.color: Ui.borderStrong
        z: 10
        Text {
            textFormat: Text.PlainText
            id: tipText
            anchors.centerIn: parent
            text: tool.tooltip
            font.family: Ui.sans
            font.pixelSize: Ui.size.caption
            color: Ui.text
        }
    }
    Timer {
        id: hoverTimer
        property bool done: false
        interval: 500
        running: mouse.containsMouse
        onTriggered: done = true
        onRunningChanged: if (!running) done = false
    }
}
