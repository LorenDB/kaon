import QtQuick

import dev.lorendb.kaon

// The launch view. One lens splits into two, the way one flat view becomes a stereo pair, and moving the pointer tilts
// both lenses and shifts the art inside them, like looking around in a headset.
Item {
    id: lens

    readonly property real d: Math.min(height * 0.62, width * 0.36)
    property Game game
    readonly property real gap: d * 0.12
    property real px: 0
    property real py: 0
    property real shown: 0

    opacity: Math.min(1, shown * 1.6)
    visible: shown > 0.01

    Behavior on px {
        NumberAnimation {
            duration: Theme.durationParallax
            easing.type: Theme.easeOut
        }
    }
    Behavior on py {
        NumberAnimation {
            duration: Theme.durationParallax
            easing.type: Theme.easeOut
        }
    }
    Behavior on shown {
        NumberAnimation {
            duration: Theme.durationTheater
            easing.type: Theme.easeInOut
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.glass
    }

    MouseArea {
        acceptedButtons: Qt.NoButton
        anchors.fill: parent
        hoverEnabled: true

        onExited: {
            lens.px = 0;
            lens.py = 0;
        }
        onPositionChanged: mouse => {
            lens.px = Math.max(-1, Math.min(1, mouse.x / width * 2 - 1));
            lens.py = Math.max(-1, Math.min(1, mouse.y / height * 2 - 1));
        }
    }

    Repeater {
        model: 2

        Item {
            id: eye

            required property int index
            readonly property real side: index === 0 ? -1 : 1
            // 0 while both lenses sit on top of each other in the middle, 1 once they've split apart
            readonly property real split: Math.max(0, Math.min(1, (lens.shown - 0.25) / 0.75))

            height: lens.d
            width: lens.d
            x: lens.width / 2 - lens.d / 2 + side * (lens.d / 2 + lens.gap / 2) * split + lens.px * 10
            y: (lens.height - lens.d) / 2 - 44 + lens.py * 6

            transform: [
                Rotation {
                    angle: lens.px * 12
                    origin.x: lens.d / 2
                    origin.y: lens.d / 2

                    axis {
                        x: 0
                        y: 1
                        z: 0
                    }
                },
                Rotation {
                    angle: -lens.py * 9
                    origin.x: lens.d / 2
                    origin.y: lens.d / 2

                    axis {
                        x: 1
                        y: 0
                        z: 0
                    }
                }
            ]

            RoundedImage {
                anchors.fill: parent
                // stereo disparity plus the parallax shift from the pointer
                contentOffsetX: -eye.side * lens.d * 0.03 * eye.split - lens.px * lens.d * 0.07
                contentOffsetY: -lens.py * lens.d * 0.05
                contentScale: 1.22
                radius: lens.d / 2
                source: lens.game ? (lens.game.heroImage !== "" ? lens.game.heroImage : lens.game.cardImage) : ""
                sourceSize.width: 1600
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -5
                border.color: Theme.glassLine
                border.width: 2
                color: "transparent"
                radius: width / 2
            }
        }
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10
        width: Math.min(lens.width - 40, 560)
        y: (lens.height + lens.d) / 2 - 44 + 22

        VText {
            font.pixelSize: 16
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            text: {
                const name = Launcher.mod ? Launcher.mod.name : "";
                const title = lens.game ? lens.game.name : "";
                switch (Launcher.phase) {
                case Launcher.Countdown:
                    return name + " opens in " + Launcher.remaining + (Launcher.remaining === 1 ? " second" : " seconds");
                case Launcher.ModOpen:
                    return name + " is open";
                case Launcher.GameStarting:
                    return title + " is starting";
                }
                return "";
            }
            width: parent.width
            wrapMode: Text.Wrap
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            text: {
                const name = Launcher.mod ? Launcher.mod.name : "";
                const title = lens.game ? lens.game.name : "";
                switch (Launcher.phase) {
                case Launcher.Countdown:
                    return "Get to the game's main menu while you wait.";
                case Launcher.ModOpen:
                    return lens.game && lens.game.canLaunch ? "Pick " + title + " in " + name + "'s window and inject, then put your headset on." :
                                                              "Start " + title + ", then pick it in " + name
                                                              + "'s window and inject.";
                case Launcher.GameStarting:
                    return "Put your headset on.";
                }
                return "";
            }
            width: parent.width
            wrapMode: Text.Wrap
        }

        VButton {
            anchors.horizontalCenter: parent.horizontalCenter
            small: true
            text: Launcher.phase === Launcher.Countdown ? "Cancel" : "Done"

            onClicked: Launcher.stop()
        }
    }
}
