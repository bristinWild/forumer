import QtQuick
import QtQuick.Layouts

// A titled block inside a Page: title + mono caption on the left, an optional
// action button on the right, a rule, then the content.
//   Section { title: "outbox"; caption: "2 unsent"; actions: [ FButton {…} ]; … }
ColumnLayout {
    id: section

    property string title: ""
    property string caption: ""
    property string note: ""             // a paragraph under the rule
    default property alias content: body.data
    property alias actions: actionRow.data

    Layout.fillWidth: true
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 12
        spacing: 12
        Text {
            textFormat: Text.PlainText
            text: section.title
            font.family: Ui.sans
            font.pixelSize: Ui.size.section
            font.weight: Ui.weight.semibold
            color: Ui.text
        }
        Text {
            textFormat: Text.PlainText
            Layout.alignment: Qt.AlignBaseline
            text: section.caption
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            color: Ui.text3
        }
        Item { Layout.fillWidth: true }
        Row { id: actionRow; spacing: 8 }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Ui.border }

    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        Layout.topMargin: 14
        Layout.maximumWidth: 640
        visible: section.note.length > 0
        text: section.note
        wrapMode: Text.WordWrap
        font.family: Ui.sans
        font.pixelSize: Ui.size.ui
        lineHeight: 1.35
        color: Ui.text2
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: 0
    }
}
