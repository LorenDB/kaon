import QtQuick

import dev.lorendb.kaon

// Soft cross-fade between visor pages. Enabled only while active so a fading page does not steal input.
Item {
    id: layer

    property bool active: false
    // Cold start should not fade the first page in from nothing.
    property bool animate: false
    property real shown: active ? 1 : 0

    anchors.fill: parent
    enabled: active
    focus: active
    opacity: shown
    scale: 0.985 + 0.015 * shown
    transformOrigin: Item.Center
    visible: shown > 0.01
    z: active ? 1 : 0

    Behavior on shown {
        enabled: layer.animate

        NumberAnimation {
            duration: Theme.durationMed
            easing.type: Theme.easeInOut
        }
    }

    Component.onCompleted: Qt.callLater(() => {
                                            layer.animate = true;
                                        })
}
