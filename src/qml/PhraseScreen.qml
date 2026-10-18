import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// The 24-word recovery phrase, full screen (after creating an identity, or
// "show phrase" in backup). A screen, not a popup: this is the one thing the
// user must not skim past.
//
// Copy: the phrase goes on the system clipboard, which other apps can read.
// Sixty seconds after copying, and when this screen closes, the clipboard is
// cleared, but only if it still holds this phrase: anything the user copied
// since is left alone. To check, the clipboard's text is pasted into a hidden
// field for a moment and compared; it is not logged or kept.
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

    // Whitespace-insensitive, so a phrase that was reflowed still matches.
    function sameText(a, b) {
        const norm = function (s) { return s.replace(/\s+/g, " ").trim(); };
        return norm(a).length > 0 && norm(a) === norm(b);
    }

    // What the clipboard holds right now, read through the hidden field.
    function clipboardText() {
        clipboard.text = "";
        clipboard.paste();
        const now = clipboard.text;
        clipboard.text = "";
        return now;
    }

    function clearClipboard() {
        clearTimer.stop();
        if (!phraseScreen.copied)
            return;
        phraseScreen.copied = false;
        // Only overwrite what is still our phrase; leave anything else alone.
        if (!sameText(clipboardText(), phraseScreen.phrase))
            return;
        clipboard.text = " ";
        clipboard.selectAll();
        clipboard.copy();
        clipboard.text = "";
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
                          ? "copied. the clipboard clears in 60 seconds, but clipboard managers may keep a copy. paste only where you intend."
                          : "copying puts all 24 words on the clipboard. clipboard managers may keep a copy even after it is cleared."
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
