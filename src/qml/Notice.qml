import QtQuick

// A one-line message: an error (red) or a note (grey). Hidden when empty.
//   Notice { message: store.lastError }
Text {
    id: notice

    property string message: ""
    property bool error: true

    visible: message.length > 0
    text: message
    wrapMode: Text.WordWrap
    font.family: Ui.sans
    font.pixelSize: Ui.size.small
    color: error ? Ui.failedText : Ui.text2
}
