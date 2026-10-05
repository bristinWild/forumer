import QtQuick
import QtQuick.Layouts

// Settings → "storage (experiment)": a first contact with Logos Storage.
// Start the node, share a small test file, and fetch it by its CID from
// another instance - to learn, before history bundles are built on it,
// whether files really travel between two computers and how long it takes.
Section {
    id: probe

    required property var store
    readonly property var st: store.storage || ({})
    readonly property var fetch: st.fetch || ({})
    readonly property bool running: st.state === "running"
    property string message: ""
    property bool copied: false

    // Elapsed time while a fetch is in flight.
    property int tick: 0
    Timer {
        interval: 1000; repeat: true
        running: ["connecting", "manifest", "fetching"].indexOf(probe.fetch.state) >= 0
        onTriggered: probe.tick++
    }

    title: "storage (experiment)"
    caption: probe.st.state === "running" ? "running" + (probe.st.attached ? " · basecamp's node" : " · own node")
           : probe.st.state === "starting" ? "starting…"
           : probe.st.state === "failed" ? "failed" : "off"
    note: "logos storage will keep the forum's history for newcomers. this checks it works from here: share a test file in one window, copy its share code, paste it into another window (or another computer) and fetch. the code carries the file's id and this node's address, so the other side can dial in directly. the file holds only a time and a random tag."

    function call(pending) {
        probe.message = "";
        probe.store.call(pending, null, function (e) { probe.message = e; });
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 18
        spacing: 14

        // Status
        GridLayout {
            Layout.fillWidth: true
            visible: probe.st.state !== undefined && probe.st.state !== "off"
            columns: 2
            columnSpacing: 24
            rowSpacing: 6
            Repeater {
                model: [
                    ["node", probe.st.state + (probe.st.detail ? " · " + probe.st.detail : "")],
                    ["peer id", probe.st.peerId || "…"],
                    ["reachability", (probe.st.reachability || "checking…") + " · " + (probe.st.peers || 0) + " peers known"]
                ]
                delegate: Item {
                    required property var modelData
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    implicitHeight: 20
                    Text {
                        textFormat: Text.PlainText
                        width: 140
                        text: parent.modelData[0]
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.ui
                        color: Ui.text2
                    }
                    Text {
                        textFormat: Text.PlainText
                        x: 164
                        width: parent.width - x
                        text: parent.modelData[1]
                        elide: Text.ElideMiddle
                        font.family: Ui.mono
                        font.pixelSize: Ui.size.small
                        color: Ui.text
                    }
                }
            }
        }

        RowLayout {
            spacing: 12
            FButton {
                kind: probe.running ? "outline" : "primary"
                text: probe.running ? "refresh" : probe.st.state === "starting" ? "starting…" : "start storage"
                enabled: probe.st.state !== "starting"
                onClicked: probe.call(probe.store.backend.storageStart())
            }
            FButton {
                visible: probe.running
                text: probe.st.sharing ? "sharing…" : "share a test file"
                enabled: !probe.st.sharing
                onClicked: probe.call(probe.store.backend.storageShareTest())
            }
        }

        // The CID just shared
        RowLayout {
            Layout.fillWidth: true
            visible: (probe.st.lastCid || "").length > 0
            spacing: 12
            Text {
                textFormat: Text.PlainText
                text: "shared as"
                font.family: Ui.sans
                font.pixelSize: Ui.size.ui
                color: Ui.text2
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: probe.st.lastCid || ""
                elide: Text.ElideMiddle
                font.family: Ui.mono
                font.pixelSize: Ui.size.small
                color: Ui.text
            }
            FButton {
                kind: "ghost"
                icon: probe.copied ? "check" : "link"
                text: probe.copied ? "copied" : "copy share code"
                onClicked: {
                    clipboard.text = probe.st.shareCode || probe.st.lastCid;
                    clipboard.selectAll();
                    clipboard.copy();
                    probe.copied = true;
                    copiedTimer.restart();
                }
                Timer { id: copiedTimer; interval: 1500; onTriggered: probe.copied = false }
            }
        }

        // Fetch by share code (or bare CID)
        RowLayout {
            Layout.fillWidth: true
            visible: probe.running
            spacing: 12
            FField {
                id: cidField
                Layout.fillWidth: true
                mono: true
                placeholder: "paste the share code from the other window"
                onAccepted: probe.call(probe.store.backend.storageFetch(cidField.text))
            }
            FButton {
                implicitHeight: 44
                readonly property bool busy: ["connecting", "manifest", "fetching"].indexOf(probe.fetch.state) >= 0
                text: probe.fetch.state === "connecting" ? "dialing…" : busy ? "fetching…" : "fetch"
                enabled: cidField.text.trim().length > 0 && !busy
                onClicked: probe.call(probe.store.backend.storageFetch(cidField.text))
            }
        }

        // Result
        Rectangle {
            Layout.fillWidth: true
            visible: probe.fetch.state !== undefined
            implicitHeight: resultCol.implicitHeight + 24
            radius: Ui.radius.l
            color: Ui.field
            border.width: 1
            border.color: probe.fetch.state === "failed" ? Ui.failed
                        : probe.fetch.state === "done" ? Ui.live : Ui.borderStrong
            ColumnLayout {
                id: resultCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 6
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: {
                        probe.tick;
                        var f = probe.fetch;
                        if (f.state === "connecting")
                            return "dialing the holder… " + Math.floor((Date.now() - f.startedMs) / 1000) + " s";
                        if (f.state === "manifest")
                            return "finding the file… " + Math.floor((Date.now() - f.startedMs) / 1000) + " s";
                        if (f.state === "fetching")
                            return "fetching… " + Math.floor((Date.now() - f.startedMs) / 1000) + " s";
                        if (f.state === "done")
                            return "fetched in " + (f.ms / 1000).toFixed(1) + " s";
                        return "could not fetch" + (f.ms ? " (after " + (f.ms / 1000).toFixed(1) + " s)" : "")
                               + (f.error ? ": " + f.error : "");
                    }
                    wrapMode: Text.WordWrap
                    font.family: Ui.mono
                    font.pixelSize: Ui.size.small
                    color: probe.fetch.state === "failed" ? Ui.failedText
                         : probe.fetch.state === "done" ? Ui.live : Ui.text2
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: (probe.fetch.connectError || "").length > 0
                    text: "dialing the holder failed: " + (probe.fetch.connectError || "")
                    wrapMode: Text.WordWrap
                    font.family: Ui.mono
                    font.pixelSize: Ui.size.caption
                    color: Ui.text3
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: probe.fetch.state === "done"
                    text: probe.fetch.text || ""
                    wrapMode: Text.WordWrap
                    font.family: Ui.mono
                    font.pixelSize: Ui.size.small
                    color: Ui.text
                }
            }
        }

        Notice { Layout.fillWidth: true; message: probe.message }
    }

    // Off-screen helper for "copy id".
    TextEdit { id: clipboard; visible: false; textFormat: TextEdit.PlainText }
}
