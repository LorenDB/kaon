import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

pragma ComponentBehavior: Bound

import dev.lorendb.kaon

Pane {
    id: addGameRoot

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 10

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
                text: "Browse..."
                icon.name: "folder"
                icon.source: Qt.resolvedUrl("icons/folder.svg")
                icon.color: palette.buttonText
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
            text: qsTr("OK")
            enabled: newGameName.text !== "" && newGameExe.text !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }

        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
        }
    }

    FileDialog {
        id: exePicker

        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
        onAccepted: newGameExe.text = selectedFile.toString().replace(/^file:\/\/?/, '')
    }
}
