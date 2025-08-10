import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import dev.lorendb.kaon

ApplicationWindow {
    id: root

    visible: true
    title: qsTr("Kaon")
    width: 900
    height: 550

    ColumnLayout {
        anchors.fill: root.contentItem
        anchors.margins: 10
        spacing: 10

        RowLayout {
            spacing: 105

            ComboBox {
                model: UEVR
            }
        }

        RowLayout {
            spacing: 105

            ComboBox {
                model: Steam
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
