import QtQuick
import QtQuick.Controls

import dev.lorendb.kaon

Item {
    Dialog {
        id: dotnetDownloadDialog

        property Game game: null

        title: "Download .NET desktop runtime"
        modal: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            Dotnet.downloadDotnetDesktopRuntime(game);
            game = null;
        }

        Label {
            text: "Kaon needs to download the .NET desktop runtime. This download is about 55 MB and will be cached for future use."
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: downloadFailedDialog

        property string whatWasBeingDownloaded: "<null>"

        title: "Download failed"
        modal: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok
        onAccepted: whatWasBeingDownloaded = "<null>"

        Label {
            anchors.fill: parent
            text: "Downloading " + downloadFailedDialog.whatWasBeingDownloaded + " failed. Please check your network connection."
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: deleteUevrDialog

        title: "Confirm deletion"
        modal: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: UEVR.deleteUEVR(UEVR.currentUevr)

        Label {
            anchors.fill: parent
            text: "Are you sure you want to delete " + (UEVR.currentUevr ? UEVR.currentUevr.name : "<null>") + "?"
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: updateAvailableDialog

        property string updateVersion
        property string updateUrl

        title: "Update available"
        modal: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        Label {
            anchors.fill: parent
            text: "Kaon " + updateAvailableDialog.updateVersion + " is now available. Find out more and download the update at <a href=\""
                  + updateAvailableDialog.updateUrl + "\">the release page</a>."
            wrapMode: Text.WordWrap
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }

    Dialog {
        id: wineFailedDialog

        property string prettyName

        title: prettyName === "" ? "Failure during process execution" : prettyName
        modal: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Ok

        Label {
            anchors.fill: parent
            text: (wineFailedDialog.prettyName === "" ? "An unknown process" : wineFailedDialog.prettyName) + " failed!"
            wrapMode: Text.WordWrap
        }
    }

    Connections {
        function onPromptDotnetDownload(game: Game) {
            dotnetDownloadDialog.game = game;
            dotnetDownloadDialog.open();
        }

        function onDotnetDownloadFailed() {
            downloadFailedDialog.open();
        }

        target: Dotnet
    }

    Connections {
        function onDownloadFailed(whatWasBeingDownloaded: string) {
            downloadFailedDialog.whatWasBeingDownloaded = whatWasBeingDownloaded
            downloadFailedDialog.open()
        }

        target: DownloadManager
    }

    UpdateChecker {
        onUpdateAvailable: (version, url) => {
            updateAvailableDialog.updateVersion = version;
            updateAvailableDialog.updateUrl = url;
            updateAvailableDialog.open();
        }
    }

    Connections {
        function onProcessFailed(prettyName: string)
        {
            wineFailedDialog.prettyName = prettyName;
            wineFailedDialog.open();
        }

        target: Wine
    }
}
