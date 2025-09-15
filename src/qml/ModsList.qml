pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

ListView {
    id: list

    property Game game
    property alias showIncompatible: mfm.showIncompatible

    clip: true

    ScrollBar.vertical: ScrollBar {
        id: sb

    }
    delegate: ItemDelegate {
        id: delegate

        required property Mod mod

        height: 48
        width: ListView.view.width - (sb.visible ? sb.width : 0)

        Settings {
            id: modSettings

            property alias launchDelay: launchDelaySpin.value

            category: delegate.mod.settingsGroup
        }

        RowLayout {
            id: content

            anchors.left: delegate.left
            anchors.margins: 10
            anchors.verticalCenter: delegate.verticalCenter
            spacing: 10
            width: delegate.width - 20

            Label {
                text: delegate.mod.name
            }

            ToolSeparator {
                visible: infoLabel.visible
            }

            Label {
                id: infoLabel

                text: delegate.mod.info
                visible: delegate.mod.info.length > 0
                textFormat: Label.MarkdownText

                onLinkActivated: link => Qt.openUrlExternally(link)
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                enabled: delegate.mod.currentRelease.installed
                text: {
                    if (delegate.mod.type === Mod.Launchable)
                        return "Launch";
                    else if (delegate.mod.type === Mod.Installable) {
                        if (delegate.mod.isInstalledForGame(list.game))
                            return "Repair or uninstall";
                        else
                            return "Install";
                    }
                }
                visible: list.game

                onClicked: {
                    if (!delegate.mod.dependenciesSatisfied(list.game))
                    {
                        missingDependenciesDialog.mod = delegate.mod;
                        missingDependenciesDialog.game = list.game;
                        missingDependenciesDialog.open();
                    }
                    else
                    {
                        if (delegate.mod.type === Mod.Launchable)
                            delegate.mod.launchMod(list.game);
                        else if (delegate.mod.type === Mod.Installable)
                        {
                            if (delegate.mod.isInstalledForGame(list.game))
                                delegate.mod.uninstallMod(list.game);
                            else
                                delegate.mod.installMod(list.game);
                        }
                    }
                }
            }

            Button {
                id: launchWithDelay

                enabled: delegate.mod.currentRelease.installed && delegate.mod.type === Mod.Launchable
                text: {
                    if (delegate.mod.type === Mod.Launchable)
                        return "Launch game, then mod";
                }
                visible: list.game && list.game.canLaunch && delegate.mod.type === Mod.Launchable

                onClicked: {
                    if (!delegate.mod.dependenciesSatisfied(list.game))
                    {
                        missingDependenciesDialog.mod = delegate.mod;
                        missingDependenciesDialog.game = list.game;
                        missingDependenciesDialog.open();
                    }
                    else
                    {
                        list.game.launch();
                        delayedLauncher.start();
                    }
                }

                Timer {
                    id: delayedLauncher

                    interval: launchDelaySpin.value * 1000

                    onTriggered: delegate.mod.launchMod(list.game)
                }
            }

            Label {
                text: "Wait:"
                visible: launchWithDelay.visible
            }

            SpinBox {
                id: launchDelaySpin

                property string prefix: ""
                property string suffix: " seconds"

                textFromValue: function (value, locale) {
                    return prefix + Number(value).toLocaleString(locale, 'f', 0) + suffix;
                }
                value: 30
                valueFromText: function (text, locale) {
                    let re = /\D*(-?\d*\.?\d*)\D*/;
                    return Number.fromLocaleString(locale, re.exec(text)[1]);
                }
                visible: launchWithDelay.visible
            }


            Label {
                text: "Version:"
            }

            ComboBox {
                id: versionCombo

                textRole: "name"
                valueRole: "id"

                model: ModReleaseFilter {
                    id: releaseFilter

                    mod: delegate.mod
                }

                Component.onCompleted: currentIndex = Math.max(0, releaseFilter.indexFromRelease(
                                                                   delegate.mod.currentRelease))
                onActivated: delegate.mod.setCurrentRelease(currentValue)

                Connections {
                    function onCurrentReleaseChanged(i) {
                        versionCombo.currentIndex = Math.max(0, releaseFilter.indexFromRelease(delegate.mod.currentRelease));
                    }

                    target: delegate.mod
                }

                Connections {
                    // If the nightlies filter changes, we need to adjust the index to the new position of the current UEVR.
                    // In some cases, the current UEVR will no longer be available, so we need to change the current UEVR.
                    function onShowNightliesChanged(state) {
                        versionCombo.currentIndex = Math.max(0, releaseFilter.indexFromRelease(delegate.mod.currentRelease));
                        if (!delegate.mod.currentRelease || versionCombo.currentValue !== delegate.mod.currentRelease.id) {
                            versionCombo.currentIndex = 0;
                            if (versionCombo.currentValue !== undefined)
                                delegate.mod.setCurrentRelease(versionCombo.currentValue);
                        }
                    }

                    target: releaseFilter
                }
            }

            ToolButton {
                icon.color: palette.buttonText
                icon.name: "download"
                icon.source: Qt.resolvedUrl("icons/download.svg")
                visible: delegate.mod.currentRelease && !delegate.mod.currentRelease.installed

                onClicked: delegate.mod.downloadRelease(delegate.mod.currentRelease)
            }

            ToolButton {
                icon.color: "#da4453"
                icon.name: "delete"
                icon.source: Qt.resolvedUrl("icons/delete.svg")
                visible: delegate.mod.currentRelease && delegate.mod.currentRelease.installed

                onClicked: {
                    deleteModDialog.mod = delegate.mod;
                    deleteModDialog.open();
                }
            }

            CheckBox {
                text: "Show nightlies"

                Component.onCompleted: checked = releaseFilter.showNightlies
                onCheckedChanged: releaseFilter.showNightlies = checked
            }
        }
    }
    model: ModsFilterModel {
        id: mfm

        game: list.game
    }

    Dialog {
        id: deleteModDialog

        property Mod mod

        closePolicy: Popup.CloseOnEscape
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "Confirm deletion"

        onAccepted: {
            mod.deleteRelease(mod.currentRelease);
            mod = undefined;
        }

        Label {
            anchors.fill: parent
            text: "Are you sure you want to delete " + ((deleteModDialog.mod && deleteModDialog.mod.currentRelease)
                                                        ? deleteModDialog.mod.currentRelease.name : "<null>") + "?"
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: missingDependenciesDialog

        property Mod mod
        property Game game

        closePolicy: Popup.CloseOnEscape
        modal: true
        standardButtons: Dialog.Ok
        title: "Missing dependencies"

        Label {
            anchors.fill: parent
            text: {
                if (!missingDependenciesDialog.mod || !missingDependenciesDialog.game)
                    return "Missing dependencies (unknown error)";
                else
                    return missingDependenciesDialog.mod.name + " requires these mods to be installed first:\n\n"
                           + missingDependenciesDialog.mod.missingDependencies(missingDependenciesDialog.game);
            }
            wrapMode: Text.WordWrap
            textFormat: Text.MarkdownText
        }
    }
}
