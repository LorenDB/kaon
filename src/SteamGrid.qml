import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

pragma ComponentBehavior: Bound

import dev.lorendb.kaon

RowLayout {
    id: gridRoot

    signal gameClicked(int steamId)
    spacing: 10

    SystemPalette { id: palette }

    Frame {
        id: frame

        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            // spacing: 10
            anchors.fill: parent

            TextField {
                Layout.fillWidth: true
                placeholderText: "Search..."
                onTextChanged: SteamFilter.search = text
            }

            GridView {
                id: grid

                readonly property int cardWidth: 120
                readonly property int cardHeight: 180

                Layout.fillWidth: true
                Layout.fillHeight: true
                model: SteamFilter
                cellWidth: {
                    let usableWidth = width - (leftMargin + rightMargin + sb.width)
                    return usableWidth / Math.floor(usableWidth / (cardWidth * 1.1))
                }
                cellHeight: cardHeight * 1.1
                bottomMargin: 15
                topMargin: 15
                leftMargin: 15
                rightMargin: 15
                clip: true

                ScrollBar.vertical: ScrollBar { id: sb }
                delegate: Item {
                    id: card

                    required property string name
                    required property int steamId
                    required property string cardImage

                    width: grid.cellWidth
                    height: grid.cardHeight
                    scale: ma.containsMouse ? 1.07 : 1.0
                    ToolTip.text: name
                    ToolTip.delay: 1000
                    ToolTip.visible: ma.containsMouse

                    Behavior on scale {
                        NumberAnimation {
                            duration: 200
                            easing.type: Easing.InOutQuad
                        }
                    }

                    Image {
                        id: cardImage

                        anchors.centerIn: card
                        source: card.cardImage
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        width: grid.cardWidth
                        height: grid.cardHeight
                        visible: false
                    }

                    MultiEffect {
                        source: cardImage
                        anchors.fill: cardImage
                        maskEnabled: true
                        maskSource: textFallback

                        // needed for smooth corners
                        // https://forum.qt.io/post/815710
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }

                    // fallback for no image
                    Rectangle {
                        id: textFallback

                        visible: cardImage.status != Image.Ready
                        width: grid.cardWidth
                        height: grid.cardHeight
                        radius: 5
                        color: "#4f4f4f"
                        anchors.centerIn: card
                        layer.enabled: true
                        layer.smooth: true

                        Label {
                            anchors.fill: textFallback
                            anchors.margins: 5
                            wrapMode: Label.WordWrap
                            text: card.name
                        }
                    }

                    Rectangle {
                        // some images clip outside the border with anchors.fill: parent, so we need to add a margin here
                        anchors.centerIn: cardImage
                        width: cardImage.width + 2
                        height: cardImage.height + 2
                        color: "#00000000"
                        border.width: ma.containsMouse ? 3 : 0
                        border.color: palette.highlight
                        radius: textFallback.radius

                        Behavior on border.width {
                            NumberAnimation {
                                duration: 200
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }

                    MouseArea {
                        id: ma

                        anchors.fill: card
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: gridRoot.gameClicked(card.steamId)
                    }

                    ColumnLayout {
                        anchors.fill: cardImage
                        anchors.margins: 5
                        spacing: 3

                        Rectangle {
                            color: "#5d9e00"
                            Layout.preferredWidth: demoLabel.implicitWidth + 4
                            Layout.preferredHeight: demoLabel.implicitHeight + 4
                            radius: 3
                            visible: Steam.gameFromId(card.steamId).type == Game.Demo

                            Label {
                                id: demoLabel

                                text: "Demo"
                                font.bold: true
                                anchors.centerIn: parent
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    ColumnLayout {
        spacing: 10

        Label {
            text: "Filter by game engine"
            font.bold: true
        }

        Label {
            text: "(may not be fully accurate)"
            font.italic: true
        }

        RowLayout {
            spacing: 10

            Button {
                text: "All"
                onClicked: {
                    unrealCb.checked = true
                    unityCb.checked = true
                    // godotCb.checked = true
                    sourceCb.checked = true
                    unknownCb.checked = true
                }
            }

            Button {
                text: "None"
                onClicked: {
                    unrealCb.checked = false
                    unityCb.checked = false
                    // godotCb.checked = false
                    sourceCb.checked = false
                    unknownCb.checked = false
                }
            }
        }

        CheckBox {
            id: unrealCb

            text: "Unreal Engine"
            Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Unreal)
            onCheckedChanged: SteamFilter.setEngineFilter(Game.Unreal, checked)
        }

        CheckBox {
            id: unityCb

            text: "Unity"
            Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Unity)
            onCheckedChanged: SteamFilter.setEngineFilter(Game.Unity, checked)
        }

        // TODO: detection support hasn't been added yet
        // CheckBox {
        //     id: godotCb

        //     text: "Godot"
        //     Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Godot)
        //     onCheckedChanged: SteamFilter.setEngineFilter(Game.Godot, checked)
        // }

        CheckBox {
            id: sourceCb

            text: "Source"
            Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Source)
            onCheckedChanged: SteamFilter.setEngineFilter(Game.Source, checked)
        }

        CheckBox {
            id: unknownCb

            text: "Other/Unknown"
            Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Unknown)
            onCheckedChanged: SteamFilter.setEngineFilter(Game.Unknown, checked)
        }

        MenuSeparator {}

        Label {
            text: "Filter by type"
            font.bold: true
        }

        RowLayout {
            spacing: 10

            Button {
                text: "All"
                onClicked: {
                    gameCb.checked = true
                    demoCb.checked = true
                    appCb.checked = true
                    toolCb.checked = true
                    musicCb.checked = true
                }
            }

            Button {
                text: "None"
                onClicked: {
                    gameCb.checked = false
                    demoCb.checked = false
                    appCb.checked = false
                    toolCb.checked = false
                    musicCb.checked = false
                }
            }
        }

        CheckBox {
            id: gameCb

            text: "Game"
            Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Game)
            onCheckedChanged: SteamFilter.setTypeFilter(Game.Game, checked)
        }

        CheckBox {
            id: demoCb

            text: "Demo"
            Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Demo)
            onCheckedChanged: SteamFilter.setTypeFilter(Game.Demo, checked)
        }

        CheckBox {
            id: appCb

            text: "Application"
            Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.App)
            onCheckedChanged: SteamFilter.setTypeFilter(Game.App, checked)
        }

        CheckBox {
            id: toolCb

            text: "Tool"
            Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Tool)
            onCheckedChanged: SteamFilter.setTypeFilter(Game.Tool, checked)
        }

        CheckBox {
            id: musicCb

            text: "Music"
            Component.onCompleted: checked = SteamFilter.isTypeFilterSet(Game.Music)
            onCheckedChanged: SteamFilter.setTypeFilter(Game.Music, checked)
        }

        Item { Layout.fillHeight: true }
    }
}
