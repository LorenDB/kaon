import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Every popup Kaon shows: confirmations, failures, updates, and picking an executable.
// Fill the window. These popups are parented here, and a zero-size parent collapses them.
Item {
    anchors.fill: parent

    GlassDialog {
        id: confirmDialog

        property string actionLabel
        property var onConfirm: null

        buttons: [
            VButton {
                small: true
                text: "Cancel"

                onClicked: confirmDialog.close()
            },
            VButton {
                small: true
                solid: true
                text: confirmDialog.actionLabel

                onClicked: {
                    confirmDialog.close();
                    if (confirmDialog.onConfirm)
                        confirmDialog.onConfirm();
                }
            }
        ]
    }

    GlassDialog {
        id: messageDialog

        buttons: [
            VButton {
                small: true
                solid: true
                text: "OK"

                onClicked: messageDialog.close()
            }
        ]
    }

    GlassDialog {
        id: updateDialog

        property string url
        property string version

        text: "Kaon " + version + " is out. The release page has the download and what changed."
        title: "A new version of Kaon is available"

        buttons: [
            VButton {
                small: true
                text: "Skip this version"

                onClicked: {
                    UpdateChecker.ignore = updateDialog.version;
                    updateDialog.close();
                }
            },
            VButton {
                small: true
                text: "Later"

                onClicked: updateDialog.close()
            },
            VButton {
                small: true
                solid: true
                text: "Open release page"

                onClicked: {
                    Qt.openUrlExternally(updateDialog.url);
                    updateDialog.close();
                }
            }
        ]
    }

    GlassDialog {
        id: exeDialog

        property int choice: 0
        property GameExecutablePickerModel model: null

        text: model ? model.game.name + " has more than one executable. Pick the one the mod should go into." : ""
        title: "Choose an executable"

        buttons: [
            VButton {
                small: true
                text: "Cancel"

                onClicked: exeDialog.close()
            },
            VButton {
                small: true
                solid: true
                text: "Use this one"

                onClicked: {
                    const m = exeDialog.model;
                    exeDialog.model = null;
                    m.select(exeDialog.choice);
                    m.destroySelf();
                    exeDialog.close();
                }
            }
        ]

        onClosed: {
            if (model) {
                model.destroySelf();
                model = null;
            }
        }

        Repeater {
            model: exeDialog.model

            Rectangle {
                id: option

                required property int index
                required property string text

                activeFocusOnTab: true
                border.color: exeDialog.choice === index ? Theme.glassMuted : Theme.glassLine
                border.width: 1.5
                color: exeDialog.choice === index || activeFocus ? Theme.glassRaised : "transparent"
                height: 40
                radius: 12
                width: parent.width

                Keys.onReturnPressed: exeDialog.choice = index
                Keys.onSpacePressed: exeDialog.choice = index
                onActiveFocusChanged: if (activeFocus)
                                          exeDialog.choice = index

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    border.color: Theme.ledBlue
                    border.width: 2
                    color: "transparent"
                    radius: 14
                    visible: option.activeFocus
                }

                Led {
                    anchors.verticalCenter: parent.verticalCenter
                    color: exeDialog.choice === option.index ? Theme.ledGreen : Theme.ledOff
                    size: 8
                    x: 14
                }

                VText {
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideMiddle
                    font.pixelSize: 13
                    text: option.text
                    width: parent.width - 50
                    x: 34
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor

                    onClicked: exeDialog.choice = option.index
                }
            }
        }
    }

    Connections {
        function onConfirmRequested(title, text, actionLabel, onConfirm) {
            confirmDialog.title = title;
            confirmDialog.text = text;
            confirmDialog.actionLabel = actionLabel;
            confirmDialog.onConfirm = onConfirm;
            confirmDialog.open();
        }

        target: Nav
    }

    Connections {
        function onInstallFailed(message: string) {
            messageDialog.title = "Portal 1 VR didn't install";
            messageDialog.text = message;
            messageDialog.open();
        }

        target: Portal1VR
    }

    Connections {
        function onDownloadFailed(whatWasBeingDownloaded: string) {
            messageDialog.title = "Download failed";
            messageDialog.text = "Kaon couldn't download " + whatWasBeingDownloaded
                    + ". Check your network connection and try again.";
            messageDialog.open();
        }

        target: DownloadManager
    }

    Connections {
        function onUpdateAvailable(version: string, url: string) {
            updateDialog.version = version;
            updateDialog.url = url;
            updateDialog.open();
        }

        target: UpdateChecker
    }

    Connections {
        function onProcessFailed(prettyName: string) {
            messageDialog.title = (prettyName !== "" ? prettyName : "A program") + " stopped with an error";
            messageDialog.text = "Kaon's log has the details: ~/.cache/LorenDB/Kaon/kaon.log";
            messageDialog.open();
        }

        target: Wine
    }

    Connections {
        function onChooseExecutable(model: GameExecutablePickerModel) {
            exeDialog.choice = 0;
            exeDialog.model = model;
            exeDialog.open();
        }

        target: GameStatus
    }
}
