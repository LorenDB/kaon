import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// A short menu for actions that don't earn a button of their own.
Popup {
    id: menu

    // [{ icon, text, run }]. Entries that are null are left out, so a caller can write `condition ? {...} : null`.
    property var actions: []

    function focusFirst() {
        const first = rows.itemAt(0);
        if (first)
            first.forceActiveFocus();
    }

    margins: 8
    padding: 6
    width: 240

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.durationFast
                easing.type: Theme.easeOut
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1
                duration: Theme.durationMed
                easing.type: Theme.easeOut
            }
        }
    }
    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.durationFast
                easing.type: Theme.easeIn
            }
            NumberAnimation {
                property: "scale"
                from: 1
                to: 0.98
                duration: Theme.durationFast
                easing.type: Theme.easeIn
            }
        }
    }

    background: Rectangle {
        border.color: Theme.glassLine
        border.width: 1.5
        color: Theme.glassPanel
        radius: 16
    }
    contentItem: Column {
        spacing: 2

        Repeater {
            id: rows

            model: menu.actions.filter(action => !!action)

            Item {
                id: row

                required property var modelData

                function trigger() {
                    menu.close();
                    modelData.run();
                }

                activeFocusOnTab: true
                height: 38
                width: parent.width

                Keys.onReturnPressed: trigger()
                Keys.onSpacePressed: trigger()

                Rectangle {
                    anchors.fill: parent
                    color: rowMouse.containsMouse || row.activeFocus ? Theme.glassRaised : "transparent"
                    radius: 11
                }

                Rectangle {
                    anchors.fill: parent
                    border.color: Theme.ledBlue
                    border.width: 2
                    color: "transparent"
                    radius: 11
                    visible: row.activeFocus
                }

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.glassMuted
                    name: row.modelData.icon ?? ""
                    size: 16
                    stroke: 2
                    x: 12
                }

                VText {
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    text: row.modelData.text
                    width: parent.width - x - 12
                    x: 38
                }

                MouseArea {
                    id: rowMouse

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: row.trigger()
                }
            }
        }
    }

    Component.onDestruction: if (visible)
                                 Nav.popupShown(false)
    onOpened: Qt.callLater(menu.focusFirst)
    onVisibleChanged: Nav.popupShown(visible)
}
