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

            Rectangle {
                width: grid.cardWidth
                height: grid.cardHeight
                radius: 5
                color: "#4f4f4f"
                scale: ma.containsMouse ? 1.07 : 1.0
                anchors.centerIn: card

                Behavior on scale {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.InOutQuad
                    }
                }

                Image {
                    id: cardImage
                    anchors.fill: parent
                    source: card.cardImage
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true

                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskSource: Rectangle {
                            width: 90
                            height: 135
                            radius: 3
                        }
                    }

                    // fallback for no image
                    Label {
                        anchors.fill: cardImage
                        anchors.margins: 3
                        wrapMode: Label.WordWrap
                        text: card.name
                        visible: cardImage.status != Image.Ready
                    }
                }

                MouseArea {
                    id: ma

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }
    }
}
