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

            Item { Layout.fillHeight: true }
        }
    }
}
