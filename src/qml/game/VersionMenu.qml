import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Version, nightly builds, prereleases, download and delete for one mod, folded into a single dropdown. The choice applies to every
// game, as it always has.
Item {
    id: picker

    readonly property ModRelease current: mod.currentRelease
    // When the mod is installed in this game, its version can't change until it's uninstalled
    property Game game: null
    readonly property bool locked: game !== null && mod.type === Mod.Installable && GameStatus.isInstalled(mod, game,
                                                                                                           GameStatus.revision)
    required property Mod mod
    property int popupEpoch: 0
    // Long lists get a search field so a version can be found without scrolling through all of them.
    // totalCount notifies; a rowCount() call in a binding would run once and go stale.
    readonly property bool showSearch: releases.totalCount > 6
    readonly property ModRelease shown: locked ? GameStatus.installedRelease(mod, game, GameStatus.revision) ?? current :
                                                 current

    function hideNightlies() {
        releases.showNightlies = false;
        // The current release may have been a nightly that's now hidden
        if (current && current.nightly && releases.rowCount() > 0)
            mod.setCurrentRelease(releases.data(releases.index(0, 0), Qt.UserRole + 1));
    }

    function hidePrereleases() {
        releases.showPrereleases = false;
        // The current release may have been a prerelease that's now hidden
        if (current && current.prerelease && releases.rowCount() > 0)
            mod.setCurrentRelease(releases.data(releases.index(0, 0), Qt.UserRole + 1));
    }

    ToolTip.delay: 500
    ToolTip.text: picker.locked ? "Uninstall it from this game to change the version" : label.truncated && picker.shown
                                  ? picker.mod.releaseTitle(picker.shown) : ""
    ToolTip.visible: (picker.locked || (label.truncated && picker.shown !== null)) && mouse.containsMouse
    activeFocusOnTab: !locked
    implicitHeight: 32
    implicitWidth: Math.min(260, label.implicitWidth + 46)
    opacity: locked ? 0.6 : 1

    Keys.onReturnPressed: if (!picker.locked)
                              pop.open()
    Keys.onSpacePressed: if (!picker.locked)
                             pop.open()
    onPopupEpochChanged: pop.close()

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

    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        border.color: Theme.ledBlue
        border.width: 2
        color: "transparent"
        radius: height / 2
        visible: picker.activeFocus
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
        text: picker.shown ? picker.mod.releaseLabel(picker.shown) : "Looking for releases"
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
        contentItem: Flickable {
            id: sheet

            // The release list used to take up to 320px inside a column that could not scroll, so the
            // nightly switch and Delete sat below a short window.
            readonly property real cap: Math.max(160, (Overlay.overlay ? Overlay.overlay.height : 480) - 32)
            readonly property real footRoom: (picker.showSearch ? 56 : 0) + (releaseList.count === 0 ? 48 : 0) + (
                                                 picker.mod.hasNightlies ? 42 : 0) + (picker.mod.hasPrereleases ? 42 : 0) + (
                                                 deleteButton.visible ? 40 : 0) + 16

            boundsBehavior: Flickable.StopAtBounds
            clip: true
            contentHeight: body.implicitHeight
            contentWidth: width
            flickableDirection: Flickable.VerticalFlick
            implicitHeight: Math.min(body.implicitHeight, cap)
            interactive: contentHeight > height

            ScrollBar.vertical: GlassScrollBar {
            }

            Column {
                id: body

                spacing: 2
                width: sheet.width

                Item {
                    id: searchRow

                    height: 34
                    visible: picker.showSearch
                    width: 324

                    Rectangle {
                        anchors.fill: parent
                        border.color: searchInput.activeFocus ? Theme.glassMuted : Theme.glassLine
                        border.width: 1.5
                        color: "transparent"
                        radius: 12
                    }

                    Icon {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassFaint
                        name: "search"
                        size: 15
                        stroke: 2
                    }

                    TextInput {
                        id: searchInput

                        activeFocusOnTab: true
                        anchors.left: parent.left
                        anchors.leftMargin: 34
                        anchors.right: clearSearch.visible ? clearSearch.left : parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        clip: true
                        color: Theme.glassText
                        font.family: Theme.font
                        font.pixelSize: 13
                        selectByMouse: true
                        selectedTextColor: Theme.glass
                        selectionColor: Theme.glassMuted

                        Keys.onEscapePressed: {
                            if (text !== "") {
                                text = "";
                                releases.filterText = "";
                            } else {
                                pop.close();
                            }
                        }
                        onTextEdited: releases.filterText = text

                        VText {
                            color: Theme.glassFaint
                            font.pixelSize: 13
                            text: "Search versions"
                            visible: parent.text === ""
                        }
                    }

                    Item {
                        id: clearSearch

                        activeFocusOnTab: visible
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        height: 24
                        visible: searchInput.text !== ""
                        width: 24

                        Keys.onReturnPressed: {
                            searchInput.text = "";
                            releases.filterText = "";
                        }
                        Keys.onSpacePressed: {
                            searchInput.text = "";
                            releases.filterText = "";
                        }

                        Rectangle {
                            anchors.fill: parent
                            border.color: clearSearch.activeFocus ? Theme.ledBlue : "transparent"
                            border.width: 2
                            color: clearMouse.containsMouse ? Theme.glassHover : "transparent"
                            radius: 12
                        }

                        Icon {
                            anchors.centerIn: parent
                            color: Theme.glassFaint
                            name: "x"
                            size: 13
                            stroke: 2.2
                        }

                        MouseArea {
                            id: clearMouse

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true

                            onClicked: {
                                searchInput.text = "";
                                releases.filterText = "";
                            }
                        }
                    }
                }

                VText {
                    color: Theme.glassFaint
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    text: releases.filterText !== "" ? releaseList.count + " of " + releases.totalCount + " versions" :
                                                       releaseList.count + (releaseList.count === 1 ? " version" :
                                                                                                      " versions")
                    visible: picker.showSearch && releases.totalCount > 0
                    width: 324
                }

                ListView {
                    id: releaseList

                    clip: true
                    height: Math.min(contentHeight, Math.max(92, sheet.cap - sheet.footRoom))
                    interactive: contentHeight > height
                    model: releases
                    width: 324

                    ScrollBar.vertical: GlassScrollBar {
                    }
                    delegate: Item {
                        id: row

                        readonly property bool downloading: GameStatus.isDownloading(picker.mod, release,
                                                                                     GameStatus.revision)
                        readonly property string kind: !release ? "" : release.nightly ? "Nightly, " : release.prerelease
                                                                                         ? "Prerelease, " : ""
                        required property var model
                        readonly property ModRelease release: picker.mod.releaseFromId(model.id)
                        readonly property bool selected: picker.current !== null && picker.current.id === model.id
                        // Whatever the name leaves over on the right: the Get button, else the status, else nothing
                        readonly property var trail: getButton.visible ? getButton : statusText.visible ? statusText : null

                        ToolTip.delay: 500
                        ToolTip.text: row.model.name
                        ToolTip.visible: rowMouse.containsMouse && nameText.truncated
                        activeFocusOnTab: true
                        height: 46
                        width: 324

                        Keys.onReturnPressed: picker.mod.setCurrentRelease(row.model.id)
                        Keys.onSpacePressed: picker.mod.setCurrentRelease(row.model.id)

                        Rectangle {
                            anchors.fill: parent
                            color: row.selected || row.activeFocus ? Theme.glassRaised : rowMouse.containsMouse ? Theme.glassHover :
                                                                                                                  "transparent"
                            radius: 12
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -2
                            border.color: Theme.ledBlue
                            border.width: 2
                            color: "transparent"
                            radius: 12
                            visible: row.activeFocus
                            z: 2
                        }

                        Led {
                            anchors.verticalCenter: parent.verticalCenter
                            blinking: row.downloading
                            color: row.release && (row.release.downloaded || row.downloading) ? Theme.ledGreen : Theme.ledOff
                            size: 7
                            x: 14
                        }

                        // Long names take whatever the status text and the Get button leave over, and the
                        // full name is one hover away
                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 32
                            anchors.right: row.trail ? row.trail.left : parent.right
                            anchors.rightMargin: row.trail ? 8 : 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            VText {
                                id: nameText

                                elide: Text.ElideRight
                                font.pixelSize: 13
                                font.weight: row.selected ? Font.ExtraBold : Font.Medium
                                text: row.model.name
                                width: parent.width
                            }

                            VText {
                                color: Theme.glassFaint
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                text: row.kind + row.model.timestamp.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                                width: parent.width
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
                            id: statusText

                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.glassFaint
                            font.pixelSize: 12
                            text: row.downloading ? "Downloading" : "On disk"
                            visible: row.downloading || (row.release !== null && row.release.downloaded)
                        }

                        VButton {
                            id: getButton

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

                VText {
                    color: Theme.glassMuted
                    font.pixelSize: 13
                    text: {
                        if (releases.totalCount === 0)
                            return "Looking for releases. Check your connection if this doesn't change.";
                        if (releases.filterText !== "")
                            return 'No versions match "' + releases.filterText + '".';
                        return "No versions to show with these options.";
                    }
                    visible: releaseList.count === 0
                    width: 324
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    color: Theme.glassLine
                    height: 1
                    visible: nightlyRow.visible || prereleaseRow.visible || deleteButton.visible
                    width: 324
                }

                Item {
                    id: nightlyRow

                    height: 40
                    visible: picker.mod.hasNightlies
                    width: 324

                    VText {
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassMuted
                        font.pixelSize: 13
                        text: "Include nightly builds"
                        x: 14
                    }

                    ToggleSwitch {
                        id: nightlySwitch

                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        checked: releases.showNightlies

                        onToggled: releases.showNightlies ? picker.hideNightlies() : releases.showNightlies = true
                    }
                }

                Item {
                    id: prereleaseRow

                    height: 40
                    visible: picker.mod.hasPrereleases
                    width: 324

                    VText {
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassMuted
                        font.pixelSize: 13
                        text: "Include prereleases"
                        x: 14
                    }

                    ToggleSwitch {
                        id: prereleaseSwitch

                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        checked: releases.showPrereleases

                        onToggled: releases.showPrereleases ? picker.hidePrereleases() : releases.showPrereleases = true
                    }
                }

                VButton {
                    id: deleteButton

                    ToolTip.delay: 500
                    ToolTip.text: picker.current ? picker.mod.releaseTitle(picker.current) : ""
                    ToolTip.visible: hovered && picker.current !== null
                    icon: "trash"
                    small: true
                    text: "Delete this version"
                    visible: picker.current !== null && picker.current.downloaded
                    width: Math.min(implicitWidth, 324)

                    onClicked: {
                        const mod = picker.mod;
                        const release = picker.current;
                        pop.close();
                        Nav.confirm("Delete " + mod.releaseTitle(release) + "?",
                                    "This removes the downloaded files. You can download it again any time.", "Delete", ()
                                    => mod.deleteRelease(release), true);
                    }
                }
            }
        }
        enter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    duration: Theme.durationFast
                    easing.type: Theme.easeOut
                    from: 0
                    property: "opacity"
                    to: 1
                }

                NumberAnimation {
                    duration: Theme.durationMed
                    easing.type: Theme.easeOut
                    from: 0.96
                    property: "scale"
                    to: 1
                }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation {
                    duration: Theme.durationFast
                    easing.type: Theme.easeIn
                    from: 1
                    property: "opacity"
                    to: 0
                }

                NumberAnimation {
                    duration: Theme.durationFast
                    easing.type: Theme.easeIn
                    from: 1
                    property: "scale"
                    to: 0.98
                }
            }
        }

        Component.onDestruction: if (visible)
                                     Nav.popupShown(false)
        onClosed: {
            // A search typed here must not hide releases the next time the menu opens
            releases.filterText = "";
            searchInput.text = "";
        }
        onOpened: Qt.callLater(() => {
            const first = releaseList.itemAtIndex(0);
            if (first)
                first.forceActiveFocus();
            else if (nightlySwitch.visible)
                nightlySwitch.forceActiveFocus();
            else if (prereleaseSwitch.visible)
                prereleaseSwitch.forceActiveFocus();
        })
        onVisibleChanged: Nav.popupShown(visible)
    }
}
