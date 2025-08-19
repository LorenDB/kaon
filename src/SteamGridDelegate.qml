import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import dev.lorendb.kaon

GridView {
    id: grid

    signal gameClicked(int steamId)

    readonly property int cardWidth: 120
    readonly property int cardHeight: 180

    model: SteamFilter
    cellWidth: {
        let usableWidth = width - (leftMargin + rightMargin + sb.width)
        return usableWidth / Math.floor(usableWidth / (cardWidth * 1.1))
    }
    cellHeight: cardHeight * 1.1
    topMargin: 10
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

            visible: cardImage.status !== Image.Ready
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
            onClicked: grid.gameClicked(card.steamId)
        }

        ColumnLayout {
            anchors.fill: cardImage
            anchors.margins: 5
            spacing: 3

            Item { Layout.fillHeight: true }

            Tag {
                text: "Demo"
                color: "#5d9e00"
                visible: Steam.gameFromId(card.steamId).type === Game.Demo
            }
        }
    }
}
