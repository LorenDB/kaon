import QtQuick

import dev.lorendb.kaon

Item {
    id: card

    readonly property bool busy: game ? GameStatus.steps(game, GameStatus.revision).some(s => s.state === "busy") : false
    property real cardWidth: 132
    readonly property Game game: modelData
    readonly property string group: game ? GameStatus.group(game, GameStatus.revision) : "none"
    readonly property bool hot: mouse.containsMouse || activeFocus
    required property var modelData

    activeFocusOnTab: true
    height: cardWidth * 1.5 + 50
    width: cardWidth

    Keys.onReturnPressed: Nav.openGame(game)
    Keys.onSpacePressed: Nav.openGame(game)

    Item {
        id: cover

        height: card.cardWidth * 1.5
        scale: card.hot ? 1.03 : 1
        width: card.cardWidth

        Behavior on scale {
            NumberAnimation {
                duration: Theme.durationFast
                easing.type: Theme.easeOut
            }
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.glassRaised
            radius: 14

            VText {
                anchors.fill: parent
                anchors.margins: 12
                color: Theme.glassMuted
                font.pixelSize: 15
                font.weight: Font.ExtraBold
                text: card.game ? card.game.name : ""
                verticalAlignment: Text.AlignBottom
                visible: !art.ready
                wrapMode: Text.Wrap
            }
        }

        RoundedImage {
            id: art

            anchors.fill: parent
            radius: 14
            source: card.game ? card.game.cardImage : ""
            sourceSize.width: 300
        }

        Row {
            anchors.left: parent.left
            anchors.margins: 8
            anchors.top: parent.top
            spacing: 4

            Repeater {
                model: card.game ? [card.game.type === Game.Demo ? "Demo" : "", card.game.vrOnly ? "VR only" : ""].filter(t
                                                                                                                          => t !== "") :
                                   []

                Rectangle {
                    required property string modelData

                    color: "#d9050608"
                    height: 20
                    radius: 10
                    width: tagText.implicitWidth + 14

                    VText {
                        id: tagText

                        anchors.centerIn: parent
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        text: parent.modelData
                    }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            border.color: card.activeFocus ? Theme.ledBlue : Theme.glassText
            border.width: 2
            color: "transparent"
            radius: 18
            visible: card.hot
        }
    }

    VText {
        elide: Text.ElideRight
        font.pixelSize: 13
        font.weight: Font.Bold
        text: card.game ? card.game.name : ""
        width: parent.width
        y: cover.height + 10
    }

    Row {
        spacing: 7
        width: parent.width
        y: cover.height + 30

        Led {
            anchors.verticalCenter: parent.verticalCenter
            blinking: card.busy
            color: Theme.led(card.group)
            size: 7
        }

        VText {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassMuted
            elide: Text.ElideRight
            font.pixelSize: 12
            text: card.game ? GameStatus.summary(card.game, GameStatus.revision) : ""
            width: parent.width - 14
        }
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: Nav.openGame(card.game)
    }
}
