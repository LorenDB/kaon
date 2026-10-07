import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// Settings for one mod's installed config file. Nothing is written until Save.
Item {
    id: view

    readonly property ModConfigDocument doc: ModConfigs.document

    function saveConfig() {
        if (!view.doc)
            return;
        if (view.doc.save())
            Nav.notify("Saved " + view.doc.title + " settings");
        else
            Nav.notify(view.doc.error);
    }

    // The page is created when it opens. Land at the top after focus has had a chance to shove the scroll.
    Component.onCompleted: Qt.callLater(() => flick.contentY = 0)

    Flickable {
        id: flick

        // How much of the top the way back covers, for whatever scrolls a focused field into view
        readonly property real pinnedTop: 60

        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentHeight: column.y + column.height + Theme.notchClearance + 84

        ScrollBar.vertical: GlassScrollBar {
        }

        Column {
            id: column

            spacing: 14
            width: Math.min(560, view.width - 2 * Theme.pad)
            x: Theme.pad
            y: 72

            VText {
                color: Theme.glassMuted
                font.pixelSize: 14
                font.weight: Font.Normal
                lineHeight: 1.35
                text: view.doc ? view.doc.note : ""
                width: parent.width
                wrapMode: Text.Wrap
            }

            VText {
                color: Theme.glassFaint
                elide: Text.ElideMiddle
                font.pixelSize: 12
                text: view.doc ? view.doc.path : ""
                width: parent.width
            }

            VText {
                color: Theme.ledRed
                font.pixelSize: 13
                text: view.doc ? view.doc.error : ""
                visible: text !== ""
                width: parent.width
                wrapMode: Text.Wrap
            }

            VButton {
                quiet: true
                small: true
                text: "Reset to download"
                visible: view.doc && view.doc.defaultsReady

                onClicked: Nav.confirm("Reset these settings?", "This replaces the file with the copy from the download.",
                                       "Reset", () => {
                                           if (!view.doc)
                                               return;
                                           if (view.doc.resetToDownload())
                                               Nav.notify("Restored the settings from the download");
                                           else
                                               Nav.notify(view.doc.error);
                                       }, true)
            }

            Repeater {
                model: view.doc

                Column {
                    id: row

                    required property var choices
                    required property string detail
                    required property string heading
                    required property int index
                    required property string kind
                    required property string label
                    required property string shipped
                    required property string value

                    spacing: 6
                    width: column.width

                    VText {
                        bottomPadding: 2
                        color: Theme.glassFaint
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        text: row.heading
                        topPadding: 10
                        visible: row.heading !== ""
                    }

                    Item {
                        height: row.kind === "bool" ? Math.max(words.height, toggle.height) : words.height + field.height + 8
                        width: parent.width

                        Column {
                            id: words

                            anchors.left: parent.left
                            anchors.right: row.kind === "bool" ? toggle.left : parent.right
                            anchors.rightMargin: row.kind === "bool" ? 12 : 0
                            spacing: 2
                            width: parent.width - (row.kind === "bool" ? toggle.width + 12 : 0)

                            VText {
                                font.pixelSize: 14
                                font.weight: Font.Bold
                                text: row.label
                                width: parent.width
                                wrapMode: Text.Wrap
                            }

                            VText {
                                color: Theme.glassFaint
                                font.pixelSize: 12
                                font.weight: Font.Normal
                                lineHeight: 1.3
                                text: row.detail
                                visible: text !== ""
                                width: parent.width
                                wrapMode: Text.Wrap
                            }
                        }

                        ToggleSwitch {
                            id: toggle

                            anchors.right: parent.right
                            anchors.verticalCenter: words.verticalCenter
                            checked: row.value === "true"
                            visible: row.kind === "bool"

                            onToggled: view.doc.setField(row.index, row.value === "true" ? "false" : "true")
                        }

                        Column {
                            id: field

                            anchors.top: words.bottom
                            anchors.topMargin: 8
                            spacing: 0
                            visible: row.kind !== "bool"
                            width: parent.width

                            Segmented {
                                current: row.value
                                options: row.choices
                                visible: row.kind === "choice"

                                onPicked: key => view.doc.setField(row.index, key)
                            }

                            EyeOrder {
                                options: row.choices
                                value: row.value
                                visible: row.kind === "text" && row.choices.length > 0
                                width: parent.width

                                onEdited: text => view.doc.setField(row.index, text)
                            }

                            KeyCombo {
                                options: row.choices
                                value: row.value
                                visible: row.kind === "keys"
                                width: parent.width

                                onEdited: combo => view.doc.setField(row.index, combo)
                            }

                            TextBox {
                                text: row.kind === "number" || (row.kind === "text" && row.choices.length === 0) ? row.value :
                                                                                                                   ""
                                visible: row.kind === "number" || (row.kind === "text" && row.choices.length === 0)
                                width: parent.width

                                onTextChanged: if (visible && text !== row.value)
                                                   view.doc.setField(row.index, text)
                            }
                        }
                    }

                    VButton {
                        quiet: true
                        small: true
                        text: "Reset"
                        visible: view.doc.defaultsReady && row.value !== row.shipped

                        onClicked: view.doc.resetField(row.index)
                    }
                }
            }
        }
    }

    BackBar {
        label: "Game"
        raised: flick.contentY > 8
        title: view.doc ? view.doc.title : ""
        width: view.width

        onBack: Nav.back()
    }

    // Pinned above the notch while the form scrolls under it, so Save is at hand
    // at any scroll level while there is something to save.
    Item {
        id: saveBar

        readonly property bool dirty: view.doc && view.doc.dirty

        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.notchClearance + 12
        enabled: dirty
        height: 60
        opacity: dirty ? 1 : 0
        visible: opacity > 0.01
        width: Math.min(560, view.width - 2 * Theme.pad)
        x: Theme.pad

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationMed
                easing.type: Theme.easeOut
            }
        }

        transform: Translate {
            y: saveBar.dirty ? 0 : 12

            Behavior on y {
                NumberAnimation {
                    duration: Theme.durationMed
                    easing.type: Theme.easeOut
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            border.color: Theme.glassLine
            border.width: 1
            color: Theme.glassPanel
            radius: 16
        }

        VText {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: revertButton.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassMuted
            elide: Text.ElideRight
            font.pixelSize: 13
            font.weight: Font.Bold
            text: "Unsaved changes"
        }

        VButton {
            id: revertButton

            anchors.right: saveButton.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            height: implicitHeight
            quiet: true
            small: true
            text: "Revert"
            width: implicitWidth

            onClicked: view.doc.discardChanges()
        }

        VButton {
            id: saveButton

            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            height: implicitHeight
            solid: true
            text: "Save"
            width: implicitWidth

            onClicked: view.saveConfig()
        }
    }
}
