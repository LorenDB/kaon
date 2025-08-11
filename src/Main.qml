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

    ColumnLayout {
        anchors.fill: root.contentItem
        anchors.margins: 10
        spacing: 10

        RowLayout {
            spacing: 10

            Item { Layout.fillWidth: true }

            Label { text: "UEVR version:" }

            ComboBox {
                model: UEVR
            }
        }

        StackView {
            id: theStack

            Layout.fillWidth: true
            Layout.fillHeight: true

            SteamGrid {
                anchors.fill: parent
                onGameClicked: (steamId) => {
                    gameDetails.createObject(theStack, { steamId: steamId })
                }
            }
        }
    }

    Component {
        id: gameDetails

        GameDetails { anchors.fill: parent }
    }
}
