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

    function focusDefaultButton() {
        let fallback = null;
        const buttons = buttonRow.children;
        for (let i = 0; i < buttons.length; ++i) {
            const button = buttons[i];
            if (!button.visible || button.enabled === false || button.width < 2)
                continue;
            fallback = button;
            if (button.solid) {
                button.forceActiveFocus();
                return;
            }
        }
        if (fallback)
            fallback.forceActiveFocus();
    }

    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    focus: true
    modal: true
    padding: 24
    // Popup.parent stays the declaring item, which may have no size of its own. Size from the overlay.
    width: Math.min(460, Math.max(280, (Overlay.overlay ? Overlay.overlay.width : 460) - 40))

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

    // A gamepad (and the keyboard) lands on the action that confirms, which is the solid button.
    onOpened: Qt.callLater(dialog.focusDefaultButton)
}
