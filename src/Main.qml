import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

ApplicationWindow {
    id: root

    visible: true
    title: qsTr("Kaon")
    width: 900
    height: 550

    header: Pane {
        RowLayout {
            anchors.fill: parent
            spacing: 10

            ToolButton {
                icon.name: "draw-arrow-back"
                visible: theStack.depth > 0
                onClicked: theStack.popCurrentItem()
            }

            Item { Layout.fillWidth: true }

            Label { text: "UEVR version:" }

            ComboBox {
                id: uevrCombo

                model: UEVRFilter
                textRole: "name"
                onActivated: UEVR.currentUevr = currentValue
                valueRole: "id"
                Component.onCompleted: currentIndex = Math.max(0, UEVRFilter.indexFromId(UEVR.currentUevr))

                Connections {
                    function onCurrentUevrChanged(i)
                    {
                        uevrCombo.currentIndex = Math.max(0, UEVRFilter.indexFromId(UEVR.currentUevr))
                    }

                    target: UEVR
                }

                Connections {
                    // If the nightlies filter changes, we need to adjust the index to the new position of the current UEVR.
                    // In some cases, the current UEVR will no longer be available, so we need to change the current UEVR.
                    function onShowNightliesChanged(state)
                    {
                        uevrCombo.currentIndex = Math.max(0, UEVRFilter.indexFromId(UEVR.currentUevr))
                        if (uevrCombo.currentValue != UEVR.currentUevr)
                        {
                            uevrCombo.currentIndex = 0;
                            UEVR.currentUevr = uevrCombo.currentValue;
                        }
                    }

                    target: UEVRFilter
                }
            }

            ToolButton {
                icon.name: "download"
                enabled: !UEVR.isInstalled(UEVR.currentUevr)
                onClicked: UEVR.downloadUEVR(UEVR.currentUevr)
            }

            CheckBox {
                text: "Show nightlies"
                Component.onCompleted: checked = UEVRFilter.showNightlies
                onCheckedChanged: UEVRFilter.showNightlies = checked
            }
        }
    }

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

    StackView {
        id: theStack

        anchors.fill: root.contentItem
        anchors.margins: 10

        SteamGrid {
            anchors.fill: parent
            onGameClicked: (steamId) => {
                theStack.push(gameDetails, { game: Steam.gameFromId(steamId) })
            }
        }
    }

    Component {
        id: gameDetails

        GameDetails {}
    }

    DotnetDownloadDialog {
        id: dotnetDownloadDialog

        anchors.centerIn: parent
    }

    Dialog {
        id: downloadFailedDialog

        title: "Download failed"
        modal: true
        standardButtons: Dialog.Ok

        Label {
            text: "Downloading the .NET runtime failed. Please check your network connection."
            wrapMode: Text.WordWrap
        }
    }

    Connections {
        function onPromptDotnetDownload(steamId: int) {
            dotnetDownloadDialog.steamId = steamId;
            dotnetDownloadDialog.open();
        }

        function onDotnetDownloadFailed() {
            downloadFailedDialog.open();
        }

        target: Dotnet
    }

}
