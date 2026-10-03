import QtQuick
import QtQuick.Layouts

// my posts: everything written from this device, newest first, with its
// delivery state.
Page {
    id: mine

    required property var store

    title: "my posts"
    subtitle: "topics and replies written on this device, and whether each one reached the network."

    Section {
        title: "posts"
        caption: mine.store.myPosts.length === 1 ? "1 post" : mine.store.myPosts.length + " posts"

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: mine.store.myPosts.length === 0
            text: "nothing yet. your topics and replies will collect here."
            font.family: Ui.sans
            font.pixelSize: Ui.size.ui
            color: Ui.text3
        }
        Repeater {
            model: mine.store.myPosts
            delegate: PostLine {
                required property var modelData
                Layout.fillWidth: true
                title: modelData.title
                body: modelData.body
                delivery: modelData.delivery
                onClicked: mine.store.openTopic(modelData.topicId)
            }
        }
    }
}
