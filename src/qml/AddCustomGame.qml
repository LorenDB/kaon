pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import dev.lorendb.kaon

Pane {
    id: addGameRoot

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 10

        Label {
            Layout.maximumWidth: parent.width
            text: "⚠️ Couldn't find a systemwide Wine installation. Windows games will not work unless you install Wine."
            visible: Wine.whichWine() === ""
            wrapMode: Label.WordWrap
        }

        TextField {
            id: newGameName

            Layout.minimumWidth: 400
            placeholderText: "Name"
        }

        RowLayout {
            spacing: 10

            TextField {
                id: newGameExe

                Layout.minimumWidth: 400
                placeholderText: "Executable"
            }

            Button {
                icon.color: palette.buttonText
                icon.name: "folder"
                icon.source: Qt.resolvedUrl("icons/folder.svg")
                text: "Browse..."

                onClicked: exePicker.open()
            }
        }
    }

    DialogButtonBox {
        anchors.bottom: parent.bottom
        anchors.right: parent.right

        onAccepted: {
            CustomGames.addGame(newGameName.text, newGameExe.text);
            theStack.popCurrentItem();
        }

        Button {
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            enabled: newGameName.text !== "" && newGameExe.text !== ""
            text: qsTr("OK")
        }

        Button {
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            text: qsTr("Cancel")
        }
    }

    FileDialog {
        id: exePicker

        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]

        onAccepted: newGameExe.text = selectedFile.toString().replace(/^file:\/\/?/, '')
    }
}
