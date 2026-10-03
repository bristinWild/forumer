import QtQuick
import QtQuick.Layouts

// A modal panel over a dimmed screen. Used for "new topic" and the few
// confirmations; a plain Item rather than a Popup so it behaves the same in
// every host.
//   Sheet {
//       id: s; title: "new topic"
//       <content items>
//       actions: [ FButton {…}, FButton {…} ]
//   }
//   s.open() / s.close()
Rectangle {
    id: sheet

    property string title: ""
    property int panelWidth: 600
    default property alias content: body.data
    property alias actions: actionRow.data
    signal opened()
    signal dismissed()

    function open() { sheet.visible = true; sheet.opened(); }
    function close() { sheet.visible = false; }

    anchors.fill: parent
    visible: false
    z: 100
    color: Ui.scrim

    // Swallow clicks so nothing underneath can be used while open; a click on
    // the dimmed area closes it.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { sheet.close(); sheet.dismissed(); }
    }

    Keys.onEscapePressed: { sheet.close(); sheet.dismissed(); }

    Rectangle {
        id: panel
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 2 * Ui.space.l, sheet.panelWidth)
        height: Math.min(parent.height - 2 * Ui.space.l, column.implicitHeight + 56)
        y: Math.max(Ui.space.l, Math.min(96, (parent.height - height) / 2))
        radius: Ui.radius.sheet
        color: Ui.surface
        border.width: 1
        border.color: Ui.borderStrong
        clip: true

        MouseArea { anchors.fill: parent }   // clicks on the panel stay on it

        Flickable {
            anchors.fill: parent
            anchors.margins: 28
            contentHeight: column.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ColumnLayout {
                id: column
                width: parent.width
                spacing: 20

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: sheet.title
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.sheet
                        font.weight: Ui.weight.semibold
                        color: Ui.text
                    }
                    FButton {
                        kind: "ghost"
                        icon: "x"
                        tooltip: "Close"
                        onClicked: { sheet.close(); sheet.dismissed(); }
                    }
                }

                ColumnLayout {
                    id: body
                    Layout.fillWidth: true
                    spacing: 20
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Row {
                        id: actionRow
                        spacing: 8
                    }
                }
            }
        }
    }
}
