import QtQuick
import QtQuick.Shapes

// Small line-icon set drawn on a 24-unit grid, so every concept can restyle stroke weight and color.
Item {
    id: root

    property color color: "white"
    readonly property var filled: ["play", "stop"]
    readonly property bool isFilled: filled.indexOf(name) >= 0
    property string name
    readonly property var paths: ({
                                      "play": "M8.5 5.8 Q7.5 5.2 7.5 6.4 V17.6 Q7.5 18.8 8.5 18.2 L18 12.6 Q19 12 18 11.4 Z",
                                      "back": "M14.5 5 L7.5 12 L14.5 19",
                                      "forward": "M9.5 5 L16.5 12 L9.5 19",
                                      "down": "M6 9.5 L12 15.5 L18 9.5",
                                      "up": "M6 14.5 L12 8.5 L18 14.5",
                                      "search": "M10.5 4.5 A6 6 0 1 1 10.49 4.5 Z M15 15 L20 20",
                                      "folder": "M3.5 6.5 Q3.5 5.5 4.5 5.5 H9 L11 7.5 H19.5 Q20.5 7.5 20.5 8.5 V17.5 Q20.5 18.5 19.5 18.5 H4.5 Q3.5 18.5 3.5 17.5 Z",
                                      "sliders": "M4 7 H11 M15 7 H20 M13 4.8 V9.2 M4 17 H8 M12 17 H20 M10 14.8 V19.2",
                                      "check": "M5.5 12.5 L10 17 L18.5 7.5",
                                      "x": "M6.5 6.5 L17.5 17.5 M17.5 6.5 L6.5 17.5",
                                      "plus": "M12 5 V19 M5 12 H19",
                                      "minus": "M5 12 H19",
                                      "download": "M12 4.5 V14.5 M7.5 10.5 L12 15 L16.5 10.5 M5 19.5 H19",
                                      "trash": "M4.5 7 H19.5 M9.5 7 V4.8 H14.5 V7 M6.5 7 L7.5 19.5 H16.5 L17.5 7",
                                      "refresh": "M19 12 A7 7 0 1 1 16.9 7 M17.5 3.5 V7.5 H13.5",
                                      "external": "M13.5 4.5 H19.5 V10.5 M19.5 4.5 L11 13 M17 14 V19.5 H4.5 V7 H10",
                                      "gear": "M12 8.6 A3.4 3.4 0 1 1 11.99 8.6 Z M12 2.8 V5.2 M12 18.8 V21.2 M2.8 12 H5.2 M18.8 12 H21.2 M5.5 5.5 L7.2 7.2 M16.8 16.8 L18.5 18.5 M5.5 18.5 L7.2 16.8 M16.8 7.2 L18.5 5.5",
                                      "headset":
                                      "M4.5 7.5 H19.5 Q21.5 7.5 21.5 9.5 V14.5 Q21.5 16.5 19.5 16.5 H14.6 Q13.7 16.5 13.3 15.3 Q12.9 14 12 14 Q11.1 14 10.7 15.3 Q10.3 16.5 9.4 16.5 H4.5 Q2.5 16.5 2.5 14.5 V9.5 Q2.5 7.5 4.5 7.5 Z",
                                      "warn": "M12 4.5 L21 19.5 H3 Z M12 10 V14 M12 16.8 V17",
                                      "info": "M12 3.5 A8.5 8.5 0 1 1 11.99 3.5 Z M12 11 V16.5 M12 7.8 V8",
                                      "grid": "M4.5 4.5 H10.5 V10.5 H4.5 Z M13.5 4.5 H19.5 V10.5 H13.5 Z M4.5 13.5 H10.5 V19.5 H4.5 Z M13.5 13.5 H19.5 V19.5 H13.5 Z",
                                      "list": "M4.5 6.5 H19.5 M4.5 12 H19.5 M4.5 17.5 H19.5",
                                      "more": "M6 12 H6.01 M12 12 H12.01 M18 12 H18.01",
                                      "copy": "M9.5 9.5 H18.5 V19.5 H9.5 Z M6 15 H5.5 V4.5 H14.5 V6",
                                      "stop": "M7 7 H17 V17 H7 Z"
                                  })
    property real size: 20
    property real stroke: 1.8

    implicitHeight: size
    implicitWidth: size

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            capStyle: ShapePath.RoundCap
            fillColor: root.isFilled ? root.color : "transparent"
            joinStyle: ShapePath.RoundJoin
            scale: Qt.size(root.size / 24, root.size / 24)
            strokeColor: root.isFilled ? "transparent" : root.color
            strokeWidth: root.isFilled ? 0 : root.stroke

            PathSvg {
                path: root.paths[root.name] ?? ""
            }
        }
    }
}
