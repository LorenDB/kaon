import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

import dev.lorendb.kaon

// Adds a game Kaon didn't find in a store. The notch button submits it.
Flickable {
    id: view

    readonly property string systemWine: Wine.whichWine()
    readonly property bool valid: nameBox.text.trim() !== "" && exeBox.text.trim() !== ""

    function localPath(url) {
        return decodeURIComponent(url.toString().replace(/^file:\/\//, ""));
    }

    function resetForm() {
        nameBox.text = "";
        exeBox.text = "";
        wineBox.text = systemWine;
        prefixBox.text = Wine.defaultWinePrefix();
        advanced.visible = false;
    }

    function submit() {
        if (!valid)
            return;
        const added = nameBox.text.trim();
        if (CustomGames.addGame(added, exeBox.text.trim(), wineBox.text.trim(), prefixBox.text.trim())) {
            resetForm();
            Nav.notify("Added " + added);
            Nav.goLibrary();
        } else {
            Nav.notify("Kaon couldn't add that game. Check that the executable exists.");
        }
    }

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: form.height + Theme.notchHeight + 60

    ScrollBar.vertical: GlassScrollBar {
    }

    Column {
        id: form

        spacing: 16
        width: Math.min(view.width - 2 * Theme.pad, 620)
        x: Theme.pad
        y: 22

        VText {
            font.pixelSize: 22
            font.weight: Font.ExtraBold
            text: "Add a game by hand"
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 13
            lineHeight: 1.35
            text: "For games Kaon doesn't find in Steam, Heroic or itch. Windows games run with the Wine and prefix below."
            width: parent.width
            wrapMode: Text.Wrap
        }

        Row {
            spacing: 10
            visible: view.systemWine === "" && wineBox.text === ""

            Led {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.ledAmber
            }

            VText {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.glassMuted
                font.pixelSize: 13
                text: "Kaon didn't find Wine on this system. Windows games won't start until you install it."
            }
        }

        Column {
            spacing: 6
            width: parent.width

            VText {
                font.pixelSize: 13
                font.weight: Font.Bold
                text: "Name"
            }

            TextBox {
                id: nameBox

                placeholder: "What the game is called"
                width: parent.width
            }
        }

        Column {
            spacing: 6
            width: parent.width

            VText {
                font.pixelSize: 13
                font.weight: Font.Bold
                text: "Executable"
            }

            Row {
                spacing: 8
                width: parent.width

                TextBox {
                    id: exeBox

                    placeholder: "/path/to/game.exe"
                    width: parent.width - exeBrowse.width - 8
                }

                VButton {
                    id: exeBrowse

                    icon: "folder"
                    text: "Browse"

                    onClicked: exePicker.open()
                }
            }
        }

        VButton {
            icon: advanced.visible ? "up" : "down"
            small: true
            text: advanced.visible ? "Hide Wine settings" : "Wine settings"

            onClicked: advanced.visible = !advanced.visible
        }

        Column {
            id: advanced

            spacing: 16
            visible: false
            width: parent.width

            Column {
                spacing: 6
                width: parent.width

                VText {
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    text: "Wine binary"
                }

                Row {
                    spacing: 8
                    width: parent.width

                    TextBox {
                        id: wineBox

                        text: view.systemWine
                        width: parent.width - wineBrowse.width - 8
                    }

                    VButton {
                        id: wineBrowse

                        icon: "folder"
                        text: "Browse"

                        onClicked: winePicker.open()
                    }
                }
            }

            Column {
                spacing: 6
                width: parent.width

                VText {
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    text: "Wine prefix"
                }

                Row {
                    spacing: 8
                    width: parent.width

                    TextBox {
                        id: prefixBox

                        text: Wine.defaultWinePrefix()
                        width: parent.width - prefixBrowse.width - 8
                    }

                    VButton {
                        id: prefixBrowse

                        icon: "folder"
                        text: "Browse"

                        onClicked: prefixPicker.open()
                    }
                }
            }
        }
    }

    FileDialog {
        id: exePicker

        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]

        onAccepted: exeBox.text = view.localPath(selectedFile)
    }

    FileDialog {
        id: winePicker

        currentFolder: "file:///usr/bin"

        onAccepted: wineBox.text = view.localPath(selectedFile)
    }

    FolderDialog {
        id: prefixPicker

        currentFolder: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]

        onAccepted: prefixBox.text = view.localPath(selectedFolder)
    }
}
