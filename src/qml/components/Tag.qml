import QtQuick
import QtQuick.Controls

Rectangle {
    id: tagRoot

    property alias text: theLabel.text

    color: "#7e7e7e"
    implicitHeight: theLabel.implicitHeight + 4
    implicitWidth: theLabel.implicitWidth + 4
    radius: 3

    Label {
        id: theLabel

        anchors.centerIn: tagRoot
        font.bold: true
    }
}
