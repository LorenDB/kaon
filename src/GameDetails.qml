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

            // This is not perfect, but it gives a decent result. It's kinda pointless to spend
            // too much time getting pixel perfect, so I'm leaving it as is.
            Image {
                anchors.margins: 10
                source: gameDetailsRoot.game.logoImage
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.Left
                width: 640 * (gameDetailsRoot.game.logoWidth / 100)
                height: hero.height * (gameDetailsRoot.game.logoHeight / 100)

                Component.onCompleted: {
                    switch (gameDetailsRoot.game.logoVPosition)
                    {
                    case Game.Center:
                        anchors.verticalCenter = hero.verticalCenter;
                        break;
                    case Game.Top:
                        anchors.top = hero.top;
                        break;
                    case Game.Bottom:
                        anchors.bottom = hero.bottom;
                        break;
                    }

                    switch (gameDetailsRoot.game.logoHPosition)
                    {
                    case Game.Center:
                        anchors.horizontalCenter = hero.horizontalCenter;
                        break;
                    case Game.Left:
                        anchors.left = hero.left;
                        break;
                    case Game.Right:
                        anchors.right = hero.right;
                        break;
                    }
                }
            }

            RowLayout {
                anchors.top: hero.top
                anchors.left: hero.left
                anchors.margins: 5
                spacing: 3

                Item { Layout.fillHeight: true }

                Rectangle {
                    color: "#5d9e00"
                    Layout.preferredWidth: demoLabel.implicitWidth + 4
                    Layout.preferredHeight: demoLabel.implicitHeight + 4
                    radius: 3
                    visible: gameDetailsRoot.game.type === Game.Demo

                    Label {
                        id: demoLabel

                        text: "Demo"
                        font.bold: true
                        anchors.centerIn: parent
                    }
                }
            }

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
            text: gameDetailsRoot.game.dotnetInstalled ? "Repair or uninstall .NET" : "Install .NET desktop runtime"
            enabled: {
                if (!gameDetailsRoot.game.dotnetInstalled)
                    return !Dotnet.dotnetDownloadInProgress;
                else
                    return true;
            }
            onClicked: Dotnet.installDotnetDesktopRuntime(gameDetailsRoot.game.id)
        }

        RowLayout {
            spacing: 10

            Button {
                text: "Launch UEVR injector"
                enabled: UEVR.currentUevr.installed && gameDetailsRoot.game.dotnetInstalled
                onClicked: UEVR.launchUEVR(gameDetailsRoot.game.id)
            }

            Label {
                text: {
                    if (!gameDetailsRoot.game.dotnetInstalled)
                        return ".NET is not installed!";
                    else if (UEVR.currentUevr)
                        return UEVR.currentUevr.name + " is not installed!";
                    else
                        return "";
                }
                visible: !gameDetailsRoot.game.dotnetInstalled || (UEVR.currentUevr !== undefined && !UEVR.currentUevr.installed)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
