import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

Pane {
    id: gameDetailsRoot

    required property int steamId
    required property string heroImage
    required property string logoImage
    required property string installDir

    Image {
        id: hero

        anchors.top: parent.top
        width: parent.width
        height: width / 1920 * 620
        source: gameDetailsRoot.heroImage
        fillMode: Image.PreserveAspectCrop

        // TODO: this needs work to display properly as it would in Steam
        // Image {
        //     anchors.fill: parent
        //     source: gameDetailsRoot.logoImage
        //     fillMode: Image.PreserveAspectFit
        // }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10

        Button {
            text: "Install .NET desktop runtime"
            enabled: !Dotnet.isDotnetInstalled(gameDetailsRoot.steamId) && !Dotnet.dotnetDownloadInProgress
            onClicked: Dotnet.installDotnetDesktopRuntime(gameDetailsRoot.steamId)
        }

        Button {
            text: "Launch UEVR injector"
            onClicked: UEVR.launchUEVR(gameDetailsRoot.steamId)
        }

        Button {
            icon.name: "folder"
            text: "Open game install folder"
            onClicked: Qt.openUrlExternally("file://" + gameDetailsRoot.installDir)
        }

        Item { Layout.fillHeight: true }
    }
}
