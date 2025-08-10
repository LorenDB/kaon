import QtQuick
import QtQuick.Controls
import QtQuick.Effects

pragma ComponentBehavior: Bound

import dev.lorendb.kaon

Frame {
    GridView {
        id: grid

        readonly property int cardWidth: 120
        readonly property int cardHeight: 180

        model: Steam
        anchors.fill: parent
        cellWidth: {
            let usableWidth = width - (leftMargin + rightMargin + sb.width)
            return usableWidth / Math.floor(usableWidth / (cardWidth * 1.16))
        }
        cellHeight: cardHeight * 1.16
        bottomMargin: 15
        topMargin: 15
        leftMargin: 15
        rightMargin: 15
        clip: true

        ScrollBar.vertical: ScrollBar { id: sb }
        delegate: Item {
            id: card

            required property string cardImage
            required property string name

            width: grid.cellWidth
            height: grid.cardHeight
            scale: ma.containsMouse ? 1.07 : 1.0

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

            MouseArea {
                id: ma

                anchors.fill: card
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
            }
        }
    }
}
