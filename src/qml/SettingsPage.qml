import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

Pane {
    objectName: "theOneAndOnlySettingsPage"

    FontInfo {
        id: fontInfo

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
