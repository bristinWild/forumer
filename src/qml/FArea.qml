import QtQuick
import QtQuick.Controls.Basic as B

// A labelled multi-line input that grows with its text (up to maxHeight, then
// scrolls). `bare: true` drops the box — for inputs that sit inside a panel
// that already draws one (the reply composer).
Column {
    id: area

    property string label: ""
    property alias text: edit.text
    property string placeholder: ""
    property int minHeight: 96
    property int maxHeight: 260
    property bool bare: false
    property bool editable: true
    property alias input: edit

    function focusInput() { edit.forceActiveFocus(); }

    spacing: 8

    Text {
        visible: area.label.length > 0
        text: area.label
        font.family: Ui.sans
        font.pixelSize: Ui.size.small
        color: Ui.text2
    }

    B.ScrollView {
        width: parent.width
        height: Math.max(area.minHeight, Math.min(area.maxHeight, edit.implicitHeight))
        clip: true

        B.TextArea {
            id: edit
            wrapMode: TextEdit.Wrap
            enabled: area.editable
            placeholderText: area.placeholder
            placeholderTextColor: Ui.text3
            color: Ui.text
            selectionColor: Ui.borderStrong
            selectedTextColor: Ui.text
            font.family: Ui.sans
            font.pixelSize: Ui.size.reading
            leftPadding: area.bare ? 0 : 14
            rightPadding: area.bare ? 0 : 14
            topPadding: area.bare ? 4 : 12
            bottomPadding: area.bare ? 4 : 12
            Accessible.name: area.label.length > 0 ? area.label : area.placeholder
            background: Rectangle {
                visible: !area.bare
                radius: Ui.radius.l
                color: Ui.field
                border.width: 1
                border.color: edit.activeFocus ? Ui.focus : Ui.borderStrong
            }
        }
    }
}
