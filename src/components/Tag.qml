import QtQuick
import QtQuick.Controls

Rectangle {
    id: tagRoot

    property alias text: theLabel.text

    color: "#7e7e7e"
    implicitWidth: theLabel.implicitWidth + 4
    implicitHeight: theLabel.implicitHeight + 4
    radius: 3

    Label {
        id: theLabel

        font.bold: true
        anchors.centerIn: tagRoot
    }
}
