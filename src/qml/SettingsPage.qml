import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

Pane {
    objectName: "theOneAndOnlySettingsPage"

    FontInfo {
        id: fontInfo

    }

    Settings {
        id: uevrSettings

        property alias launchDelay: launchDelaySpin.value

        category: "uevr"
    }

    ScrollView {
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        anchors.fill: parent
        anchors.margins: 10

        ColumnLayout {
            spacing: 10

            Label {
                font.bold: true
                font.pointSize: fontInfo.pointSize * 1.4
                text: "UEVR"
            }

            RowLayout {
                spacing: 10

                Label {
                    text: "When launching the injector with a game, wait this long for the game to start:"
                }

                SpinBox {
                    id: launchDelaySpin

                    property string prefix: ""
                    property string suffix: " seconds"

                    value: 30
                    textFromValue: function(value, locale) {
                        return prefix + Number(value).toLocaleString(locale, 'f', 0) + suffix
                    }
                    valueFromText: function(text, locale) {
                        let re = /\D*(-?\d*\.?\d*)\D*/
                        return Number.fromLocaleString(locale, re.exec(text)[1])
                    }
                }
            }

            MenuSeparator {
            }

            Label {
                font.bold: true
                font.pointSize: fontInfo.pointSize * 1.4
                text: "Updates"
            }

            RowLayout {
                spacing: 10

                CheckBox {
                    text: "Notify when an update is available"

                    Component.onCompleted: checked = UpdateChecker.enabled
                    onCheckedChanged: UpdateChecker.enabled = checked
                }

                Button {
                    text: "Check now"

                    onClicked: UpdateChecker.checkUpdates()
                }
            }

            MenuSeparator {
            }

            Label {
                font.bold: true
                font.pointSize: fontInfo.pointSize * 1.4
                text: "Privacy"
            }

            CheckBox {
                text: "Send anonymous usage information"

                Component.onCompleted: checked = Aptabase.enabled
                onCheckedChanged: Aptabase.enabled = checked
            }

            MenuSeparator {
            }

            Label {
                font.bold: true
                font.pointSize: fontInfo.pointSize * 1.4
                text: "Info"
            }

            Label {
                text: Steam.storeRoot === "" ? "No Steam installation detected" : "Steam install detected: "
                                               + Steam.storeRoot
            }

            Label {
                text: Heroic.storeRoot === "" ? "No Heroic installation detected" : "Heroic install detected: "
                                                + Heroic.storeRoot
            }

            Label {
                text: Itch.storeRoot === "" ? "No Itch installation detected" : "Itch install detected: " + Itch.storeRoot
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
