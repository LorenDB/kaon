import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

ApplicationWindow {
    id: root

    height: 720
    minimumHeight: 480
    minimumWidth: 640
    title: qsTr("Kaon")
    visible: true
    width: 950

    footer: Pane {
        visible: DownloadManager.downloading

        RowLayout {
            spacing: 10

            Label {
                text: "Downloading " + DownloadManager.currentDownloadName
            }

            ProgressBar {
                indeterminate: true
            }
        }
    }
    header: Pane {
        RowLayout {
            anchors.fill: parent
            spacing: 10

            TabBar {
                id: tabBar

                TabButton {
                    text: "Games"
                }

                TabButton {
                    text: "Mods"
                }

                TabButton {
                    text: "Settings"
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    Settings {
        id: settings

        property alias windowHeight: root.height
        property alias windowWidth: root.width
    }

    SystemPalette {
        id: palette

    }

    SystemPalette {
        id: disabledPalette

        colorGroup: SystemPalette.Disabled
    }

    StackLayout {
        anchors.fill: root.contentItem
        currentIndex: tabBar.currentIndex

        ColumnLayout {
            spacing: 10

            RowLayout {
                spacing: 10

                ToolButton {
                    icon.color: palette.buttonText
                    icon.name: "draw-arrow-back"
                    icon.source: Qt.resolvedUrl("icons/draw-arrow-back.svg")
                    visible: theStack.depth > 1

                    onClicked: if (theStack.depth > 1)
                                   theStack.popCurrentItem()
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    icon.color: palette.buttonText
                    icon.name: "media-playback-start"
                    icon.source: Qt.resolvedUrl("icons/media-playback-start.svg")
                    text: "Launch SteamVR"
                    visible: Steam.hasSteamVR

                    onClicked: Steam.launchSteamVR()
                }
            }

            StackView {
                id: theStack

                Layout.fillHeight: true
                Layout.fillWidth: true

                initialItem: GamesView {
                    onGameClicked: game => {
                                       theStack.push(gameDetails, {
                                                         game: game
                                                     });
                                   }
                }
            }
        }

        ModsList {
        }

        SettingsPage {
        }
    }

    Component {
        id: gameDetails

        GameDetails {
        }
    }

    Component {
        id: settingsPage

        SettingsPage {
        }
    }

    Dialogs {
    }
}
