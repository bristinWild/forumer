import QtQuick
import QtQuick.Shapes

// A small stroke icon drawn on a 24×24 grid (the style of Lucide/Feather),
// scaled to `size`. Stroke-only, so it takes any colour.
//   Icon { name: "search"; size: 16; color: Ui.text3 }
Item {
    id: icon

    property string name: ""
    property int size: 16
    property color color: Ui.text2
    property real strokeWidth: 1.8

    implicitWidth: size
    implicitHeight: size

    // Multisampled layer at the icon's real pixel size: smooth edges on every
    // Qt 6 release (the curve renderer is 6.6+).
    layer.enabled: true
    layer.samples: 8

    // SVG path data per icon. Circles are written as two arcs.
    readonly property var paths: ({
        "search":   "M 18 11 A 7 7 0 1 1 4 11 A 7 7 0 1 1 18 11 M 20 20 L 16.5 16.5",
        "plus":     "M 12 5 L 12 19 M 5 12 L 19 12",
        "x":        "M 6 6 L 18 18 M 18 6 L 6 18",
        "home":     "M 3 11 L 12 4 L 21 11 M 5 10 L 5 20 L 19 20 L 19 10",
        "inbox":    "M 22 12 L 16 12 L 14 15 L 10 15 L 8 12 L 2 12 M 5.5 5 L 2 12 L 2 18 A 2 2 0 0 0 4 20 L 20 20 A 2 2 0 0 0 22 18 L 22 12 L 18.5 5 Z",
        "pen":      "M 12 20 L 21 20 M 16.5 3.5 A 2.1 2.1 0 0 1 19.5 6.5 L 7 19 L 3 20 L 4 16 Z",
        "chat":     "M 21 11.5 A 8.4 8.4 0 0 1 12.5 20 A 8.4 8.4 0 0 1 8.6 19.1 L 3 21 L 4.9 15.4 A 8.4 8.4 0 0 1 4 11.5 A 8.5 8.5 0 0 1 12.5 3 A 8.4 8.4 0 0 1 21 11.5 Z",
        "user":     "M 16 8 A 4 4 0 1 1 8 8 A 4 4 0 1 1 16 8 M 4 21 C 5.5 17 8.5 15 12 15 C 15.5 15 18.5 17 20 21",
        "settings": "M 15 12 A 3 3 0 1 1 9 12 A 3 3 0 1 1 15 12 M 12 2 L 12 5 M 12 19 L 12 22 M 4.9 4.9 L 7 7 M 17 17 L 19.1 19.1 M 2 12 L 5 12 M 19 12 L 22 12 M 4.9 19.1 L 7 17 M 17 7 L 19.1 4.9",
        "lock":     "M 7 11 L 17 11 A 2 2 0 0 1 19 13 L 19 19 A 2 2 0 0 1 17 21 L 7 21 A 2 2 0 0 1 5 19 L 5 13 A 2 2 0 0 1 7 11 Z M 8 11 L 8 7 A 4 4 0 0 1 16 7 L 16 11",
        "key":      "M 10 15 A 4 4 0 1 1 2 15 A 4 4 0 1 1 10 15 M 9 12 L 20 3 M 16.5 6 L 19 8.5 M 14 8 L 16 10",
        "sun":      "M 16 12 A 4 4 0 1 1 8 12 A 4 4 0 1 1 16 12 M 12 2 L 12 4 M 12 20 L 12 22 M 4.9 4.9 L 6.3 6.3 M 17.7 17.7 L 19.1 19.1 M 2 12 L 4 12 M 20 12 L 22 12 M 4.9 19.1 L 6.3 17.7 M 17.7 6.3 L 19.1 4.9",
        "moon":     "M 21 12.8 A 9 9 0 1 1 11.2 3 A 7 7 0 0 0 21 12.8 Z",
        "sidebar":  "M 5 4 L 19 4 A 2 2 0 0 1 21 6 L 21 18 A 2 2 0 0 1 19 20 L 5 20 A 2 2 0 0 1 3 18 L 3 6 A 2 2 0 0 1 5 4 Z M 9 4 L 9 20",
        "refresh":  "M 21 4 L 21 10 L 15 10 M 20.5 15 A 9 9 0 1 1 18.4 5.6 L 21 10",
        "list":     "M 4 6 L 20 6 M 4 12 L 16 12 M 4 18 L 12 18",
        "chevron-down": "M 6 9 L 12 15 L 18 9",
        "chevron-left": "M 15 6 L 9 12 L 15 18",
        "reply":    "M 9 14 L 4 9 L 9 4 M 20 20 L 20 13 A 4 4 0 0 0 16 9 L 4 9",
        "link":     "M 10 13 A 5 5 0 0 0 17.5 13.5 L 20.5 10.5 A 5 5 0 0 0 13.5 3.5 L 11.8 5.2 M 14 11 A 5 5 0 0 0 6.5 10.5 L 3.5 13.5 A 5 5 0 0 0 10.5 20.5 L 12.2 18.8",
        "alert":    "M 12 3 L 22 20 L 2 20 Z M 12 9 L 12 13 M 12 16.5 L 12 17",
        "check":    "M 4 12 L 9 17 L 20 6"
    })

    Shape {
        width: 24
        height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft

        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
