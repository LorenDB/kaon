import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import dev.lorendb.kaon

ListView {
    id: list

    signal gameClicked(Game game)

    clip: true
    model: GamesFilterModel

    ScrollBar.vertical: ScrollBar {
        id: sb

    }
    delegate: ItemDelegate {
        id: delegate

        required property Game game

        height: 48
        width: ListView.view.width - (sb.visible ? sb.width : 0)

        Image {
            id: iconImage

            Layout.preferredHeight: 32
            Layout.preferredWidth: 32
            fillMode: Image.PreserveAspectFit
            source: delegate.game.icon
            visible: false
        }

        RowLayout {
            id: content

            anchors.left: delegate.left
            anchors.margins: 10
            anchors.verticalCenter: delegate.verticalCenter
            spacing: 10
            width: delegate.width - 20

            MultiEffect {
                Layout.preferredHeight: 32
                Layout.preferredWidth: 32
                maskEnabled: true
                maskSource: textFallback
                maskSpreadAtMin: 1.0

                // needed for smooth corners
                // https://forum.qt.io/post/815710
                maskThresholdMin: 0.5
                source: iconImage
                visible: iconImage.status === Image.Ready
            }

            // fallback for no image
            Rectangle {
                id: textFallback

                Layout.preferredHeight: 32
                Layout.preferredWidth: 32
                color: "#4f4f4f"
                layer.enabled: true
                layer.smooth: true
                radius: 3
                visible: iconImage.status !== Image.Ready
            }

            Label {
                text: delegate.game.name
            }

            Tag {
                color: "#ffac26"
                text: "VR"
                visible: delegate.game.supportsVr
            }

            Tag {
                color: "#5d9e00"
                text: "Demo"
                visible: delegate.game.type === Game.Demo
            }

            Item {
                Layout.fillWidth: true
            }
        }

        MouseArea {
            id: ma

            anchors.fill: delegate
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: list.gameClicked(delegate.game)
        }
    }
}
