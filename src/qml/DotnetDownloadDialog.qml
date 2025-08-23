import QtQuick
import QtQuick.Controls
import dev.lorendb.kaon

Dialog {
    id: dotnetRoot

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
