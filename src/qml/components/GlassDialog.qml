import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// A modal panel of black glass. Put the body text in `text` or any items as children; buttons go in `buttons`.
Popup {
    id: dialog

    property alias buttons: buttonRow.data
    default property alias content: extra.data
    property string text
    property string title

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    focus: true
    modal: true
    padding: 24
    width: Math.min(460, (parent ? parent.width : 460) - 40)

    Overlay.modal: Rectangle {
        color: "#99050608"
    }
    background: Rectangle {
        border.color: Theme.glassLine
        border.width: 1.5
        color: Theme.glassPanel
        radius: 20
    }
    contentItem: Column {
        spacing: 14

        VText {
            font.pixelSize: 18
            font.weight: Font.ExtraBold
            text: dialog.title
            width: parent.width
            wrapMode: Text.Wrap
        }

        VText {
            color: Theme.glassMuted
            font.pixelSize: 14
            font.weight: Font.Normal
            lineHeight: 1.35
            linkColor: Theme.ledBlue
            text: Theme.linkify(dialog.text)
            textFormat: Text.StyledText
            visible: dialog.text !== ""
            width: parent.width
            wrapMode: Text.Wrap

            onLinkActivated: link => Qt.openUrlExternally(link)
        }

        Column {
            id: extra

            spacing: 8
            width: parent.width
        }

        Row {
            id: buttonRow

            anchors.right: parent.right
            spacing: 8
            topPadding: 6
        }
    }
}
