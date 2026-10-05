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
            textFormat: Text.PlainText
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
            textFormat: Text.PlainText
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
    // Older history: fetched week by week from peers, back to the oldest post
    // anyone holds. Automatic; "load older" walks four more weeks back.
    Section {
        id: historySection
        readonly property var h: missed.store.history || ({})
        // What the network's store nodes gave us this session.
        function archiveText() {
            var a = historySection.h.archive || "";
            if (a === "asking") return "asking store nodes… " + (h.archiveGot || 0) + " so far";
            if (a === "done") return (h.archiveGot || 0) + " post(s) from " + (h.archiveNode || "a store node");
            if (a === "empty") return "store nodes hold nothing new for this forum";
            if (a === "unreachable") return "no store node answered" + (h.archiveError ? " (" + h.archiveError + ")" : "");
            return "not asked yet";
        }
        title: "history"
        caption: !h.floorMs ? ""
               : h.active ? "fetching " + missed.store.day(h.fromMs) + " – " + missed.store.day(h.toMs) + "…"
               : (h.targetMs && h.targetMs < h.floorMs) ? "waiting for peers"
               : "complete back to " + missed.store.day(h.floorMs)
        note: "on joining, forumer first asks the network's store nodes for what they kept (the last weeks), so you get the forum even when nobody else is online. older posts are fetched from peers a week at a time, back to the oldest post anyone has. it runs on its own; nothing to do here unless you want to go further back."
        actions: [
            FButton {
                implicitHeight: 36
                text: historySection.h.active ? "fetching…" : "load older"
                icon: "refresh"
                enabled: !historySection.h.active
                onClicked: missed.store.call(missed.store.backend.loadOlderHistory())
            }
        ]

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            columns: 2
            columnSpacing: 24
            rowSpacing: 6
            Repeater {
                model: [
                    ["complete back to", historySection.h.floorMs ? missed.store.day(historySection.h.floorMs) : "…"],
                    ["oldest known post", historySection.h.targetMs ? missed.store.day(historySection.h.targetMs) : "not heard of yet"],
                    ["older posts received", String(historySection.h.received || 0) + " this session"],
                    ["network archive", historySection.archiveText()]
                ]
                delegate: Item {
                    required property var modelData
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    implicitHeight: 20
                    Text {
                        textFormat: Text.PlainText
                        width: 170
                        text: parent.modelData[0]
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.ui
                        color: Ui.text2
                    }
                    Text {
                        textFormat: Text.PlainText
                        x: 194
                        width: parent.width - x
                        text: parent.modelData[1]
                        font.family: Ui.mono
                        font.pixelSize: Ui.size.small
                        color: Ui.text
                    }
                }
            }
        }
    }
}
