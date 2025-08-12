import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

pragma ComponentBehavior: Bound

import dev.lorendb.kaon

Frame {
    id: gridRoot

    signal gameClicked(int steamId)

    RowLayout {
        anchors.fill: parent
        spacing: 10

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
                required property string heroImage
                required property string logoImage
                required property string installDir

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
                    onClicked: gridRoot.gameClicked(card.steamId)
                }
            }
        }

        ColumnLayout {
            spacing: 10

            CheckBox {
                text: "Show all"
                hoverEnabled: true
                ToolTip.text: "Shows normally hidden entries like Proton"
                ToolTip.visible: hovered
                ToolTip.delay: 1000
                Component.onCompleted: checked = SteamFilter.showAll
                onCheckedChanged: SteamFilter.showAll = checked
            }

            Item { Layout.fillHeight: true }
        }
    }
}
