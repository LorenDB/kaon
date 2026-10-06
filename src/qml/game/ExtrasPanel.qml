import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// One line per extra, with the description on hover. Download cards stay on the Mods page.
Column {
    id: panel

    property Game game: null
    property var mods: []
    readonly property var rows: ordered(mods)
    property bool showLaunchOptions: false
    property bool spaced: false

    function detailFor(mod, installed, hold, busy, downloading) {
        if (hold !== "")
            return hold;
        if (downloading)
            return "Downloading";
        if (busy)
            return "Installing";
        if (!installed && !mod.currentRelease)
            return "Looking for a download";
        if (!installed)
            return "";
        if (mod.settingsGroup === "vrperfkit")
            return "Edit vrperfkit.yml next to the game.";
        if (mod.settingsGroup === "openxr-cas")
            return "Enabled in this game's prefix.";
        return "";
    }

    function ordered(list) {
        const copy = list ? list.slice() : [];
        copy.sort((a, b) => a.name.localeCompare(b.name));
        return copy;
    }

    spacing: 8
    visible: rows.length > 0
    width: parent ? parent.width : 0

    VText {
        color: Theme.glassFaint
        font.pixelSize: 12
        font.weight: Font.Bold
        text: "Optional"
        topPadding: panel.spaced ? 8 : 0
        width: parent.width
    }

    Repeater {
        model: panel.rows

        Item {
            id: row

            readonly property bool busy: (GameStatus.revision, modelData.isBusyForGame(panel.game))
            readonly property string detail: panel.detailFor(modelData, installed, hold, busy, downloading)
            readonly property bool downloading: modelData.currentRelease !== null && (GameStatus.revision,
                                                                                      GameStatus.isDownloading(modelData,
                                                                                                               modelData.currentRelease))
            // "Quit the game" can go stale while this page stays open, so that case stays clickable.
            readonly property bool held: hold !== "" && !hold.startsWith("Quit the game")
            readonly property string hold: (GameStatus.revision, modelData.installHoldReason(panel.game))
            readonly property bool installed: (GameStatus.revision, modelData.isInstalledForGame(panel.game))
            required property var modelData

            ToolTip.delay: 400
            ToolTip.text: row.modelData.description
            ToolTip.visible: hover.hovered && row.modelData.description !== ""
            height: Math.max(labelCol.implicitHeight, sw.implicitHeight)
            width: panel.width

            HoverHandler {
                id: hover
            }

            Column {
                id: labelCol

                anchors.left: parent.left
                anchors.right: sw.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Row {
                    width: parent.width

                    VText {
                        id: nameText

                        elide: Text.ElideRight
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        text: row.modelData.name
                        width: Math.min(implicitWidth, parent.width - suffixText.implicitWidth)
                    }

                    VText {
                        id: suffixText

                        font.pixelSize: 14
                        font.weight: Font.Normal
                        text: row.modelData.vrRuntime !== "" ? " · " + row.modelData.vrRuntime : ""
                        visible: text !== ""
                    }
                }

                VText {
                    color: Theme.glassMuted
                    font.pixelSize: 12
                    text: row.detail
                    visible: text !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                }
            }

            ToggleSwitch {
                id: sw

                anchors.right: parent.right
                anchors.top: parent.top
                checked: row.installed
                enabled: !row.held && !row.busy && !row.downloading && (row.installed || row.modelData.currentRelease
                                                                        !== null)

                onToggled: {
                    const mod = row.modelData;
                    if (!panel.game || mod.isBusyForGame(panel.game))
                        return;
                    const reason = mod.installHoldReason(panel.game);
                    if (reason !== "") {
                        Nav.notify(reason);
                        return;
                    }
                    if (row.installed)
                        mod.uninstallMod(panel.game);
                    else if (!mod.currentRelease)
                        Nav.notify("Still looking for a " + mod.name + " download");
                    else
                        GameStatus.installForGame(panel.game, mod);
                }
            }
        }
    }

    LaunchOptions {
        options: panel.showLaunchOptions && panel.game ? (GameStatus.revision, GameStatus.launchOptions(panel.game)) : ""
    }
}
