import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts

// Scaffold for the simple screens (missed, my posts, profile, settings…):
// a scrolling page with a big lowercase title, a one-line subtitle, and the
// screen's sections stacked below at reading width.
//   Page { title: "missed"; subtitle: "…"; Section {…} Section {…} }
Item {
    id: page

    property string title: ""
    property string subtitle: ""
    property int maxWidth: 860
    default property alias content: body.data

    B.ScrollView {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            x: 64
            width: Math.min(page.maxWidth, scroller.availableWidth - 128)
            spacing: 44

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.page
                spacing: 10
                Text {
                    textFormat: Text.PlainText
                    text: page.title
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.page
                    font.weight: Ui.weight.bold
                    font.letterSpacing: -0.9
                    color: Ui.text
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: page.subtitle.length > 0
                    text: page.subtitle
                    wrapMode: Text.WordWrap
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.reading
                    color: Ui.text2
                }
            }

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                Layout.bottomMargin: 64
                spacing: 44
            }
        }
    }
}
