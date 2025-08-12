import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

Pane {
    id: gameDetailsRoot

    required property int steamId
    required property string heroImage
    required property string logoImage
    required property string installDir

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Image {
            id: hero

            Layout.fillWidth: true
            Layout.preferredHeight: width / 1920 * 620
            source: gameDetailsRoot.heroImage
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
                    onClicked: Qt.openUrlExternally("file://" + gameDetailsRoot.installDir)
                }

                Button {
                    icon.name: "settings-configure"
                    hoverEnabled: true
                    ToolTip.text: "Change Steam game settings"
                    ToolTip.delay: 1000
                    ToolTip.visible: hovered
                    onClicked: Qt.openUrlExternally("steam://gameproperties/" + gameDetailsRoot.steamId)
                }

                Button {
                    icon.name: "media-playback-start"
                    hoverEnabled: true
                    ToolTip.text: "Launch game in Steam"
                    ToolTip.delay: 1000
                    ToolTip.visible: hovered
                    onClicked: Qt.openUrlExternally("steam://launch/" + gameDetailsRoot.steamId)
                }
            }
        }

        Button {
            text: Dotnet.isDotnetInstalled(gameDetailsRoot.steamId) ? "Repair or uninstall .NET" : "Install .NET desktop runtime"
            enabled: {
                if (!Dotnet.isDotnetInstalled(gameDetailsRoot.steamId))
                    return !Dotnet.dotnetDownloadInProgress;
                else
                    return true;
            }
            onClicked: Dotnet.installDotnetDesktopRuntime(gameDetailsRoot.steamId)
        }

        Button {
            text: "Launch UEVR injector"
            enabled: Dotnet.isDotnetInstalled(gameDetailsRoot.steamId)
            onClicked: UEVR.launchUEVR(gameDetailsRoot.steamId)
        }

        Item { Layout.fillHeight: true }
    }
}
