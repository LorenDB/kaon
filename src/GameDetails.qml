import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

Pane {
    id: gameDetailsRoot

    required property Game game

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Image {
            id: hero

            Layout.fillWidth: true
            Layout.preferredHeight: width / 1920 * 620
            source: gameDetailsRoot.game.heroImage
            fillMode: Image.PreserveAspectCrop

            // TODO: this needs work to display properly as it would in Steam
            // Image {
            //     anchors.fill: parent
            //     source: gameDetailsRoot.logoImage
            //     fillMode: Image.PreserveAspectFit
            // }

            RowLayout {
                anchors.top: hero.top
                anchors.right: hero.right
                anchors.margins: 10
                spacing: 10

                Button {
                    icon.name: "folder"
                    hoverEnabled: true
                    ToolTip.text: "Open game install folder"
                    ToolTip.delay: 1000
                    ToolTip.visible: hovered
                    onClicked: Qt.openUrlExternally("file://" + gameDetailsRoot.game.installDir)
                }

                Button {
                    icon.name: "settings-configure"
                    hoverEnabled: true
                    ToolTip.text: "Change Steam game settings"
                    ToolTip.delay: 1000
                    ToolTip.visible: hovered
                    onClicked: Qt.openUrlExternally("steam://gameproperties/" + gameDetailsRoot.game.id)
                }

                Button {
                    icon.name: "media-playback-start"
                    hoverEnabled: true
                    ToolTip.text: "Launch game in Steam"
                    ToolTip.delay: 1000
                    ToolTip.visible: hovered
                    onClicked: Qt.openUrlExternally("steam://launch/" + gameDetailsRoot.game.id)
                }
            }
        }

        Button {
            text: Dotnet.isDotnetInstalled(gameDetailsRoot.game.id) ? "Repair or uninstall .NET" : "Install .NET desktop runtime"
            enabled: {
                if (!Dotnet.isDotnetInstalled(gameDetailsRoot.game.id))
                    return !Dotnet.dotnetDownloadInProgress;
                else
                    return true;
            }
            onClicked: Dotnet.installDotnetDesktopRuntime(gameDetailsRoot.game.id)
        }

        Button {
            text: "Launch UEVR injector"
            enabled: Dotnet.isDotnetInstalled(gameDetailsRoot.game.id)
            onClicked: UEVR.launchUEVR(gameDetailsRoot.game.id)
        }

        Item { Layout.fillHeight: true }
    }
}
