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
        // Header and buttons stay put; only the content between them scrolls,
        // so a long message can never push "post" out of reach (review F15).
        height: Math.min(parent.height - 2 * Ui.space.l,
                         header.implicitHeight + body.implicitHeight + footer.implicitHeight + 2 * 28 + 2 * 20 + 1)
        y: Math.max(Ui.space.l, Math.min(96, (parent.height - height) / 2))
        radius: Ui.radius.sheet
        color: Ui.surface
        border.width: 1
        border.color: Ui.borderStrong
        clip: true

        MouseArea { anchors.fill: parent }   // clicks on the panel stay on it

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 28
            spacing: 20

            RowLayout {
                id: header
                Layout.fillWidth: true
                Text {
                    textFormat: Text.PlainText
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

            Flickable {
                id: scroller
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: body.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ColumnLayout {
                    id: body
                    width: scroller.width
                    spacing: 20
                }
            }

            // A rule above the buttons when the content scrolls under them.
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: -10
                Layout.bottomMargin: -10
                implicitHeight: 1
                color: Ui.border
                opacity: scroller.contentHeight > scroller.height + 1 ? 1 : 0
            }

            RowLayout {
                id: footer
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
