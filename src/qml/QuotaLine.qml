import QtQuick

// What's left of the hourly posting limit, under a composer:
//   ▮▮▮▮▮▮▮▯▯▯  7 of 10 topics left this hour
//   ▯▯▯▯▯▯▯▯▯▯  limit reached · next topic in 23 min
// Quiet grey normally, amber when 2 or fewer are left, red at 0.
Row {
    id: line

    property var quota: ({ left: 0, max: 0, waitMin: 0 })
    property string noun: "topic"          // singular
    property string nounPlural: noun + "s"

    readonly property int remaining: quota && quota.left !== undefined ? quota.left : 0
    readonly property int limit: quota && quota.max ? quota.max : 0
    readonly property color tone: remaining === 0 ? Ui.failedText : remaining <= 2 ? Ui.sending : Ui.text3

    visible: limit > 0
    spacing: 10

    // A thin meter: one segment per post when the limit is small, a bar otherwise.
    Item {
        anchors.verticalCenter: parent.verticalCenter
        width: 72
        height: 6

        Row {
            visible: line.limit <= 12
            anchors.fill: parent
            spacing: 2
            Repeater {
                model: line.limit <= 12 ? line.limit : 0
                delegate: Rectangle {
                    required property int index
                    width: (72 - 2 * (line.limit - 1)) / Math.max(1, line.limit)
                    height: 6
                    radius: 2
                    color: index < line.remaining ? line.tone : Ui.borderStrong
                }
            }
        }
        Rectangle {
            visible: line.limit > 12
            anchors.fill: parent
            radius: 3
            color: Ui.borderStrong
            Rectangle {
                width: parent.width * (line.limit > 0 ? line.remaining / line.limit : 0)
                height: parent.height
                radius: 3
                color: line.tone
            }
        }
    }

    Text {
        textFormat: Text.PlainText
        anchors.verticalCenter: parent.verticalCenter
        text: line.remaining > 0
              ? line.remaining + " of " + line.limit + " " + line.nounPlural + " left this hour"
              : "limit reached · next " + line.noun + " in " + Math.max(1, line.quota.waitMin || 0) + " min"
        font.family: Ui.mono
        font.pixelSize: Ui.size.caption
        color: line.tone
    }
}
