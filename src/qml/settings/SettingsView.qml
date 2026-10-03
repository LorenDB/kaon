import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Two columns on a wide window, so the settings don't sit in a narrow strip of a big black panel.
Flickable {
    id: view

    readonly property real colW: wide ? (width - 2 * Theme.pad - 40) / 2 : width - 2 * Theme.pad
    readonly property bool wide: width > 900

    function libraryRows() {
        const rows = [];
        for (const lib of Steam.libraries)
            rows.push(lib);
        for (const lib of Heroic.libraries)
            rows.push(lib);
        rows.push({
                      "store": "itch",
                      "path": Itch.storeRoot,
                      "count": Itch.count
                  });
        return rows;
    }

    boundsBehavior: Flickable.StopAtBounds
    clip: true
    contentHeight: Math.max(left.height, right.y + right.height) + Theme.notchHeight + 50

    ScrollBar.vertical: GlassScrollBar {
    }

    Column {
        id: left

        width: view.colW
        x: Theme.pad
        y: 18

        Heading {
            text: "Libraries"
        }

        Repeater {
            model: view.libraryRows()

            Item {
                required property var modelData

                height: 40
                width: view.colW

                Led {
                    anchors.verticalCenter: parent.verticalCenter
                    color: parent.modelData.path !== "" ? Theme.ledGreen : Theme.ledOff
                    x: 3
                }

                VText {
                    id: storeName

                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    text: parent.modelData.store
                    x: 26
                }

                VText {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.glassMuted
                    elide: Text.ElideMiddle
                    font.pixelSize: 13
                    text: parent.modelData.path !== "" ? parent.modelData.path + (parent.modelData.count > 0 ? ", "
                                                                                                               + parent.modelData.count
                                                                                                               + " apps" :
                                                                                                               "") : "Not found"
                    width: parent.width - x
                    x: storeName.x + storeName.implicitWidth + 16
                }
            }
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 13
            text: CustomGames.count === 1 ? "1 game added by hand" : CustomGames.count + " games added by hand"
            topPadding: 4
            visible: CustomGames.count > 0
            x: 26
        }

        Flow {
            spacing: 8
            topPadding: 12
            width: parent.width

            VButton {
                icon: "refresh"
                small: true
                text: GamesFilterModel.scanning ? "Scanning" : "Rescan"

                onClicked: {
                    GameStatus.rescanLibraries();
                    Nav.notify("Scanning your libraries");
                }
            }

            VButton {
                icon: "plus"
                small: true
                text: "Add a game by hand"

                onClicked: Nav.view = "addGame"
            }
        }

        VText {
            color: Theme.glassFaint
            font.pixelSize: 12
            text: "Flatpak and Snap installs of Steam and Heroic aren't supported yet."
            topPadding: 14
            width: parent.width
            wrapMode: Text.Wrap
        }
    }

    Column {
        id: right

        width: view.colW
        x: view.wide ? Theme.pad + view.colW + 40 : Theme.pad
        y: view.wide ? 18 : left.y + left.height + 28

        Heading {
            text: "Appearance"
        }

        Item {
            height: 48
            width: view.colW

            VText {
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 14
                font.weight: Font.Bold
                text: "Frame color"
            }

            Segmented {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                current: Theme.appearance
                options: [
                    {
                        "id": "system",
                        "label": "Match system"
                    },
                    {
                        "id": "light",
                        "label": "Light"
                    },
                    {
                        "id": "dark",
                        "label": "Dark"
                    }
                ]

                onPicked: key => Theme.appearance = key
            }
        }

        VText {
            bottomPadding: 18
            color: Theme.glassMuted
            font.pixelSize: 13
            text: "The panel in the middle stays black either way; game art reads best on it."
            width: view.colW
            wrapMode: Text.Wrap
        }

        Heading {
            text: "General"
        }

        SwitchRow {
            checked: UpdateChecker.enabled
            detail: "Kaon checks GitHub when it starts."
            text: "Tell me about new Kaon versions"

            onToggled: UpdateChecker.enabled = !UpdateChecker.enabled
        }

        SwitchRow {
            checked: Aptabase.enabled
            detail: "Anonymous events such as “installed UEVR 1.05 for Stray”. They help decide what to build next."
            text: "Send anonymous usage statistics"

            onToggled: Aptabase.enabled = !Aptabase.enabled
        }

        Flow {
            spacing: 12
            topPadding: 14
            width: parent.width

            VButton {
                small: true
                text: "Check for updates now"

                onClicked: {
                    UpdateChecker.checkUpdates();
                    Nav.notify("Checking for a new Kaon version");
                }
            }

            VText {
                color: Theme.glassFaint
                font.pixelSize: 12
                height: 32
                text: "Kaon " + Qt.application.version
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    component Heading: VText {
        bottomPadding: 6
        color: Theme.glassFaint
        font.pixelSize: 12
        font.weight: Font.Bold
    }
    component SwitchRow: Item {
        id: sr

        property bool checked
        property string detail
        property string text

        signal toggled

        height: srCol.height + 24
        width: view.colW

        Column {
            id: srCol

            spacing: 2
            width: parent.width - 70
            y: 12

            VText {
                font.pixelSize: 14
                font.weight: Font.Bold
                text: sr.text
                width: parent.width
                wrapMode: Text.Wrap
            }

            VText {
                color: Theme.glassMuted
                font.pixelSize: 13
                font.weight: Font.Normal
                text: sr.detail
                width: parent.width
                wrapMode: Text.Wrap
            }
        }

        ToggleSwitch {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            checked: sr.checked

            onToggled: sr.toggled()
        }
    }
}
