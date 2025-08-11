import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import dev.lorendb.kaon

Dialog {
    id: dotnetRoot

    property int steamId: 0

    title: "Download .NET desktop runtime"
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    onAccepted: {
        UEVR.downloadDotnetDesktopRuntime(steamId);
        steamId = 0;
    }

    Label {
        text: "Kaon needs to download the .NET desktop runtime. This download is about 55 MB and will be cached for future use."
        wrapMode: Text.WordWrap
    }
}
