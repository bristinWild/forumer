import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// The 24-word recovery phrase, full screen (after creating an identity, or
// "show phrase" in backup). A screen, not a popup: this is the one thing the
// user must not skim past.
Rectangle {
    id: phraseScreen

    property string phrase: ""
    signal done()

    color: Ui.bg

    MouseArea { anchors.fill: parent }

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
                    text: "your recovery phrase"
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.thread
                    font.weight: Ui.weight.bold
                    font.letterSpacing: -0.6
                    color: Ui.text
                }
                Text {
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
                                    width: 22
                                    horizontalAlignment: Text.AlignRight
                                    text: (index + 1)
                                    font.family: Ui.mono
                                    font.pixelSize: Ui.size.caption
                                    color: Ui.text3
                                    anchors.baseline: word.baseline
                                }
                                Text {
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

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    FButton {
                        kind: "primary"
                        implicitHeight: 44
                        text: "i've written it down"
                        icon: "check"
                        onClicked: phraseScreen.done()
                    }
                }
            }
        }
    }
}
