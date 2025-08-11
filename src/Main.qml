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
                model: UEVR
            }
        }
    }

    footer: Pane {
        visible: UEVR.dotnetDownloadInProgress

        RowLayout {
            spacing: 10

            Label {
                text: "Downloading..."
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
                theStack.push(gameDetails, { steamId: steamId })
            }
        }
    }

    Component {
        id: gameDetails

        GameDetails { anchors.fill: parent }
    }

    DotnetDownloadDialog {
        id: dotnetDownloadDialog
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

        target: UEVR
    }

}
