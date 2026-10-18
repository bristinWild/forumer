import QtQuick
import QtQuick.Layouts

// The images of one post. Each shows its preview as soon as the thread opens
// (it came with the post over Delivery; asked for if missing), then the full
// image once Storage has it (automatically with "load full images" on, else
// on a click). Every picture is checked against what the author signed.
//   MediaCards { store: …; postId: …; media: post.media; Layout.fillWidth: true }
ColumnLayout {
    id: cards

    required property var store
    property string postId: ""
    property var media: []
    property int maxImageHeight: 480

    visible: media.length > 0
    spacing: 10

    // On screen: show what this device has, get the rest. Asked once both the
    // post id and its images are known (bindings settle in any order).
    function request() {
        if (cards.postId.length === 0) return;
        for (var i = 0; i < cards.media.length; ++i)
            if (cards.media[i] && cards.media[i].ok) cards.store.showMedia(cards.postId, i);
    }
    onPostIdChanged: Qt.callLater(cards.request)
    onMediaChanged: Qt.callLater(cards.request)
    Component.onCompleted: Qt.callLater(cards.request)

    // Seconds since fetching started, refreshed while something is loading.
    property int tick: 0
    Timer {
        interval: 1000
        repeat: true
        running: cards.visible && cards.media.some(function (m, i) {
            var s = cards.store.mediaState(cards.postId, i).state;
            return s === "starting" || s === "fetching";
        })
        onTriggered: cards.tick++
    }

    Repeater {
        model: cards.media
        delegate: Item {
            id: slot
            required property var modelData
            required property int index
            readonly property var st: cards.store.mediaState(cards.postId, index)
            readonly property bool full: st.state === "done" && picture.status === Image.Ready
            readonly property bool previewOnly: !full && (st.previewUrl || "").length > 0
                                                && picture.status === Image.Ready
            readonly property bool busy: st.state === "starting" || st.state === "fetching"
            // The picture's data can't be drawn here. In Basecamp builds that
            // block data: image URLs (and have no SandboxImages provider yet)
            // this is the normal case, so say so instead of showing a blank box.
            readonly property bool cantShow: picture.status === Image.Error
            Layout.fillWidth: true
            implicitHeight: full ? picture.height
                          : previewOnly ? picture.height + status.implicitHeight + 6
                          : card.implicitHeight


            // The full image, or its preview until then.
            Image {
                id: picture
                // Never wider than the column or taller than maxImageHeight; a
                // preview is shown at the full image's size (it is smaller).
                readonly property real fitScale: implicitWidth > 0 && implicitHeight > 0
                    ? Math.min(slot.full ? 1 : 2, slot.width / implicitWidth, cards.maxImageHeight / implicitHeight) : 0
                width: implicitWidth * fitScale
                height: implicitHeight * fitScale
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                visible: slot.full || slot.previewOnly
                // Basecamp blocks data: image URLs in sandboxed plugins.
                // Use its engine-local in-memory provider when available; keep
                // the original behavior for older standalone environments.
                source: {
                    var raw = slot.st.state === "done" ? slot.st.detail : (slot.st.previewUrl || "");
                    return typeof SandboxImages !== "undefined"
                        ? SandboxImages.sourceFor(raw) : raw;
                }
                onStatusChanged: if (status === Image.Error) cards.store.log("media image didn't decode: " + errorString)
            }

            // Under a preview: where the full image is.
            Text {
                id: status
                y: picture.height + 6
                visible: slot.previewOnly
                textFormat: Text.PlainText
                width: slot.width
                elide: Text.ElideRight
                text: {
                    cards.tick;
                    var s = slot.st;
                    var secs = s.startedMs ? Math.floor((Date.now() - s.startedMs) / 1000) : 0;
                    if (s.state === "starting") return "preview · full quality once the storage node runs…";
                    if (s.state === "fetching") {
                        var d = s.detail || "";
                        return "preview · full quality: " + (/^[0-9]+$/.test(d) ? "downloading " + d + "%"
                               : d === "download" ? "downloading" : d === "holder" ? "connecting to a holder"
                               : "looking for a holder") + " · " + secs + " s";
                    }
                    if (s.state === "failed") return "preview · full quality unavailable: " + s.detail + " · click to retry";
                    return "preview · click for full quality (" + cards.store.sizeText(slot.modelData.size) + ")";
                }
                font.family: Ui.mono
                font.pixelSize: Ui.size.caption
                color: slot.st.state === "failed" ? Ui.failedText : Ui.text3
            }
            MouseArea {
                anchors.fill: parent
                visible: slot.previewOnly && !slot.busy
                cursorShape: Qt.PointingHandCursor
                onClicked: cards.store.loadMedia(cards.postId, slot.index)
            }

            Rectangle {
                id: card
                visible: !slot.full && !slot.previewOnly
                width: Math.min(parent.width, 420)
                implicitHeight: row.implicitHeight + 24
                radius: Ui.radius.l
                color: mouse.containsMouse && mouse.enabled ? Ui.hover : Ui.field
                border.width: 1
                border.color: slot.st.state === "failed" ? Ui.failed : Ui.borderStrong

                RowLayout {
                    id: row
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 14
                    spacing: 12
                    Icon { name: "image"; size: 20; color: Ui.text2 }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: {
                            cards.tick;
                            var s = slot.st;
                            var what = "image · " + cards.store.sizeText(slot.modelData.size);
                            if (!slot.modelData.ok) return "an attachment this version can't show";
                            if (slot.cantShow)
                                return what + " · this Basecamp can't display images yet "
                                     + "(it may need an update). The post itself is unaffected.";
                            if (s.state === "starting") return what + " · starting the storage node…";
                            if (s.state === "fetching") {
                                var secs = s.startedMs ? Math.floor((Date.now() - s.startedMs) / 1000) : 0;
                                var d = s.detail || "";
                                var step = d === "lookup" ? "looking for who holds it"
                                         : d === "holder" ? "found a holder, connecting"
                                         : d === "download" ? "downloading"
                                         : /^[0-9]+$/.test(d) ? "downloading " + d + "%"
                                         : "fetching";
                                return what + " · " + step + " · " + secs + " s";
                            }
                            if (s.state === "done") return what + " · showing…";
                            if (s.state === "failed") return s.detail + " · click to try again";
                            return what + " · waiting for its preview · click to load";
                        }
                        font.family: Ui.mono
                        font.pixelSize: Ui.size.caption
                        color: slot.st.state === "failed" ? Ui.failedText : Ui.text2
                    }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: slot.modelData.ok && !slot.cantShow
                             && (slot.st.state === "idle" || slot.st.state === "failed")
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: cards.store.loadMedia(cards.postId, slot.index)
                }
            }
        }
    }
}
