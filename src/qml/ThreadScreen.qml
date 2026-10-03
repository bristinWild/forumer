import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// A topic and its replies, set like a document: one reading column (~720 px)
// and, on wide windows, an "in this thread" rail that jumps between
// sub-threads. The composer at the bottom answers the topic, or the reply
// whose "reply" was pressed (shown as a "replying to …" chip).
Item {
    id: thread

    required property var store

    readonly property var topic: store.currentTopic
    readonly property bool wide: width > 1120

    property string disclosure: store.defaultDisclosure
    property string error: ""
    property bool sending: false

    function focusComposer() { composerArea.focusInput(); }

    // Scroll so the reply `id` is at the top of the view.
    function scrollTo(id) {
        for (var i = 0; i < replies.count; ++i) {
            var it = replies.itemAt(i);
            if (it && it.item.id === id) {
                var p = it.mapToItem(content, 0, 0);
                flick.contentY = Math.max(0, Math.min(p.y - 24, flick.contentHeight - flick.height));
                return;
            }
        }
    }

    function send() {
        var body = composerArea.text.trim();
        if (body.length === 0 || thread.sending) return;
        thread.error = "";
        thread.sending = true;
        thread.store.sendReply(body, thread.disclosure, function () {
            thread.sending = false;
            composerArea.text = "";
            thread.store.replyTargetId = "";
        }, function (err) {
            thread.sending = false;
            thread.error = err;
        });
    }

    onTopicChanged: thread.disclosure = store.defaultDisclosure

    B.ScrollView {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        Flickable {
            id: flick
            contentWidth: scroller.availableWidth
            contentHeight: content.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            Item {
                id: content
                width: flick.contentWidth
                implicitHeight: Math.max(article.implicitHeight, rail.implicitHeight) + 112

                // ── Reading column ─────────────────────────────────────────────
                ColumnLayout {
                    id: article
                    x: 64
                    y: 48
                    width: Math.min(Ui.readingWidth, content.width - 128 - (thread.wide ? 288 : 0))
                    spacing: 24

                    // Breadcrumb
                    Row {
                        spacing: 8
                        Text {
                            text: "home"
                            font.family: Ui.mono
                            font.pixelSize: Ui.size.small
                            color: crumbMouse.containsMouse ? Ui.text : Ui.text2
                            MouseArea {
                                id: crumbMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: thread.store.open("home")
                            }
                        }
                        Text {
                            text: "/"
                            font.family: Ui.mono
                            font.pixelSize: Ui.size.small
                            color: Ui.text3
                        }
                        Text {
                            text: thread.topic && thread.topic.domains.length > 0 ? "#" + thread.topic.domains[0] : "topic"
                            font.family: Ui.mono
                            font.pixelSize: Ui.size.small
                            color: Ui.text3
                        }
                    }

                    // Title + meta
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        TextEdit {
                            Layout.fillWidth: true
                            text: !thread.topic ? "" : thread.topic.placeholder ? "Topic still arriving…" : thread.topic.title
                            readOnly: true
                            selectByMouse: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            font.family: Ui.sans
                            font.pixelSize: Ui.size.thread
                            font.weight: Ui.weight.bold
                            font.letterSpacing: -0.6
                            color: Ui.text
                            selectionColor: Ui.borderStrong
                        }
                        Row {
                            spacing: 10
                            visible: thread.topic !== null && !thread.topic.placeholder
                            Text {
                                text: !thread.topic ? "" : [thread.topic.author, thread.store.ago(thread.topic.tsMs),
                                                           thread.store.domainsText(thread.topic.domains)]
                                                          .filter(function (s) { return s.length > 0; }).join(" · ")
                                font.family: Ui.mono
                                font.pixelSize: Ui.size.small
                                color: Ui.text3
                            }
                            StateTag { delivery: thread.topic ? thread.topic.delivery : "" }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: thread.topic !== null && thread.topic.placeholder
                        text: "Replies to this topic reached you before the topic itself. It will appear here as soon as it syncs — try “catch up now” under missed."
                        wrapMode: Text.WordWrap
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.body
                        color: Ui.text2
                    }

                    // Body
                    TextEdit {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: thread.topic && !thread.topic.placeholder ? thread.topic.body : ""
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        font.family: Ui.sans
                        font.pixelSize: 17
                        color: Ui.textBody
                        selectionColor: Ui.borderStrong
                    }

                    // Topic actions
                    Row {
                        spacing: 6
                        FButton {
                            text: "reply"
                            icon: "reply"
                            implicitHeight: 36
                            enabled: thread.store.canPost
                            onClicked: { thread.store.replyTargetId = ""; thread.focusComposer(); }
                        }
                        FButton {
                            text: copied ? "copied" : "copy id"
                            icon: copied ? "check" : "link"
                            kind: "ghost"
                            implicitHeight: 36
                            property bool copied: false
                            onClicked: {
                                clipboard.text = thread.store.selectedTopicId;
                                clipboard.selectAll();
                                clipboard.copy();
                                copied = true;
                                copiedTimer.restart();
                            }
                            Timer { id: copiedTimer; interval: 1500; onTriggered: parent.copied = false }
                        }
                    }

                    // Replies
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 16
                        text: thread.store.thread.length === 0 ? "no replies yet"
                              : thread.store.thread.length === 1 ? "1 reply"
                              : thread.store.thread.length + " replies"
                        font.family: Ui.sans
                        font.pixelSize: Ui.size.body
                        font.weight: Ui.weight.semibold
                        color: Ui.text
                    }
                    Rectangle { Layout.fillWidth: true; Layout.topMargin: -12; implicitHeight: 1; color: Ui.border }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: -12
                        spacing: 0
                        Repeater {
                            id: replies
                            model: thread.store.thread
                            delegate: ReplyItem {
                                required property var modelData
                                Layout.fillWidth: true
                                item: modelData
                                canReply: thread.store.canNestReplies && thread.store.canPost
                                replyTarget: thread.store.replyTargetId === modelData.id
                                onReplyClicked: {
                                    thread.store.replyTargetId = modelData.id;
                                    thread.focusComposer();
                                }
                            }
                        }
                    }

                    // Composer
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        implicitHeight: composer.implicitHeight + 32
                        radius: Ui.radius.xl
                        color: Ui.surface
                        border.width: 1
                        border.color: composerArea.input.activeFocus ? Ui.focus : Ui.borderStrong

                        ColumnLayout {
                            id: composer
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 16
                            spacing: 12

                            Chip {
                                visible: thread.store.replyTargetId.length > 0
                                text: "replying to " + thread.store.replyAuthor(thread.store.replyTargetId)
                                closable: true
                                implicitHeight: 28
                                onClosed: thread.store.replyTargetId = ""
                                onClicked: thread.scrollTo(thread.store.replyTargetId)
                            }

                            FArea {
                                id: composerArea
                                Layout.fillWidth: true
                                bare: true
                                minHeight: 72
                                placeholder: thread.store.canPost ? "Write a reply…" : "Unlock and connect to reply"
                                editable: thread.store.canPost
                            }

                            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Ui.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Text {
                                    text: "post as"
                                    font.family: Ui.sans
                                    font.pixelSize: Ui.size.small
                                    color: Ui.text2
                                }
                                Segmented {
                                    Layout.preferredWidth: 300
                                    implicitHeight: 38
                                    options: thread.store.disclosureOptions()
                                    value: thread.disclosure
                                    onPicked: (v) => thread.disclosure = v
                                }
                                Item { Layout.fillWidth: true }
                                FButton {
                                    kind: "primary"
                                    text: thread.sending ? "signing…" : "reply"
                                    implicitHeight: 38
                                    enabled: thread.store.canPost && composerArea.text.trim().length > 0 && !thread.sending
                                    onClicked: thread.send()
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: thread.store.disclosureHint(thread.disclosure)
                                wrapMode: Text.WordWrap
                                font.family: Ui.mono
                                font.pixelSize: Ui.size.caption
                                color: Ui.text3
                            }
                            Notice { Layout.fillWidth: true; message: thread.error }
                        }
                    }
                }

                // ── "In this thread" rail ──────────────────────────────────────
                ColumnLayout {
                    id: rail
                    visible: thread.wide
                    x: article.x + article.width + 48
                    y: 52
                    width: 224
                    spacing: 28

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Row {
                            spacing: 10
                            Icon { anchors.verticalCenter: parent.verticalCenter; name: "list"; size: 16; color: Ui.text2 }
                            Text {
                                text: "in this thread"
                                font.family: Ui.sans
                                font.pixelSize: Ui.size.body
                                color: Ui.text2
                            }
                        }
                        Text {
                            visible: thread.store.thread.length === 0
                            text: "no replies yet"
                            font.family: Ui.sans
                            font.pixelSize: Ui.size.ui
                            color: Ui.text3
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: outline.implicitHeight
                            visible: thread.store.thread.length > 0
                            Rectangle { width: 1; height: parent.height; color: Ui.borderStrong }
                            Column {
                                id: outline
                                width: parent.width
                                Repeater {
                                    // Top-level replies: the sub-threads.
                                    model: {
                                        var out = [];
                                        var list = thread.store.thread;
                                        for (var i = 0; i < list.length; ++i) {
                                            if (list[i].depth !== 0) continue;
                                            var n = 1;
                                            for (var j = i + 1; j < list.length && list[j].depth > 0; ++j) ++n;
                                            out.push({ id: list[i].id, label: list[i].author + " · " + n });
                                        }
                                        return out;
                                    }
                                    delegate: Item {
                                        required property var modelData
                                        required property int index
                                        width: outline.width
                                        height: 32
                                        Rectangle {
                                            visible: index === 0
                                            width: 2
                                            height: parent.height
                                            color: Ui.text
                                        }
                                        Text {
                                            x: 14
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 14
                                            text: modelData.label
                                            elide: Text.ElideRight
                                            font.family: Ui.sans
                                            font.pixelSize: Ui.size.ui
                                            color: railMouse.containsMouse || index === 0 ? Ui.text : Ui.text2
                                        }
                                        MouseArea {
                                            id: railMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: thread.scrollTo(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: thread.store.participants.length > 0
                        Text {
                            text: "people"
                            font.family: Ui.sans
                            font.pixelSize: Ui.size.body
                            color: Ui.text2
                        }
                        Repeater {
                            model: thread.store.participants
                            delegate: Text {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                text: modelData + (index === 0 ? "  op" : "")
                                elide: Text.ElideRight
                                font.family: Ui.mono
                                font.pixelSize: Ui.size.caption
                                color: Ui.text2
                            }
                        }
                    }
                }
            }
        }
    }

    // Off-screen helper for "copy id" (QML has no clipboard API of its own).
    TextEdit { id: clipboard; visible: false }
}
