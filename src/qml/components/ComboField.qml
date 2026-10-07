import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// A closed list, drawn like a text field. options is [{ id, label }].
Item {
    id: box

    property string current
    property var options: []
    // Shown on the closed field when current is empty and not one of the options.
    property string placeholder: ""

    signal picked(string key)

    function labelFor(id) {
        for (let i = 0; i < box.options.length; ++i) {
            if (box.options[i].id === id)
                return box.options[i].label;
        }
        if (id === "" && box.placeholder !== "")
            return box.placeholder;
        return id;
    }

    function openList() {
        if (!pop.opened)
            pop.open();
    }

    activeFocusOnTab: true
    implicitHeight: 38
    implicitWidth: Math.max(120, caption.implicitWidth + 44)

    Keys.onReturnPressed: openList()
    Keys.onSpacePressed: openList()

    Rectangle {
        anchors.fill: parent
        border.color: box.activeFocus || pop.opened ? Theme.glassMuted : Theme.glassLine
        border.width: 1.5
        color: Theme.glassPanel
        radius: 12
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        border.color: Theme.ledBlue
        border.width: 2
        color: "transparent"
        radius: 14
        visible: box.activeFocus && !pop.opened
    }

    VText {
        id: caption

        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.right: caret.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        color: box.current === "" ? Theme.glassFaint : Theme.glassText
        elide: Text.ElideRight
        font.pixelSize: 14
        text: box.labelFor(box.current)
    }

    Icon {
        id: caret

        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.glassMuted
        name: "down"
        size: 14
        stroke: 2.2
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: box.openList()
    }

    Popup {
        id: pop

        // The popup sizes its content item itself. Binding the list's height to contentHeight
        // collapses that to nothing, and clip then hides every row.
        readonly property int rowHeight: 34

        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        height: Math.min(list.count * rowHeight + Math.max(0, list.count - 1) * list.spacing, 280) + topPadding
                + bottomPadding

        margins: 8
        padding: 6
        width: Math.max(box.width, 168)
        y: box.height + 6

        background: Rectangle {
            border.color: Theme.glassLine
            border.width: 1.5
            color: Theme.glassPanel
            radius: 16
        }
        contentItem: ListView {
            id: list

            clip: true
            model: box.options
            spacing: 2

            ScrollBar.vertical: GlassScrollBar {
            }
            delegate: Item {
                id: opt

                required property int index
                required property var modelData

                function choose() {
                    pop.close();
                    box.picked(opt.modelData.id);
                }

                activeFocusOnTab: true
                height: pop.rowHeight
                width: list.width

                Keys.onReturnPressed: choose()
                Keys.onSpacePressed: choose()

                Rectangle {
                    anchors.fill: parent
                    color: opt.modelData.id === box.current || opt.activeFocus ? Theme.glassRaised : optMouse.containsMouse
                                                                                 ? Theme.glassHover : "transparent"
                    radius: 10
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    border.color: Theme.ledBlue
                    border.width: 2
                    color: "transparent"
                    radius: 10
                    visible: opt.activeFocus
                }

                VText {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    font.weight: opt.modelData.id === box.current ? Font.Bold : Font.Medium
                    text: opt.modelData.label
                }

                MouseArea {
                    id: optMouse

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: opt.choose()
                }
            }
        }

        Component.onDestruction: if (visible)
                                     Nav.popupShown(false)
        onOpened: Qt.callLater(() => {
            let at = 0;
            for (let i = 0; i < box.options.length; ++i) {
                if (box.options[i].id === box.current) {
                    at = i;
                    break;
                }
            }
            list.positionViewAtIndex(at, ListView.Contain);
            const item = list.itemAtIndex(at);
            if (item)
                item.forceActiveFocus();
        })
        onVisibleChanged: Nav.popupShown(visible)
    }
}
