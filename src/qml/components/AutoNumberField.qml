import QtQuick
import QtQuick.Controls.Basic

import dev.lorendb.kaon

// OptiScaler-style "auto or number": Auto stays on until the player picks a value. Ranged fields get a
// glass slider; unbounded ones get a plain number box.
Column {
    id: field

    property string value: "auto"
    property bool ranged: false
    property real minimum: 0
    property real maximum: 1
    property real step: 0.01
    property string manualDefault: "0"

    signal edited(string next)

    readonly property bool isAuto: value.trim().toLowerCase() === "auto"
    readonly property real numeric: {
        const n = Number(value);
        if (!isFinite(n))
            return Number(manualDefault) || minimum;
        if (ranged)
            return Math.min(maximum, Math.max(minimum, n));
        return n;
    }

    function formatNumber(n) {
        if (!isFinite(n))
            return manualDefault;
        // Keep short decimals so the ini stays readable (0.3 not 0.3000000004).
        const rounded = Math.round(n / Math.max(step, 0.0001)) * Math.max(step, 0.0001);
        let text = rounded.toFixed(4);
        text = text.replace(/\.?0+$/, "");
        return text === "-0" ? "0" : text;
    }

    function setAuto(on) {
        if (on)
            field.edited("auto");
        else
            field.edited(isAuto ? (manualDefault || formatNumber(minimum)) : formatNumber(numeric));
    }

    spacing: 8
    width: parent ? parent.width : 280

    Row {
        spacing: 10

        ToggleSwitch {
            id: autoSwitch

            anchors.verticalCenter: parent.verticalCenter
            checked: field.isAuto

            onToggled: field.setAuto(!field.isAuto)
        }

        VText {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.glassMuted
            font.pixelSize: 13
            font.weight: Font.Bold
            text: "Auto"
        }
    }

    Item {
        height: field.isAuto ? 0 : (field.ranged ? sliderRow.height : numberBox.height)
        opacity: field.isAuto ? 0 : 1
        visible: height > 0.5
        width: parent.width

        Behavior on height {
            NumberAnimation {
                duration: Theme.durationMed
                easing.type: Theme.easeOut
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.durationFast
            }
        }

        Row {
            id: sliderRow

            spacing: 12
            visible: field.ranged
            width: parent.width

            Slider {
                id: slider

                anchors.verticalCenter: parent.verticalCenter
                from: field.minimum
                stepSize: field.step
                to: field.maximum
                value: field.numeric
                width: parent.width - valueLabel.width - 12

                background: Rectangle {
                    color: Theme.glassLine
                    height: 6
                    radius: 3
                    width: slider.availableWidth
                    x: slider.leftPadding
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2

                    Rectangle {
                        color: Theme.ledGreen
                        height: parent.height
                        radius: parent.radius
                        width: slider.visualPosition * parent.width
                    }
                }
                handle: Rectangle {
                    color: slider.pressed ? Theme.glassText : Theme.glassMuted
                    height: 18
                    radius: 9
                    width: 18
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2

                    Behavior on color {
                        ColorAnimation {
                            duration: Theme.durationFast
                        }
                    }
                }

                onMoved: field.edited(field.formatNumber(value))
            }

            VText {
                id: valueLabel

                anchors.verticalCenter: parent.verticalCenter
                color: Theme.glassText
                font.pixelSize: 13
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignRight
                text: field.formatNumber(field.numeric)
                width: Math.max(44, implicitWidth)
            }
        }

        TextBox {
            id: numberBox

            visible: !field.ranged
            width: parent.width
            text: field.isAuto ? "" : field.value

            onTextChanged: {
                if (!visible || field.isAuto)
                    return;
                if (text !== field.value)
                    field.edited(text);
            }
        }
    }
}
