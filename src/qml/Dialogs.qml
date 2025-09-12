import QtQuick
import QtQuick.Controls

import dev.lorendb.kaon

Item {
    Dialog {
        id: dotnetDownloadDialog

        property Game game: null

        closePolicy: Popup.CloseOnEscape
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "Download .NET desktop runtime"

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

        closePolicy: Popup.CloseOnEscape
        modal: true
        standardButtons: Dialog.Ok
        title: "Download failed"

        onAccepted: whatWasBeingDownloaded = "<null>"

        Label {
            anchors.fill: parent
            text: "Downloading " + downloadFailedDialog.whatWasBeingDownloaded
                  + " failed. Please check your network connection."
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: updateAvailableDialog

        property string updateUrl
        property string updateVersion

        closePolicy: Popup.CloseOnEscape
        modal: true
        title: "Update available"

        footer: DialogButtonBox {
            Button {
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                text: qsTr("OK")
            }

            Button {
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                text: qsTr("Ignore this release")
            }
        }

        onRejected: UpdateChecker.ignore = updateVersion

        Label {
            anchors.fill: parent
            text: "Kaon " + updateAvailableDialog.updateVersion
                  + " is now available. Find out more and download the update at <a href=\""
                  + updateAvailableDialog.updateUrl + "\">the release page</a>."
            wrapMode: Text.WordWrap

            onLinkActivated: link => Qt.openUrlExternally(link)
        }
    }

    Dialog {
        id: wineFailedDialog

        property string prettyName

        closePolicy: Popup.CloseOnEscape
        modal: true
        standardButtons: Dialog.Ok
        title: prettyName === "" ? "Failure during process execution" : prettyName

        Label {
            anchors.fill: parent
            text: (wineFailedDialog.prettyName === "" ? "An unknown process" : wineFailedDialog.prettyName) + " failed!"
            wrapMode: Text.WordWrap
        }
    }

    Connections {
        function onDotnetDownloadFailed() {
            downloadFailedDialog.open();
        }

        target: Dotnet
    }

    Connections {
        function onDownloadFailed(whatWasBeingDownloaded: string) {
            downloadFailedDialog.whatWasBeingDownloaded = whatWasBeingDownloaded;
            downloadFailedDialog.open();
        }

        target: DownloadManager
    }

    Connections {
        function onUpdateAvailable(version: string, url: string) {
            updateAvailableDialog.updateVersion = version;
            updateAvailableDialog.updateUrl = url;
            updateAvailableDialog.open();
        }

        target: UpdateChecker
    }

    Connections {
        function onProcessFailed(prettyName: string) {
            wineFailedDialog.prettyName = prettyName;
            wineFailedDialog.open();
        }

        target: Wine
    }
}
