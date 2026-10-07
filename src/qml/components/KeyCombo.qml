import QtQuick
import QtQml.Models

import dev.lorendb.kaon

// Records a hotkey by pressing it. Modifiers and the key are written as "ctrl, f1".
Column {
    id: grab

    // F1–F12 with every modifier chord. The shortcut map receives function keys even when the field is handed a code
    // it does not know.
    readonly property var fKeys: {
        const groups = [["", []], ["Ctrl+", ["ctrl"]], ["Shift+", ["shift"]], ["Alt+", ["alt"]], ["Ctrl+Shift+", ["ctrl",
                                                                                                                  "shift"]],
                        ["Ctrl+Alt+", ["ctrl", "alt"]], ["Shift+Alt+", ["shift", "alt"]], ["Ctrl+Shift+Alt+", ["ctrl", "shift",
                                                                                                               "alt"]]];
        const out = [];
        for (let i = 1; i <= 12; ++i) {
            for (let g = 0; g < groups.length; ++g) {
                out.push({
                             "combo": groups[g][1].concat(["f" + i]).join(", "),
                             "sequence": groups[g][0] + "F" + i
                         });
            }
        }
        return out;
    }
    property bool listening: false
    property var options: [] // [{ id, label }]
    property var preview: []
    property string value

    signal edited(string combo)

    function begin() {
        preview = [];
        listening = true;
    }

    function cancelCapture() {
        preview = [];
        listening = false;
    }

    function commitCombo(combo) {
        if (!grab.listening)
            return;
        const parts = [];
        const split = combo.split(",");
        for (let i = 0; i < split.length; ++i) {
            const token = split[i].trim();
            if (token !== "")
                parts.push(token);
        }
        grab.preview = parts;
        grab.edited(combo);
        // listening stays true until this returns. A shortcut is what called us, and clearing it now would destroy
        // that shortcut before its activation finished.
        Qt.callLater(() => grab.listening = false);
    }

    // Qt.Key_F1 when the platform translated it. Otherwise the X11/Wayland keysym, the Windows virtual key, or the
    // scan code. Wayland often delivers function keys as one of those instead of Qt.Key_F1.
    function functionKey(event) {
        const key = Number(event.key);
        const f1 = Number(Qt.Key_F1);
        if (key >= f1 && key <= Number(Qt.Key_F12))
            return "f" + (key - f1 + 1);
        const sym = Number(event.nativeVirtualKey);
        if (Qt.platform.os === "windows" && sym >= 0x70 && sym <= 0x7b)
            return "f" + (sym - 0x70 + 1);
        if (sym >= 0xffbe && sym <= 0xffc9)
            return "f" + (sym - 0xffbe + 1);
        if (key >= 0xffbe && key <= 0xffc9)
            return "f" + (key - 0xffbe + 1);
        if (key !== 0 && key !== Number(Qt.Key_unknown))
            return "";
        const scan = Number(event.nativeScanCode);
        const plugin = Qt.platform.pluginName;
        if (plugin === "wayland" || (plugin !== "xcb" && Qt.platform.os === "linux")) {
            if (scan >= 59 && scan <= 68)
                return "f" + (scan - 58);
            if (scan === 87)
                return "f11";
            if (scan === 88)
                return "f12";
        }
        if (plugin === "xcb") {
            if (scan >= 67 && scan <= 76)
                return "f" + (scan - 66);
            if (scan === 95)
                return "f11";
            if (scan === 96)
                return "f12";
        }
        if (Qt.platform.os === "windows") {
            if (scan >= 0x3b && scan <= 0x44)
                return "f" + (scan - 0x3b + 1);
            if (scan === 0x57)
                return "f11";
            if (scan === 0x58)
                return "f12";
        }
        return "";
    }

    function labelFor(id) {
        for (let i = 0; i < grab.options.length; ++i) {
            if (grab.options[i].id === id)
                return grab.options[i].label;
        }
        return id;
    }

    function modifierNames(event) {
        const names = [];
        if (event.modifiers & Qt.ControlModifier)
            names.push("ctrl");
        if (event.modifiers & Qt.ShiftModifier)
            names.push("shift");
        if (event.modifiers & Qt.AltModifier)
            names.push("alt");
        return names;
    }

    function orderKeys(list) {
        const rank = {
            "alt": 2,
            "ctrl": 0,
            "shift": 1
        };
        const mods = [];
        const rest = [];
        for (let i = 0; i < list.length; ++i) {
            if (rank[list[i]] !== undefined)
                mods.push(list[i]);
            else
                rest.push(list[i]);
        }
        mods.sort((a, b) => rank[a] - rank[b]);
        return mods.concat(rest);
    }

    function showText() {
        const parts = grab.listening ? grab.preview : grab.tokens();
        if (parts.length === 0)
            return grab.listening ? "Press a combination" : "Click, then press keys";
        const labels = [];
        for (let i = 0; i < parts.length; ++i)
            labels.push(grab.labelFor(parts[i]));
        return labels.join(" + ");
    }

    // The key that finishes a combination. Modifiers are read from the event instead.
    function tokenFor(event) {
        const fn = grab.functionKey(event);
        if (fn !== "")
            return fn;
        const key = event.key;
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key).toLowerCase();
        if ((event.modifiers & Qt.KeypadModifier) && key >= Qt.Key_0 && key <= Qt.Key_9)
            return "num" + (key - Qt.Key_0);
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key);
        const named = {};
        named[Qt.Key_Escape] = "esc";
        named[Qt.Key_Tab] = "tab";
        named[Qt.Key_Space] = "space";
        named[Qt.Key_Return] = "enter";
        named[Qt.Key_Enter] = "enter";
        named[Qt.Key_Backspace] = "backspace";
        named[Qt.Key_Pause] = "pause";
        named[Qt.Key_PageUp] = "pgup";
        named[Qt.Key_PageDown] = "pgdown";
        named[Qt.Key_Home] = "home";
        named[Qt.Key_End] = "end";
        named[Qt.Key_Left] = "left";
        named[Qt.Key_Up] = "up";
        named[Qt.Key_Right] = "right";
        named[Qt.Key_Down] = "down";
        named[Qt.Key_Insert] = "insert";
        named[Qt.Key_Delete] = "delete";
        named[Qt.Key_Print] = "print";
        return named[key] ?? "";
    }

    function tokens() {
        const out = [];
        const parts = grab.value.split(",");
        for (let i = 0; i < parts.length; ++i) {
            const token = parts[i].trim();
            if (token !== "")
                out.push(token);
        }
        return out;
    }

    function unique(list) {
        const out = [];
        for (let i = 0; i < list.length; ++i) {
            if (out.indexOf(list[i]) < 0)
                out.push(list[i]);
        }
        return out;
    }

    spacing: 6

    Component.onDestruction: if (listening)
                                 Nav.capturingKeys = false
    onListeningChanged: Nav.capturingKeys = listening

    Instantiator {
        model: grab.listening ? grab.fKeys : []

        delegate: Shortcut {
            required property var modelData

            enabled: grab.listening
            sequence: modelData.sequence

            onActivated: grab.commitCombo(modelData.combo)
        }
    }

    Item {
        id: box

        function cancelCapture() {
            grab.cancelCapture();
        }

        activeFocusOnTab: true
        height: 38
        width: parent.width

        Keys.onPressed: event => {
            if (!grab.listening) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    grab.begin();
                    event.accepted = true;
                } else if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && grab.tokens().length > 0) {
                    grab.edited("");
                    event.accepted = true;
                }
                return;
            }

            // Bare Esc backs out of recording. Esc with a modifier can still be the hotkey.
            if (event.key === Qt.Key_Escape && !(event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier
                                                                    | Qt.AltModifier))) {
                grab.cancelCapture();
                event.accepted = true;
                return;
            }
            if (event.isAutoRepeat || event.key === Qt.Key_Control || event.key === Qt.Key_Shift || event.key === Qt.Key_Alt
                    || event.key === Qt.Key_Meta) {
                grab.preview = grab.orderKeys(grab.modifierNames(event));
                event.accepted = true;
                return;
            }

            const token = grab.tokenFor(event);
            if (token === "") {
                event.accepted = true;
                return;
            }
            grab.commitCombo(grab.orderKeys(grab.unique(grab.modifierNames(event).concat([token]))).join(", "));
            event.accepted = true;
        }
        Keys.onReleased: event => {
            if (!grab.listening || event.isAutoRepeat)
                return;
            // The released modifier is already gone, so this is whatever is still held.
            grab.preview = grab.orderKeys(grab.modifierNames(event));
            event.accepted = true;
        }
        onActiveFocusChanged: if (!activeFocus)
                                  cancelCapture()

        Rectangle {
            anchors.fill: parent
            border.color: grab.listening ? Theme.ledBlue : box.activeFocus ? Theme.glassMuted : Theme.glassLine
            border.width: grab.listening ? 2 : 1.5
            color: Theme.glassPanel
            radius: 12
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            border.color: Theme.ledBlue
            border.width: 2
            color: "transparent"
            radius: 14
            visible: box.activeFocus && !grab.listening
        }

        VText {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.right: clearMouse.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            color: (grab.listening ? grab.preview.length === 0 : grab.tokens().length === 0) ? Theme.glassFaint :
                                                                                               Theme.glassText

            elide: Text.ElideRight
            font.pixelSize: 14
            text: grab.showText()
        }

        MouseArea {
            id: clearMouse

            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            cursorShape: Qt.PointingHandCursor
            height: 30
            hoverEnabled: true
            visible: !grab.listening && grab.tokens().length > 0
            width: visible ? clearText.implicitWidth + 16 : 0

            onClicked: grab.edited("")

            VText {
                id: clearText

                anchors.centerIn: parent
                color: clearMouse.containsMouse ? Theme.glassText : Theme.glassMuted
                font.pixelSize: 13
                font.weight: Font.Bold
                text: "Clear"
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: clearMouse.width
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            onClicked: {
                box.forceActiveFocus();
                grab.listening ? grab.cancelCapture() : grab.begin();
            }
        }
    }

    VText {
        color: Theme.glassFaint
        font.pixelSize: 12
        text: "Press the keys together. Esc cancels."
        visible: grab.listening
        width: parent.width
    }
}
