import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: gameDetailsRoot

    required property int steamId

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10

        Button {
            text: "Install .NET desktop runtime"
            onClicked: UEVR.installDotnetDesktopRuntime(gameDetailsRoot.steamId)
        }

        Button {
            text: "Launch UEVR injector"
            onClicked: UEVR.launchUEVR(gameDetailsRoot.steamId)
        }

        Item { Layout.fillHeight: true }
    }
}
