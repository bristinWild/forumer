import QtQuick
import QtQuick.Layouts

// One column of the home feed: a header line, then topic rows. The parent
// scrolls; this just lays the rows out at full height.
ColumnLayout {
    id: column

    property string title: ""
    property string caption: ""        // right of the title, mono
    property var items: []
    property string emptyText: ""
    signal openTopic(string id)

    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 12
        Text {
            text: column.title
            font.family: Ui.sans
            font.pixelSize: Ui.size.body
            font.weight: Ui.weight.semibold
            color: Ui.text
        }
        Item { Layout.fillWidth: true }
        Text {
            Layout.maximumWidth: column.width * 0.6
            text: column.caption
            elide: Text.ElideLeft
            font.family: Ui.mono
            font.pixelSize: Ui.size.caption
            color: Ui.text3
        }
    }
    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Ui.border }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: 20
        visible: column.items.length === 0
        text: column.emptyText
        wrapMode: Text.WordWrap
        font.family: Ui.sans
        font.pixelSize: Ui.size.ui
        color: Ui.text3
        lineHeight: 1.3
    }

    Repeater {
        model: column.items
        delegate: TopicRow {
            required property var modelData
            Layout.fillWidth: true
            item: modelData
            onClicked: column.openTopic(modelData.id)
        }
    }
}
