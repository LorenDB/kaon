import QtQuick
import QtQuick.Effects

// Image clipped to a rounded rectangle, the same MultiEffect mask trick Kaon already uses for covers.
Item {
    id: root

    // Lets callers shift the picture inside the clip, which is how the parallax hero works.
    property real contentOffsetX: 0
    property real contentOffsetY: 0
    property real contentScale: 1
    property alias fillMode: img.fillMode
    property alias horizontalAlignment: img.horizontalAlignment
    property real radius: 10
    readonly property bool ready: img.status === Image.Ready
    property alias source: img.source
    property alias sourceSize: img.sourceSize
    property alias verticalAlignment: img.verticalAlignment

    Item {
        id: holder

        anchors.fill: parent
        layer.enabled: true
        visible: false

        Image {
            id: img

            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            height: parent.height * root.contentScale
            mipmap: true
            width: parent.width * root.contentScale
            x: (parent.width - width) / 2 + root.contentOffsetX
            y: (parent.height - height) / 2 + root.contentOffsetY
        }
    }

    Rectangle {
        id: mask

        anchors.fill: parent
        layer.enabled: true
        layer.smooth: true
        radius: root.radius
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        maskEnabled: true
        maskSource: mask
        maskSpreadAtMin: 1.0
        maskThresholdMin: 0.5
        source: holder
        visible: root.ready
    }
}
