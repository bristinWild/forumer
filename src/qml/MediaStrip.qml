import QtQuick
import QtQuick.Layouts

// The images attached in one composer: an "attach images" button (the
// system's file picker, run by the backend as its own program - a dialog
// opened from this view would take Basecamp's offscreen view down), files
// dropped on the composer, a thumbnail per image with its upload state, and
// a line saying what posting waits for.
//   MediaStrip { Layout.fillWidth: true; store: …; group: "topic"; forum: store.currentForum }
ColumnLayout {
    id: strip

    required property var store
    property string group: "topic"
    property string forum: "public"
    property string error: ""

    readonly property var items: { strip.store.mediaJson; return strip.store.attachments(strip.group); }
    readonly property bool full: items.length >= 4
    readonly property bool hasPicker: strip.store.media.picker === true
    readonly property bool picking: strip.store.media.picking === strip.group
    readonly property string notice: strip.store.media.noticeGroup === strip.group ? (strip.store.media.notice || "") : ""

    visible: strip.store.canAttachImages
    spacing: 8

    function attach(urls) {
        strip.error = "";
        for (var i = 0; i < urls.length; ++i) {
            var u = String(urls[i]).trim().replace(/^["']|["']$/g, "");   // a pasted path may be quoted
            if (u.length === 0) continue;
            if (strip.items.length + i >= 4) { strip.error = "At most 4 images per post"; break; }
            strip.store.attachImage(strip.group, strip.forum, u, function (e) { strip.error = e; });
        }
    }


    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        FButton {
            kind: "outline"
            icon: "image"
            implicitHeight: 40
            text: strip.picking ? "choose in the picker…" : strip.full ? "4 images attached" : "attach images"
            enabled: strip.store.canCompose && !strip.full && !strip.picking
            tooltip: "JPEG, PNG or WebP · up to 4 · re-encoded on this device, so location and camera details never leave it"
            onClicked: {
                strip.error = "";
                if (strip.hasPicker)
                    strip.store.call(strip.store.backend.pickImages(strip.group, strip.forum), null,
                                     function (e) { strip.error = e; pathRow.visible = true; });
                else { pathRow.visible = true; pathField.focusInput(); }
            }
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: strip.items.length > 0 ? strip.store.attachmentNote(strip.group)
                                         : "or drop them here · JPEG, PNG, WebP · up to 4"
            elide: Text.ElideRight
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            color: strip.items.some(function (a) { return a.state === "failed"; }) ? Ui.failedText : Ui.text3
        }
    }

    // Only where there is no system file picker: paste a path.
    RowLayout {
        id: pathRow
        Layout.fillWidth: true
        visible: false
        spacing: 8
        FField {
            id: pathField
            Layout.fillWidth: true
            placeholder: Qt.platform.os === "osx" ? "paste the picture's path (in Finder: select it, ⌥⌘C)"
                                                  : "paste the picture's path"
            mono: true
            onAccepted: { strip.attach([pathField.text.trim()]); pathField.text = ""; }
        }
        FButton {
            text: "attach"
            enabled: pathField.text.trim().length > 0
            onClicked: { strip.attach([pathField.text.trim()]); pathField.text = ""; }
        }
    }

    Flow {
        Layout.fillWidth: true
        visible: strip.items.length > 0
        spacing: 10
        Repeater {
            model: strip.items
            delegate: Rectangle {
                id: tile
                required property var modelData
                width: 120
                height: 120
                radius: Ui.radius.m
                color: Ui.field
                border.width: 1
                border.color: modelData.state === "failed" ? Ui.failed : Ui.borderStrong
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: tile.modelData.thumb || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    opacity: tile.modelData.state === "ready" ? 1 : 0.55
                }
                Rectangle {   // state, along the bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 24
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Text {
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        text: tile.modelData.state === "ready" ? "ready · " + strip.store.sizeText(tile.modelData.size)
                            : tile.modelData.state === "uploading" ? "uploading " + (tile.modelData.progress || 0) + "%"
                            : tile.modelData.state === "failed" ? "failed"
                            : "waiting"
                        font.family: Ui.mono
                        font.pixelSize: 11
                        color: "#ffffff"
                    }
                }
                Rectangle {   // remove
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 4
                    width: 22; height: 22; radius: 11
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Icon { anchors.centerIn: parent; name: "x"; size: 13; color: "#ffffff" }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: strip.store.removeAttachment(strip.group, tile.modelData.id)
                    }
                }
                MouseArea {   // retry a failed one
                    anchors.fill: parent
                    anchors.topMargin: 30
                    enabled: tile.modelData.state === "failed" || (tile.modelData.state === "queued" && strip.store.storageState === "failed")
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: strip.store.retryAttachment(strip.group, tile.modelData.id)
                }
            }
        }
    }

    // Why an upload failed (click the image to retry).
    Repeater {
        model: strip.items.filter(function (a) { return a.state === "failed" && a.error; })
        delegate: Notice {
            required property var modelData
            Layout.fillWidth: true
            message: modelData.name + ": " + modelData.error + " (click it to retry)"
        }
    }

    Notice { Layout.fillWidth: true; message: strip.error.length > 0 ? strip.error : strip.notice }
}
