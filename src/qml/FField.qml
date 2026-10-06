import QtQuick
import QtQuick.Controls.Basic as B

// A labelled single-line input.
//   FField { label: "title"; placeholder: "…"; onAccepted: … }
// `password: true` masks it; `mono: true` sets it in the mono face (domains,
// phrases). `text` reads/writes the value; `focusInput()` focuses it.
Column {
    id: field

    property string label: ""
    property string hint: ""                // small grey text right of the label
    property alias text: input.text
    property string placeholder: ""
    property bool password: false
    property bool mono: false
    property int fieldHeight: 44
    property alias input: input
    signal accepted()

    function focusInput() { input.forceActiveFocus(); }

    spacing: 8

    Row {
        width: parent.width
        visible: field.label.length > 0 || field.hint.length > 0
        Text {
            textFormat: Text.PlainText
            id: labelText
            text: field.label
            font.family: Ui.sans
            font.pixelSize: Ui.size.small
            color: Ui.text2
        }
        Item { width: Math.max(0, parent.width - labelText.width - hintText.width); height: 1 }
        Text {
            textFormat: Text.PlainText
            id: hintText
            text: field.hint
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            color: Ui.text3
        }
    }

    B.TextField {
        id: input
        width: parent.width
        height: field.fieldHeight
        leftPadding: 14
        rightPadding: 14
        placeholderText: field.placeholder
        placeholderTextColor: Ui.text3
        echoMode: field.password ? TextInput.Password : TextInput.Normal
        color: Ui.text
        selectionColor: Ui.borderStrong
        selectedTextColor: Ui.text
        font.family: field.mono ? Ui.mono : Ui.sans
        font.pixelSize: field.mono ? Ui.size.ui : Ui.size.reading
        Accessible.name: field.label.length > 0 ? field.label : field.placeholder
        background: Rectangle {
            radius: Ui.radius.l
            color: Ui.field
            border.width: 1
            border.color: input.activeFocus ? Ui.focus : Ui.borderStrong
        }
        onAccepted: field.accepted()
    }
}
