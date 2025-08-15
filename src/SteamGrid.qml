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
                    toolsCb.checked = true
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
                    toolsCb.checked = false
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

        CheckBox {
            id: toolsCb

            text: "Proton and runtimes"
            Component.onCompleted: checked = SteamFilter.isEngineFilterSet(Game.Runtime)
            onCheckedChanged: SteamFilter.setEngineFilter(Game.Runtime, checked)
        }

        Item { Layout.fillHeight: true }
    }
}
