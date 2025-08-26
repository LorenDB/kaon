import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

Pane {
    id: gameDetailsRoot

    required property Game game

    FontInfo {
        id: fontInfo

    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Image {
            id: hero

            Layout.fillWidth: true
            Layout.preferredHeight: width / 1920 * 620
            fillMode: Image.PreserveAspectCrop
            source: gameDetailsRoot.game.heroImage

            // This is not perfect, but it gives a decent result. It's kinda pointless to spend
            // too much time getting pixel perfect, so I'm leaving it as is.
            Image {
                anchors.margins: 10
                fillMode: Image.PreserveAspectFit
                height: hero.height * (gameDetailsRoot.game.logoHeight / 100)
                horizontalAlignment: Image.Left
                source: gameDetailsRoot.game.logoImage
                width: 640 * (gameDetailsRoot.game.logoWidth / 100)

                Component.onCompleted: {
                    switch (gameDetailsRoot.game.logoVPosition) {
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

                    switch (gameDetailsRoot.game.logoHPosition) {
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

            // fallback for no image
            Rectangle {
                id: textFallback

                anchors.fill: hero
                color: "#4f4f4f"
                radius: 5
                visible: hero.status !== Image.Ready

                Label {
                    anchors.centerIn: textFallback
                    anchors.margins: 5
                    text: gameDetailsRoot.game.name
                    wrapMode: Label.WordWrap
                }
            }

            RowLayout {
                anchors.left: hero.left
                anchors.margins: 10
                anchors.top: hero.top
                spacing: 3

                Item {
                    Layout.fillHeight: true
                }

                Tag {
                    color: "#ffac26"
                    text: "VR"
                    visible: gameDetailsRoot.game.supportsVr
                }

                Tag {
                    color: "#5d9e00"
                    text: "Demo"
                    visible: gameDetailsRoot.game.type === Game.Demo
                }
            }

            RowLayout {
                anchors.margins: 10
                anchors.right: hero.right
                anchors.top: hero.top
                spacing: 10

                Button {
                    ToolTip.delay: 1000
                    ToolTip.text: "Open game install folder"
                    ToolTip.visible: hovered
                    hoverEnabled: true
                    icon.color: palette.buttonText
                    icon.name: "folder"
                    icon.source: Qt.resolvedUrl("icons/folder.svg")

                    onClicked: Qt.openUrlExternally("file://" + gameDetailsRoot.game.installDir)
                }

                Button {
                    ToolTip.delay: 1000
                    ToolTip.text: "Change Steam game settings"
                    ToolTip.visible: hovered
                    hoverEnabled: true
                    icon.color: palette.buttonText
                    icon.name: "settings-configure"
                    icon.source: Qt.resolvedUrl("icons/settings-configure.svg")
                    visible: gameDetailsRoot.game.canOpenSettings

                    // TODO: make this work with other stores if possible
                    onClicked: Qt.openUrlExternally("steam://gameproperties/" + gameDetailsRoot.game.id)
                }

                Button {
                    ToolTip.delay: 1000
                    ToolTip.text: "Remove from Kaon"
                    ToolTip.visible: hovered
                    hoverEnabled: true
                    icon.color: "#da4453"
                    icon.name: "delete"
                    icon.source: Qt.resolvedUrl("delete")
                    visible: gameDetailsRoot.game.store === Game.Custom

                    onClicked: {
                        CustomGames.deleteGame(gameDetailsRoot.game);
                        theStack.popCurrentItem();
                    }
                }

                Button {
                    ToolTip.delay: 1000
                    ToolTip.text: "Launch game"
                    ToolTip.visible: hovered
                    hoverEnabled: true
                    icon.color: palette.buttonText
                    icon.name: "media-playback-start"
                    icon.source: Qt.resolvedUrl("icons/media-playback-start.svg")
                    visible: gameDetailsRoot.game.canLaunch

                    onClicked: gameDetailsRoot.game.launch()
                }
            }
        }

        Label {
            id: nameLabel

            font.bold: true
            font.pointSize: fontInfo.pointSize * 1.5
            text: gameDetailsRoot.game.name
        }

        MenuSeparator {
            Layout.minimumWidth: nameLabel.width
            Layout.preferredWidth: gameDetailsRoot.width / 2
        }

        Label {
            Layout.maximumWidth: parent.width
            text: "⚠️ " + gameDetailsRoot.game.name
                  + " appears to use anticheat. Using UEVR or other mods could potentially result in an in-game ban."
            visible: gameDetailsRoot.game.hasAnticheat
            wrapMode: Label.WordWrap
        }

        Label {
            Layout.maximumWidth: parent.width
            text: "⚠️ " + gameDetailsRoot.game.name
                  + " has multiple launch options available. Make sure you are launching this game using Wine or Proton, or else UEVR will not work."
            visible: gameDetailsRoot.game.hasMultiplePlatforms && !gameDetailsRoot.game.noWindowsSupport
            wrapMode: Label.WordWrap
        }

        Label {
            Layout.maximumWidth: parent.width
            text: "⚠️ " + gameDetailsRoot.game.name
                  + " does not have a Windows executable, so it can't be launched via Wine or Proton. UEVR will not work."
            visible: gameDetailsRoot.game.noWindowsSupport
            wrapMode: Label.WordWrap
        }

        Label {
            Layout.maximumWidth: parent.width
            text: "⚠️ This game already supports VR, so you probably should use its native VR support instead!"
            visible: gameDetailsRoot.game.supportsVr
            wrapMode: Label.WordWrap
        }

        Button {
            enabled: {
                if (gameDetailsRoot.game.noWindowsSupport)
                    return false;
                else if (!gameDetailsRoot.game.dotnetInstalled)
                    return !Dotnet.dotnetDownloadInProgress;
                else
                    return true;
            }
            text: gameDetailsRoot.game.dotnetInstalled ? "Repair or uninstall .NET" : "Install .NET desktop runtime"

            onClicked: Dotnet.installDotnetDesktopRuntime(gameDetailsRoot.game)
        }

        RowLayout {
            spacing: 10

            Button {
                enabled: UEVR.currentUevr.installed && gameDetailsRoot.game.dotnetInstalled
                         && gameDetailsRoot.game.winePrefixExists && !gameDetailsRoot.game.noWindowsSupport
                text: "Launch UEVR injector"

                onClicked: UEVR.launchUEVR(gameDetailsRoot.game)
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
                visible: !gameDetailsRoot.game.dotnetInstalled || (UEVR.currentUevr !== undefined &&
                                                                   !UEVR.currentUevr.installed)
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
