import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

import dev.lorendb.kaon

ListView {
    id: list

    signal gameClicked(int steamId)

    model: SteamFilter
    clip: true

    ScrollBar.vertical: ScrollBar { id: sb }
    delegate: ItemDelegate {
        id: delegate

        required property string name
        required property int steamId
        required property string iconImage

        // height: content.implicitHeight
        height: 48
        width: ListView.view.width - (sb.visible ? sb.width : 0)

        Image {
            id: iconImage

            source: "file://" + delegate.iconImage
            fillMode: Image.PreserveAspectFit
            Layout.preferredHeight: 32
            Layout.preferredWidth: 32
            visible: false
        }

        RowLayout {
            id: content

            spacing: 10
            width: delegate.width - 20
            anchors.left: delegate.left
            anchors.verticalCenter: delegate.verticalCenter
            anchors.margins: 10

            MultiEffect {
                source: iconImage
                Layout.preferredHeight: 32
                Layout.preferredWidth: 32
                maskEnabled: true
                maskSource: textFallback
                visible: iconImage.status === Image.Ready

                // needed for smooth corners
                // https://forum.qt.io/post/815710
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            // fallback for no image
            Rectangle {
                id: textFallback

                visible: iconImage.status !== Image.Ready
                Layout.preferredHeight: 32
                Layout.preferredWidth: 32
                radius: 3
                color: "#4f4f4f"
                layer.enabled: true
                layer.smooth: true
            }

            Label {
                text: delegate.name
            }

            Tag {
                text: "VR"
                color: "#ffac26"
                visible: Steam.gameFromId(delegate.steamId).supportsVr
            }

            Tag {
                text: "Demo"
                color: "#5d9e00"
                visible: Steam.gameFromId(delegate.steamId).type === Game.Demo
            }

            Item { Layout.fillWidth: true }
        }

        MouseArea {
            id: ma

            anchors.fill: delegate
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: list.gameClicked(delegate.steamId)
        }
    }
}
