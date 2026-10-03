import QtQuick

import dev.lorendb.kaon

// Launch options as plain text, with a Copy button. The description above is styled text, which can't be selected.
Column {
    id: box

    property string options

    spacing: 6
    visible: options !== ""
    width: parent ? parent.width : 0

    VText {
        color: Theme.glassFaint
        font.pixelSize: 12
        font.weight: Font.Bold
        text: "Launch options"
    }

    Row {
        id: line

        spacing: 8
        width: parent.width

        Rectangle {
            id: well

            border.color: field.activeFocus ? Theme.glassMuted : Theme.glassLine
            border.width: 1.5
            clip: true
            color: Theme.glass
            height: field.height + 16
            radius: 12
            width: Math.max(0, line.width - copyButton.width - line.spacing)

            TextEdit {
                id: field

                activeFocusOnTab: true
                color: Theme.glassText
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.Medium
                height: contentHeight
                persistentSelection: true
                readOnly: true
                selectByKeyboard: true
                selectByMouse: true
                selectedTextColor: Theme.glass
                selectionColor: Theme.glassMuted
                text: box.options
                width: parent.width - 16
                wrapMode: TextEdit.Wrap
                x: 8
                y: 8
            }
        }

        VButton {
            id: copyButton

            small: true
            text: "Copy"

            onClicked: {
                field.selectAll();
                field.copy();
                field.deselect();
                Nav.notify("Copied launch options");
            }
        }
    }
}
