import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Version, nightly builds, download and delete for one mod, folded into a single dropdown. The choice applies to every
// game, as it always has.
Item {
    id: picker

    readonly property ModRelease current: (GameStatus.revision, mod.currentRelease)
    // When the mod is installed in this game, its version can't change until it's uninstalled
    property Game game: null
    readonly property bool locked: game !== null && mod.type === Mod.Installable && (GameStatus.revision, mod.isInstalledForGame(
                                                                                         game))
    required property Mod mod
    readonly property ModRelease shown: locked ? (GameStatus.revision, mod.releaseInstalledForGame(game)) ?? current :
                                                 current

    function hideNightlies() {
        releases.showNightlies = false;
        // The current release may have been a nightly that's now hidden
        if (current && current.nightly && releases.rowCount() > 0)
            mod.setCurrentRelease(releases.data(releases.index(0, 0), Qt.UserRole + 1));
    }

    ToolTip.delay: 500
    ToolTip.text: "Uninstall it from this game to change the version"
    ToolTip.visible: picker.locked && mouse.containsMouse
    implicitHeight: 32
    implicitWidth: Math.min(260, label.implicitWidth + 46)
    opacity: locked ? 0.6 : 1

    ModReleaseFilter {
        id: releases

        mod: picker.mod
    }

    Rectangle {
        anchors.fill: parent
        border.color: !picker.locked && (mouse.containsMouse || pop.visible) ? Theme.glassMuted : Theme.glassLine
        border.width: 1.5
        color: "transparent"
        radius: height / 2
    }

    VText {
        id: label

        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.right: caret.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        font.pixelSize: 12
        font.weight: Font.Bold
        text: picker.shown ? picker.shown.name : "Looking for releases"
    }

    Icon {
        id: caret

        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.glassMuted
        name: "down"
        size: 14
        stroke: 2.2
        visible: !picker.locked
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        cursorShape: picker.locked ? Qt.ArrowCursor : Qt.PointingHandCursor
        hoverEnabled: true

        onClicked: if (!picker.locked)
                       pop.open()
    }

    Popup {
        id: pop

        // keep the whole list inside the window; it moves up instead of running off the bottom
        margins: 8
        padding: 8
        width: 340
        x: picker.width - width
        y: picker.height + 6

        background: Rectangle {
            border.color: Theme.glassLine
            border.width: 1.5
            color: Theme.glassPanel
            radius: 18
        }
        contentItem: Column {
            spacing: 2

            ListView {
                clip: true
                height: Math.min(contentHeight, 320)
                interactive: contentHeight > height
                model: releases
                width: 324

                ScrollBar.vertical: GlassScrollBar {
                }
                delegate: Item {
                    id: row

                    readonly property bool downloading: (GameStatus.revision, GameStatus.isDownloading(picker.mod, release))
                    required property var model
                    readonly property ModRelease release: picker.mod.releaseFromId(model.id)
                    readonly property bool selected: picker.current !== null && picker.current.id === model.id

                    height: 46
                    width: 324

                    Rectangle {
                        anchors.fill: parent
                        color: row.selected ? Theme.glassRaised : rowMouse.containsMouse ? Theme.glassHover : "transparent"
                        radius: 12
                    }

                    Led {
                        anchors.verticalCenter: parent.verticalCenter
                        blinking: row.downloading
                        color: row.release && (row.release.downloaded || row.downloading) ? Theme.ledGreen : Theme.ledOff
                        size: 7
                        x: 14
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        width: 190
                        x: 32

                        VText {
                            elide: Text.ElideRight
                            font.pixelSize: 13
                            font.weight: row.selected ? Font.ExtraBold : Font.Medium
                            text: row.model.name
                            width: parent.width
                        }

                        VText {
                            color: Theme.glassFaint
                            font.pixelSize: 11
                            text: (row.release && row.release.nightly ? "Nightly, " : "")
                                  + row.model.timestamp.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                        }
                    }

                    MouseArea {
                        id: rowMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true

                        onClicked: picker.mod.setCurrentRelease(row.model.id)
                    }

                    VText {
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassFaint
                        font.pixelSize: 12
                        text: row.downloading ? "Downloading" : "On disk"
                        visible: row.downloading || (row.release !== null && row.release.downloaded)
                    }

                    VButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        icon: "download"
                        small: true
                        text: "Get"
                        visible: !row.downloading && row.release !== null && !row.release.downloaded

                        onClicked: GameStatus.download(picker.mod, row.release)
                    }
                }
            }

            Rectangle {
                color: Theme.glassLine
                height: 1
                visible: nightlyRow.visible || deleteButton.visible
                width: 324
            }

            Item {
                id: nightlyRow

                height: 40
                visible: (GameStatus.revision, picker.mod.hasNightlies())
                width: 324

                VText {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.glassMuted
                    font.pixelSize: 13
                    text: "Include nightly builds"
                    x: 14
                }

                ToggleSwitch {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    checked: releases.showNightlies

                    onToggled: releases.showNightlies ? picker.hideNightlies() : releases.showNightlies = true
                }
            }

            VButton {
                id: deleteButton

                icon: "trash"
                small: true
                text: "Delete " + (picker.current ? picker.current.name : "")
                visible: picker.current !== null && picker.current.downloaded
                width: Math.min(implicitWidth, 324)

                onClicked: {
                    const mod = picker.mod;
                    const release = picker.current;
                    pop.close();
                    Nav.confirm("Delete " + release.name + "?",
                                "This removes the downloaded files. You can download it again any time.", "Delete", ()
                                => mod.deleteRelease(release));
                }
            }
        }
    }
}
