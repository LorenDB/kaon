import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

Pane {
    objectName: "theOneAndOnlySettingsPage"

    FontInfo { id: fontInfo }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 10
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            spacing: 10

            Label {
                text: "Privacy"
                font.bold: true
                font.pointSize: fontInfo.pointSize * 1.4
            }

            CheckBox {
                text: "Send anonymous usage information"
                Component.onCompleted: checked = Aptabase.enabled
                onCheckedChanged: Aptabase.enabled = checked
            }

            MenuSeparator {}

            Label {
                text: "Info"
                font.bold: true
                font.pointSize: fontInfo.pointSize * 1.4
            }

            Label {
                text: Steam.storeRoot === "" ? "No Steam installation detected" : "Steam install detected: " + Steam.storeRoot
            }

            Label {
                text: Heroic.storeRoot === "" ? "No Heroic installation detected" : "Heroic install detected: " + Heroic.storeRoot
            }

            Label {
                text: Itch.storeRoot === "" ? "No Itch installation detected" : "Itch install detected: " + Itch.storeRoot
            }

            Item { Layout.fillHeight: true }
        }
    }
}
