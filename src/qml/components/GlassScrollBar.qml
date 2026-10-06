import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// A thin scroll bar for the black visor that widens while you hold it.
ScrollBar {
    id: bar

    background: null
    // The visor's corners are round, and a bar that ran into them would be cut off at an angle
    bottomPadding: 12
    minimumSize: 0.06
    padding: 3
    topPadding: 12

    contentItem: Rectangle {
        color: bar.pressed ? Theme.glassMuted : bar.hovered ? Theme.glassFaint : Theme.glassLine
        implicitWidth: bar.hovered || bar.pressed ? 8 : 5
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.size < 1 ? 1 : 0
        radius: width / 2

        Behavior on implicitWidth {
            NumberAnimation {
                duration: 120
            }
        }
    }
}
