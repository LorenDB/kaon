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

    // The bundled rounded font draws U+2026 vertically centered (Japanese-style), so Qt's built-in eliding floats
    // mid-line. Truncate manually with three baseline periods instead. The font is an argument only so that a binding
    // which calls this runs again when the font does change: a value that is read and not used gets dropped by the QML
    // compiler.
    function dotsElided(fontMetrics, source, availWidth, font) {
        if (!source || availWidth <= 0)
            return "";
        if (fontMetrics.advanceWidth(source) <= availWidth)
            return source;
        const dots = "...";
        const dotsWidth = fontMetrics.advanceWidth(dots);
        if (dotsWidth >= availWidth)
            return "";
        let lo = 0;
        let hi = source.length;
        while (lo < hi) {
            const mid = (lo + hi + 1) >> 1;
            if (fontMetrics.advanceWidth(source.slice(0, mid)) + dotsWidth <= availWidth)
                lo = mid;
            else
                hi = mid - 1;
        }
        return source.slice(0, lo) + dots;
    }

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
        id: titleText

        clip: true
        font.pixelSize: 13
        font.weight: Font.Bold
        maximumLineCount: 1
        text: dotsElided(titleFontMetrics, card.game ? card.game.name : "", width, titleFontMetrics.font)
        width: parent.width
        wrapMode: Text.NoWrap
        y: cover.height + 10
    }

    FontMetrics {
        id: titleFontMetrics

        font: titleText.font
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
            id: statusText

            anchors.verticalCenter: parent.verticalCenter
            clip: true
            color: Theme.glassMuted
            font.pixelSize: 12
            maximumLineCount: 1
            text: dotsElided(statusFontMetrics, card.game ? GameStatus.summary(card.game, GameStatus.revision) : "", width,
                             statusFontMetrics.font)
            width: parent.width - 14
            wrapMode: Text.NoWrap
        }
    }

    FontMetrics {
        id: statusFontMetrics

        font: statusText.font
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: Nav.openGame(card.game)
    }
}
