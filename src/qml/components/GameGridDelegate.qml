import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import dev.lorendb.kaon

GridView {
    id: grid

    readonly property int cardHeight: 180
    readonly property int cardWidth: 120

    signal gameClicked(Game game)

    cellHeight: cardHeight * 1.1
    cellWidth: {
        let usableWidth = width - (leftMargin + rightMargin + sb.width);
        return usableWidth / Math.floor(usableWidth / (cardWidth * 1.1));
    }
    clip: true
    model: GamesFilterModel
    topMargin: 10

    ScrollBar.vertical: ScrollBar {
        id: sb

    }
    delegate: Item {
        id: card

        required property Game game

        ToolTip.delay: 1000
        ToolTip.text: game.name
        ToolTip.visible: ma.containsMouse
        height: grid.cardHeight
        scale: ma.containsMouse ? 1.07 : 1.0
        width: grid.cellWidth

        Behavior on scale {
            NumberAnimation {
                duration: 200
                easing.type: Easing.InOutQuad
            }
        }

        Image {
            id: cardImage

            anchors.centerIn: card
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            height: grid.cardHeight
            source: card.game.cardImage
            visible: false
            width: grid.cardWidth
        }

        MultiEffect {
            anchors.fill: cardImage
            maskEnabled: true
            maskSource: textFallback
            maskSpreadAtMin: 1.0

            // needed for smooth corners
            // https://forum.qt.io/post/815710
            maskThresholdMin: 0.5
            source: cardImage
        }

        // fallback for no image
        Rectangle {
            id: textFallback

            anchors.centerIn: card
            color: "#4f4f4f"
            height: grid.cardHeight
            layer.enabled: true
            layer.smooth: true
            radius: 5
            visible: cardImage.status !== Image.Ready
            width: grid.cardWidth

            Label {
                anchors.fill: textFallback
                anchors.margins: 5
                text: card.game.name
                wrapMode: Label.WordWrap
            }
        }

        Rectangle {
            // some images clip outside the border with anchors.fill: parent, so we need to add a margin here
            anchors.centerIn: cardImage
            border.color: palette.highlight
            border.width: ma.containsMouse ? 3 : 0
            color: "#00000000"
            height: cardImage.height + 2
            radius: textFallback.radius
            width: cardImage.width + 2

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
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: grid.gameClicked(card.game)
        }

        ColumnLayout {
            anchors.fill: cardImage
            anchors.margins: 5
            spacing: 3

            Item {
                Layout.fillHeight: true
            }

            Tag {
                color: "#ffac26"
                text: "VR"
                visible: game.supportsVr
            }

            Tag {
                color: "#5d9e00"
                text: "Demo"
                visible: game.type === Game.Demo
            }
        }
    }
}
