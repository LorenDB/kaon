import QtQuick

import dev.lorendb.kaon

// One chip per draw. Drag reorders them. Click switches left, right, and skip.
Column {
    id: eyes

    readonly property bool automatic: folded === "" && !editingCustom
    readonly property int cap: 12
    readonly property bool custom: editingCustom || (folded !== "" && !presetThree && !presetAlt)
    property int dragIndex: -1
    readonly property bool dragging: dragIndex >= 0
    // True after Custom is chosen, including when the letters still match a preset.
    property bool editingCustom: false
    readonly property string folded: value.replace(/\s/g, "").toUpperCase()
    property var options: [] // [{ id, label }]
    readonly property int pillWidth: 92
    readonly property bool presetAlt: folded === "LRLRLR"
    readonly property bool presetThree: folded === "LLLRRR"
    readonly property var steps: {
        const out = [];
        const compact = value.replace(/\s/g, "");
        for (let i = 0; i < compact.length; ++i)
            out.push(compact.charAt(i));
        return out;
    }
    readonly property int stride: pillWidth + 8
    property string value

    signal edited(string text)

    function addDraw() {
        const next = eyes.ids();
        if (next.length >= eyes.cap)
            return;
        next.push(eyes.options.length > 0 ? eyes.options[0].id : "L");
        eyes.write(next);
    }

    function canon(id) {
        const folded = id.toUpperCase();
        for (let i = 0; i < eyes.options.length; ++i) {
            if (eyes.options[i].id.toUpperCase() === folded)
                return eyes.options[i].id;
        }
        return id;
    }

    function chooseCustom() {
        eyes.editingCustom = true;
        if (eyes.folded === "")
            eyes.edited("L");
    }

    function choosePreset(text) {
        eyes.editingCustom = false;
        if (eyes.folded !== text)
            eyes.edited(text);
    }

    function cycle(index) {
        const next = eyes.ids();
        if (index < 0 || index >= next.length)
            return;
        next[index] = eyes.nextId(next[index]);
        eyes.write(next);
    }

    function ids() {
        const out = [];
        for (let i = 0; i < eyes.steps.length; ++i)
            out.push(eyes.canon(eyes.steps[i]));
        return out;
    }

    function labelFor(id) {
        const folded = id.toUpperCase();
        for (let i = 0; i < eyes.options.length; ++i) {
            if (eyes.options[i].id.toUpperCase() === folded)
                return eyes.options[i].label;
        }
        return id;
    }

    function move(from, to) {
        const next = eyes.ids();
        if (from < 0 || to < 0 || from >= next.length || to >= next.length || from === to)
            return;
        const step = next.splice(from, 1)[0];
        next.splice(to, 0, step);
        eyes.write(next);
    }

    function nextId(id) {
        const current = eyes.canon(id);
        for (let i = 0; i < eyes.options.length; ++i) {
            if (eyes.options[i].id === current)
                return eyes.options[(i + 1) % eyes.options.length].id;
        }
        return eyes.options.length > 0 ? eyes.options[0].id : current;
    }

    function removeAt(index) {
        const next = eyes.ids();
        if (index < 0 || index >= next.length)
            return;
        next.splice(index, 1);
        eyes.write(next);
    }

    function write(next) {
        if (next.length === 0) {
            eyes.editingCustom = false;
            if (eyes.folded !== "")
                eyes.edited("");
            return;
        }
        const joined = next.join("");
        if (joined !== eyes.value.replace(/\s/g, ""))
            eyes.edited(joined);
    }

    spacing: 8
    width: parent.width

    Flow {
        spacing: 8
        width: parent.width

        VButton {
            height: 38
            small: true
            solid: eyes.automatic
            text: "Automatic"

            onClicked: eyes.choosePreset("")
        }

        VButton {
            height: 38
            small: true
            solid: eyes.presetThree && !eyes.editingCustom
            text: "Three left, then right"

            onClicked: eyes.choosePreset("LLLRRR")
        }

        VButton {
            height: 38
            small: true
            solid: eyes.presetAlt && !eyes.editingCustom
            text: "Alternating"

            onClicked: eyes.choosePreset("LRLRLR")
        }

        VButton {
            height: 38
            small: true
            solid: eyes.custom
            text: "Custom"

            onClicked: eyes.chooseCustom()
        }

        VButton {
            height: 38
            small: true
            text: "Add draw"
            visible: eyes.custom && eyes.steps.length < eyes.cap

            onClicked: eyes.addDraw()
        }
    }

    Flickable {
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        contentWidth: track.width
        flickableDirection: Flickable.HorizontalFlick
        height: eyes.custom && eyes.steps.length > 0 ? 38 : 0
        interactive: !eyes.dragging
        visible: eyes.custom && eyes.steps.length > 0
        width: parent.width

        Item {
            id: track

            height: 38
            width: Math.max(eyes.steps.length * eyes.stride - 8, 0)

            Repeater {
                model: eyes.steps

                Item {
                    id: pill

                    required property int index
                    required property string modelData

                    activeFocusOnTab: true
                    height: 38
                    width: eyes.pillWidth
                    x: index * eyes.stride
                    z: dragArea.drag.active ? 2 : 1

                    Keys.onDeletePressed: eyes.removeAt(index)
                    Keys.onReturnPressed: eyes.cycle(index)
                    Keys.onSpacePressed: eyes.cycle(index)

                    Rectangle {
                        anchors.fill: parent
                        border.color: pill.activeFocus ? Theme.ledBlue : dragArea.containsMouse ? Theme.glassMuted :
                                                                                                  Theme.glassLine

                        border.width: pill.activeFocus || dragArea.drag.active ? 2 : 1.5
                        color: Theme.glassPanel
                        radius: height / 2
                    }

                    VText {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: removeMouse.left
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.glassText
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        text: eyes.labelFor(pill.modelData)
                    }

                    MouseArea {
                        id: dragArea

                        property real origin: 0

                        anchors.fill: parent
                        anchors.rightMargin: removeMouse.width
                        cursorShape: Qt.PointingHandCursor
                        drag.axis: Drag.XAxis
                        drag.maximumX: Math.max(0, (eyes.steps.length - 1) * eyes.stride)
                        drag.minimumX: 0
                        drag.target: pill
                        hoverEnabled: true
                        preventStealing: true

                        onPressed: {
                            origin = pill.x;
                            eyes.dragIndex = index;
                            pill.forceActiveFocus();
                        }
                        onReleased: {
                            const from = index;
                            const delta = Math.abs(pill.x - origin);
                            const to = Math.max(0, Math.min(eyes.steps.length - 1, Math.round(pill.x / eyes.stride)));
                            eyes.dragIndex = -1;
                            if (delta < 10)
                                eyes.cycle(from);
                            else if (to !== from)
                                eyes.move(from, to);
                            else
                                pill.x = Qt.binding(() => index * eyes.stride);
                        }
                    }

                    MouseArea {
                        id: removeMouse

                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        cursorShape: Qt.PointingHandCursor
                        height: 22
                        hoverEnabled: true
                        preventStealing: true
                        width: 16

                        onClicked: eyes.removeAt(index)

                        Icon {
                            anchors.centerIn: parent
                            color: removeMouse.containsMouse ? Theme.glassText : Theme.glassFaint
                            name: "x"
                            size: 12
                            stroke: 2.2
                        }
                    }
                }
            }
        }
    }

    VText {
        color: Theme.glassFaint
        font.pixelSize: 12
        text: "Drag to reorder. Click a draw to switch left, right, and skip."
        visible: eyes.custom && eyes.steps.length > 0
        width: parent.width
        wrapMode: Text.Wrap
    }
}
