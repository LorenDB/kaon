import QtQuick

import dev.lorendb.kaon

// Optional tools for one game, one switch each. The list stays folded until someone wants it, so a growing number
// of tools doesn't push everything else down the page. Their downloads are on the Mods page.
Column {
    id: panel

    property Game game: null
    readonly property int onCount: tools.filter(tool => tool.on).length
    // Rows from GameStatus.tools()
    property var tools: []

    spacing: 4
    visible: tools.length > 0
    width: parent ? parent.width : 0

    // Open/closed is remembered per game so leaving one open doesn't open the next.
    readonly property string gameKey: game ? (String(game.store) + "/" + game.id) : ""
    property bool toolsOpen: false
    property var toolsOpenByGame: ({})

    onGameKeyChanged: toolsOpen = gameKey !== "" && toolsOpenByGame[gameKey] === true

    function setToolsOpen(open) {
        toolsOpen = open;
        if (gameKey === "")
            return;
        const next = Object.assign({}, toolsOpenByGame);
        next[gameKey] = open;
        toolsOpenByGame = next;
    }

    Item {
        id: heading

        activeFocusOnTab: true
        height: 32
        width: parent.width

        Keys.onReturnPressed: panel.setToolsOpen(!panel.toolsOpen)
        Keys.onSpacePressed: panel.setToolsOpen(!panel.toolsOpen)

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -8
            anchors.rightMargin: -8
            border.color: heading.activeFocus ? Theme.ledBlue : "transparent"
            border.width: 2
            color: headingMouse.containsMouse ? Theme.glassHover : "transparent"
            radius: height / 2
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassFaint
            name: "forward"
            rotation: panel.toolsOpen ? 90 : 0
            size: 14
            stroke: 2.2

            Behavior on rotation {
                NumberAnimation {
                    duration: Theme.durationFast
                    easing.type: Theme.easeOut
                }
            }
        }

        VText {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassFaint
            font.pixelSize: 12
            font.weight: Font.Bold
            text: "Optional tools"
            x: 22
        }

        // Folded, the heading still says whether anything in it is switched on
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 7
            visible: !panel.toolsOpen

            Led {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ledGreen
                size: 7
                visible: panel.onCount > 0
            }

            VText {
                anchors.verticalCenter: parent.verticalCenter
                color: panel.onCount > 0 ? Theme.glassMuted : Theme.glassFaint
                font.pixelSize: 12
                text: panel.onCount === 0 ? panel.tools.length + " available" : panel.onCount === 1 ? panel.tools.find(tool
                                                                                                                       => tool.on).mod.name
                                                                                                      + " is on" :
                                                                                                      panel.onCount + " on"
            }
        }

        MouseArea {
            id: headingMouse

            anchors.fill: parent
            anchors.leftMargin: -8
            anchors.rightMargin: -8
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: panel.setToolsOpen(!panel.toolsOpen)
        }
    }

    Item {
        id: body

        clip: true
        enabled: panel.toolsOpen
        height: panel.toolsOpen ? toolsCol.height : 0
        opacity: panel.toolsOpen ? 1 : 0
        width: parent.width

        Behavior on height {
            NumberAnimation {
                duration: Theme.durationMed
                easing.type: Theme.easeInOut
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationFast
                easing.type: Theme.easeOut
            }
        }

        Column {
            id: toolsCol

            spacing: 4
            width: parent.width

            Repeater {
                model: panel.tools

                Item {
                    id: row

                    required property var modelData
                    // What the tool is doing here when there is something to say, and otherwise what it is for
                    readonly property string note: modelData.detail !== "" ? modelData.detail : modelData.mod.description

                    height: Math.max(labels.height, toggle.height) + 12
                    width: toolsCol.width

                    Column {
                        id: labels

                        anchors.left: parent.left
                        anchors.right: toggle.left
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Row {
                            width: parent.width

                            VText {
                                elide: Text.ElideRight
                                font.pixelSize: 14
                                font.weight: Font.Bold
                                text: row.modelData.mod.name
                                width: Math.min(implicitWidth, parent.width - runtime.implicitWidth)
                            }

                            VText {
                                id: runtime

                                color: Theme.glassMuted
                                font.pixelSize: 14
                                font.weight: Font.Normal
                                text: row.modelData.mod.vrRuntime !== "" ? " · " + row.modelData.mod.vrRuntime : ""
                                visible: text !== ""
                            }
                        }

                        VText {
                            color: row.modelData.detail !== "" ? Theme.glassMuted : Theme.glassFaint
                            font.pixelSize: 12
                            lineHeight: 1.2
                            text: row.note
                            visible: text !== ""
                            width: parent.width
                            wrapMode: Text.Wrap
                        }
                    }

                    ToggleSwitch {
                        id: toggle

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: row.modelData.on
                        enabled: row.modelData.enabled

                        onToggled: GameStatus.toggleTool(panel.game, row.modelData.mod)
                    }
                }
            }
        }
    }
}
