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

            Label {
                text: "UEVR version:"
            }

            ComboBox {
                id: uevrCombo

                model: UEVRFilter
                textRole: "name"
                valueRole: "id"

                Component.onCompleted: currentIndex = Math.max(0, UEVRFilter.indexFromRelease(UEVR.currentUevr))
                onActivated: UEVR.setCurrentUevr(currentValue)

                Connections {
                    function onCurrentUevrChanged(i) {
                        uevrCombo.currentIndex = Math.max(0, UEVRFilter.indexFromRelease(UEVR.currentUevr));
                    }

                    target: UEVR
                }

                Connections {
                    // If the nightlies filter changes, we need to adjust the index to the new position of the current UEVR.
                    // In some cases, the current UEVR will no longer be available, so we need to change the current UEVR.
                    function onShowNightliesChanged(state) {
                        uevrCombo.currentIndex = Math.max(0, UEVRFilter.indexFromRelease(UEVR.currentUevr));
                        if (!UEVR.currentUevr || uevrCombo.currentValue !== UEVR.currentUevr.id) {
                            uevrCombo.currentIndex = 0;
                            if (uevrCombo.currentValue !== undefined)
                                UEVR.setCurrentUevr(uevrCombo.currentValue);
                        }
                    }

                    target: UEVRFilter
                }
            }

            ToolButton {
                icon.color: palette.buttonText
                icon.name: "download"
                icon.source: Qt.resolvedUrl("icons/download.svg")
                visible: UEVR.currentUevr && !UEVR.currentUevr.installed

                onClicked: UEVR.downloadUEVR(UEVR.currentUevr)
            }

            ToolButton {
                icon.color: "#da4453"
                icon.name: "delete"
                icon.source: Qt.resolvedUrl("icons/delete.svg")
                visible: UEVR.currentUevr && UEVR.currentUevr.installed

                onClicked: dialogs.deleteUevrDialog.open()
            }

            CheckBox {
                text: "Show nightlies"

                Component.onCompleted: checked = UEVRFilter.showNightlies
                onCheckedChanged: UEVRFilter.showNightlies = checked
            }

            ToolSeparator {
            }

            ToolButton {
                icon.color: palette.buttonText
                icon.name: "media-playback-start"
                icon.source: Qt.resolvedUrl("icons/media-playback-start.svg")
                text: "Launch SteamVR"
                visible: Steam.hasSteamVR

                onClicked: Steam.launchSteamVR()
            }

            ToolSeparator {
                visible: Steam.hasSteamVR
            }

            ToolButton {
                ToolTip.delay: 1000
                ToolTip.text: "Settings"
                ToolTip.visible: hovered
                enabled: theStack.currentItem ? theStack.currentItem.objectName !== "theOneAndOnlySettingsPage" : true
                hoverEnabled: true
                icon.color: enabled ? palette.buttonText : disabledPalette.buttonText
                icon.name: "settings-configure"
                icon.source: Qt.resolvedUrl("icons/settings-configure.svg")

                onClicked: theStack.push(settingsPage)
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

    StackView {
        id: theStack

        anchors.fill: root.contentItem
        anchors.margins: 10

        initialItem: GamesView {
            onGameClicked: game => {
                               theStack.push(gameDetails, {
                                                 game: game
                                             });
                           }
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
        id: dialogs
    }
}
