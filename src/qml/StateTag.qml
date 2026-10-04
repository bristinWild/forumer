import QtQuick

// Delivery state of one of our posts, in mono with a coloured dot:
// "● live", "● sending…", "● failed to publish · retrying". Hidden for posts
// that aren't ours (state "").
Row {
    id: tag

    property string delivery: ""
    property string detail: ""      // appended after a " · " when set

    visible: delivery.length > 0
    spacing: 6

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 6
        height: 6
        radius: 3
        color: Ui.deliveryColor(tag.delivery)
    }
    Text {
        textFormat: Text.PlainText
        anchors.verticalCenter: parent.verticalCenter
        text: Ui.deliveryLabel(tag.delivery) + (tag.detail.length > 0 ? " · " + tag.detail : "")
        font.family: Ui.mono
        font.pixelSize: Ui.size.caption
        color: tag.delivery === "failed" ? Ui.failedText : Ui.deliveryColor(tag.delivery)
    }
}
