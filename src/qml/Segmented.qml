import QtQuick

// A row of mutually exclusive options in one box - "post as", rotation.
//   Segmented {
//       options: [{ value: "persona", label: "persona" }, …]
//       value: "persona"
//       onPicked: (v) => …
//   }
Rectangle {
    id: seg

    property var options: []
    property string value: ""
    signal picked(string value)

    implicitHeight: 46
    implicitWidth: 360
    radius: Ui.radius.l
    color: Ui.field
    border.width: 1
    border.color: Ui.borderStrong

    Accessible.role: Accessible.PageTabList

    Row {
        id: row
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        Repeater {
            model: seg.options
            delegate: Rectangle {
                id: opt
                required property var modelData
                readonly property bool on: modelData.value === seg.value
                width: (row.width - row.spacing * (seg.options.length - 1)) / Math.max(1, seg.options.length)
                height: row.height
                radius: Ui.radius.m
                color: on ? Ui.borderStrong : (optMouse.containsMouse ? Ui.hover : "transparent")
                border.width: activeFocus ? 1 : 0
                border.color: Ui.focus
                activeFocusOnTab: true

                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: on

                Text {
                    anchors.centerIn: parent
                    text: opt.modelData.label
                    font.family: Ui.sans
                    font.pixelSize: Ui.size.ui
                    color: opt.on ? Ui.text : Ui.text2
                }
                MouseArea {
                    id: optMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: seg.picked(opt.modelData.value)
                }
                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                        seg.picked(opt.modelData.value);
                        event.accepted = true;
                    }
                }
            }
        }
    }
}
