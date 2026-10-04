import QtQuick
import QtQuick.Layouts

// replies: answers to this account's posts, and replies in its topics, newest
// first. Worked out on this device only; nothing about it is published.
Page {
    id: screen

    required property var store

    title: "replies"
    subtitle: "answers to what you wrote, and new replies in threads you started or joined. worked out on this device only: nobody else can tell who was notified."

    Section {
        title: "replies to you"
        caption: screen.store.unreadReplies === 0 ? "all read"
               : screen.store.unreadReplies === 1 ? "1 unread" : screen.store.unreadReplies + " unread"
        actions: [
            FButton {
                visible: screen.store.unreadReplies > 0
                implicitHeight: 36
                text: "mark all read"
                icon: "check"
                onClicked: screen.store.markAllRepliesRead()
            }
        ]

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: screen.store.inbox.length === 0
            text: "nothing yet. when someone answers you, or replies in a thread you started or joined, it shows up here, even for posts you made anonymously."
            wrapMode: Text.WordWrap
            font.family: Ui.sans
            font.pixelSize: Ui.size.ui
            lineHeight: 1.35
            color: Ui.text3
        }
        Repeater {
            model: screen.store.inbox
            delegate: ReplyLine {
                required property var modelData
                Layout.fillWidth: true
                item: modelData
                onClicked: screen.store.openTopic(modelData.topicId)
            }
        }
    }
}
