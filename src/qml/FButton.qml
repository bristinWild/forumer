import QtQuick

// Forumer's button. Three looks:
//   "primary" — the one white (dark mode) / black (light) action on a screen
//   "outline" — a quiet bordered action
//   "ghost"   — text only, for row actions ("reply", "copy link")
// Keyboard: Tab focuses it (with a ring), Enter/Space presses it.
//   FButton { text: "post"; kind: "primary"; icon: "plus"; onClicked: … }
Rectangle {
    id: button

    property string text: ""
    property string kind: "outline"
    property string icon: ""
    property bool compact: false      // ghost row actions: no padding, small text
    property string tooltip: ""
    signal clicked()

    readonly property bool primary: kind === "primary"
    readonly property bool ghost: kind === "ghost"
    readonly property bool hovered: mouse.containsMouse

    implicitHeight: compact ? 24 : Ui.controlHeight
    implicitWidth: compact ? row.implicitWidth
                 : text.length === 0 ? implicitHeight          // icon-only: square
                 : row.implicitWidth + 32
    radius: Ui.radius.m
    opacity: enabled ? 1 : 0.4
    activeFocusOnTab: true

    color: primary ? (hovered ? Qt.lighter(Ui.inverse, Ui.dark ? 1.08 : 1.4) : Ui.inverse)
                   : (ghost || compact ? "transparent" : (hovered ? Ui.hover : "transparent"))
    border.width: (kind === "outline" && !compact) || activeFocus ? 1 : 0
    border.color: activeFocus ? Ui.focus : Ui.borderStrong

    Accessible.role: Accessible.Button
    Accessible.name: button.text.length > 0 ? button.text : button.tooltip

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: button.icon.length > 0
            name: button.icon
            size: button.compact ? 14 : 16
            strokeWidth: 2
            color: label.color
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            visible: button.text.length > 0
            text: button.text
            font.family: Ui.sans
            font.pixelSize: button.compact ? Ui.size.small : Ui.size.ui
            font.weight: button.primary ? Ui.weight.medium : Ui.weight.regular
            color: button.primary ? Ui.inverseText
                 : (button.ghost || button.compact) ? (button.hovered ? Ui.text : Ui.text2)
                 : Ui.text
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (button.enabled) button.clicked()
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            if (button.enabled) button.clicked();
            event.accepted = true;
        }
    }
}
