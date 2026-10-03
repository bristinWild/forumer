import QtQuick
import QtQuick.Layouts

// missed: the outbox (our posts the network hasn't confirmed, with retry) and
// catch-up (ask peers now; what arrived from before this session started).
Page {
    id: missed

    required property var store
    property bool asking: false

    title: "missed"
    subtitle: "posts of yours that have not gone out yet, and posts that arrived while you were away."

    Section {
        title: "outbox"
        caption: missed.store.outbox.length === 0 ? "all sent" : missed.store.outbox.length + " unsent"
        actions: [
            FButton {
                visible: missed.store.outbox.length > 0
                implicitHeight: 36
                text: "retry all"
                icon: "refresh"
                onClicked: missed.store.call(missed.store.backend.retryUnsent())
            }
        ]

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: missed.store.outbox.length === 0
            text: "nothing waiting. posts that can't reach the network stay here and are retried automatically: after 15 s, 30 s, 1 min, 2 min, 4 min, then every 5 min."
            wrapMode: Text.WordWrap
            font.family: Ui.sans
            font.pixelSize: Ui.size.ui
            lineHeight: 1.35
            color: Ui.text3
        }
        Repeater {
            model: missed.store.outbox
            delegate: PostLine {
                required property var modelData
                Layout.fillWidth: true
                title: modelData.title
                body: modelData.body
                delivery: modelData.delivery
                actionText: modelData.delivery === "failed" ? "retry now" : ""
                onClicked: missed.store.openTopic(modelData.topicId)
                onAction: missed.store.call(missed.store.backend.retryUnsent())
            }
        }
    }

    Section {
        title: "catch-up"
        caption: missed.asking ? "asking peers…" : missed.store.syncInfo
        note: "forumer asks the peers you can reach for anything posted in the last 48 hours. it also does this when you come online and every few minutes after."
        actions: [
            FButton {
                implicitHeight: 36
                text: missed.asking ? "loading…" : "load now"
                icon: "refresh"
                enabled: !missed.asking
                onClicked: {
                    missed.asking = true;
                    askTimer.restart();
                    missed.store.call(missed.store.backend.catchUp());
                }
            }
        ]

        Timer { id: askTimer; interval: 4000; onTriggered: missed.asking = false }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: missed.store.missed.length === 0
            text: "nothing caught up yet this session."
            font.family: Ui.sans
            font.pixelSize: Ui.size.ui
            color: Ui.text3
        }
        Repeater {
            model: missed.store.missed
            delegate: PostLine {
                required property var modelData
                Layout.fillWidth: true
                title: modelData.title
                meta: modelData.meta
                onClicked: missed.store.openTopic(modelData.topicId)
            }
        }
    }
}
