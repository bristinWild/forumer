import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// The 24-word recovery phrase, full screen (after creating an identity, or
// "show phrase" in backup). A screen, not a popup: this is the one thing the
// user must not skim past.
//
// Copy: the phrase goes on the system clipboard, which other apps can read.
// The clipboard is overwritten with blank text 60 seconds after copying, and
// again when this screen closes. QML can't read the clipboard, so the clear
// is unconditional: anything else copied in that minute is cleared too.
Rectangle {
    id: phraseScreen

    property string phrase: ""
    property bool copied: false
    signal done()

    readonly property int clearAfterMs: 60000

    color: Ui.bg

    MouseArea { anchors.fill: parent }

    // Off-screen helper for the clipboard (QML has no clipboard API of its
    // own; the same approach HomeScreen uses for "copy link").
    TextEdit { id: clipboard; visible: false; textFormat: TextEdit.PlainText }

    Timer {
        id: clearTimer
        interval: phraseScreen.clearAfterMs
        repeat: false
        onTriggered: phraseScreen.clearClipboard()
    }

    function copyPhrase() {
        if (phraseScreen.phrase.length === 0)
            return;
        clipboard.text = phraseScreen.phrase;
        clipboard.selectAll();
        clipboard.copy();
        clipboard.text = "";
        phraseScreen.copied = true;
        clearTimer.restart();
    }

    function clearClipboard() {
        clearTimer.stop();
        if (!phraseScreen.copied)
            return;
        clipboard.text = " ";
        clipboard.selectAll();
        clipboard.copy();
        clipboard.text = "";
        phraseScreen.copied = false;
    }

    Component.onDestruction: clearClipboard()

    B.ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: col.implicitHeight + 160
        clip: true

        Item {
            width: scroller.availableWidth
            height: Math.max(scroller.height, col.implicitHeight + 160)

            ColumnLayout {
                id: col
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 680)
                spacing: 24

                Text {
                    textFormat: Text.PlainText
                    text: "your recovery phrase"
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.thread
                    font.weight: Ui.weight.bold
                    font.letterSpacing: -0.6
                    color: Ui.text
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: "write these 24 words down, in order, and keep them somewhere safe and offline. they are the only way back in if you forget your password or lose this device. anyone who sees them can post as you."
                    wrapMode: Text.WordWrap
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.reading
                    lineHeight: 1.4
                    color: Ui.text2
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: grid.implicitHeight + 40
                    radius: Ui.radius.xl
                    color: Ui.surface
                    border.width: 1
                    border.color: Ui.borderStrong

                    GridLayout {
                        id: grid
                        anchors.fill: parent
                        anchors.margins: 20
                        columns: col.width > 560 ? 4 : 3
                        rowSpacing: 14
                        columnSpacing: 16
                        Repeater {
                            model: phraseScreen.phrase.length > 0 ? phraseScreen.phrase.split(" ") : []
                            delegate: Row {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    textFormat: Text.PlainText
                                    width: 22
                                    horizontalAlignment: Text.AlignRight
                                    text: (index + 1)
                                    font.family: Ui.mono
                                    font.pixelSize: Ui.size.caption
                                    color: Ui.text3
                                    anchors.baseline: word.baseline
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    id: word
                                    text: modelData
                                    font.family: Ui.mono
                                    font.pixelSize: Ui.size.reading
                                    color: Ui.text
                                }
                            }
                        }
                    }
                }

                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: phraseScreen.copied
                          ? "copied. other apps can read the clipboard, so paste it only where you mean to. it clears in 60 seconds."
                          : "copying puts all 24 words on the clipboard, where other apps can read them."
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.caption
                    lineHeight: 1.4
                    color: Ui.text3
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Item { Layout.fillWidth: true }
                    FButton {
                        kind: "outline"
                        implicitHeight: 44
                        text: phraseScreen.copied ? "copied" : "copy phrase"
                        enabled: phraseScreen.phrase.length > 0
                        onClicked: phraseScreen.copyPhrase()
                    }
                    FButton {
                        kind: "primary"
                        implicitHeight: 44
                        text: "i've written it down"
                        icon: "check"
                        onClicked: {
                            phraseScreen.clearClipboard();
                            phraseScreen.done();
                        }
                    }
                }
            }
        }
    }
}
