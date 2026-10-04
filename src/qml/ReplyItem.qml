import QtQuick

// One reply in a thread, indented by its depth with thin guide lines on the
// left (one per level), like a sub-thread in a document outline.
Item {
    id: reply

    property var item: ({})           // ForumStore.thread entry
    property bool canReply: true
    property bool replyTarget: false  // the composer is answering this one
    signal replyClicked()

    readonly property int depth: item.depth || 0
    readonly property int indent: Math.min(depth, 6) * 26

    implicitHeight: col.implicitHeight + 28

    // Guide lines, one per nesting level.
    Repeater {
        model: Math.min(reply.depth, 6)
        delegate: Rectangle {
            required property int index
            x: 6 + index * 26
            width: 1
            height: reply.height
            color: Ui.borderStrong
        }
    }

    Rectangle {   // marks the reply being answered
        visible: reply.replyTarget
        x: reply.indent - 12
        width: parent.width - x + 12
        height: parent.height
        radius: Ui.radius.l
        color: Ui.hover
    }

    Rectangle {   // a reply to you that was unread when the thread opened
        visible: reply.item.fresh === true && !reply.replyTarget
        x: reply.indent - 12
        width: parent.width - x + 12
        height: parent.height
        radius: Ui.radius.l
        color: Qt.rgba(Ui.unread.r, Ui.unread.g, Ui.unread.b, Ui.dark ? 0.08 : 0.06)
        Rectangle {
            width: 2
            height: parent.height - 16
            anchors.verticalCenter: parent.verticalCenter
            radius: 1
            color: Ui.unread
        }
    }

    Column {
        id: col
        x: reply.indent
        width: parent.width - x
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Row {
            spacing: 10
            Text {
                text: reply.item.author + (reply.item.isOp ? " · op" : "") + " · " + reply.item.time
                font.family: Ui.mono
                font.pixelSize: Ui.size.small
                color: Ui.text3
            }
            StateTag { delivery: reply.item.delivery || "" }
            Text {
                visible: reply.item.fresh === true
                text: "new"
                font.family: Ui.mono
                font.pixelSize: Ui.size.small
                color: Ui.unread
            }
        }
        TextEdit {
            width: parent.width
            text: reply.item.body || ""
            readOnly: true
            selectByMouse: true
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            font.family: Ui.sans
            font.pixelSize: Ui.size.reading
            color: Ui.textBody
            selectionColor: Ui.borderStrong
            selectedTextColor: Ui.text
        }
        FButton {
            visible: reply.canReply
            compact: true
            kind: "ghost"
            text: "reply"
            onClicked: reply.replyClicked()
        }
    }
}
