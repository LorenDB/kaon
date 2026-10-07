import QtQuick

import dev.lorendb.kaon
import QtQuick.Shapes

// The plastic shell, drawn on top of everything with a visor-shaped hole in it. Whatever scrolls inside the visor
// is hidden behind the shell, so the rounded corners and the nose notch need no masking.
Shape {
    id: frame

    property color color: Theme.shell
    readonly property real cx: visorX + visorW / 2
    readonly property real nh: Theme.notchClearance
    readonly property real nw: nh > 0 ? Theme.notchHalfWidth : 0
    readonly property real r: Theme.radius
    property real visorH: height - Theme.topStrap - Theme.bottomStrap
    property real visorW: width - 2 * Theme.side
    property real visorX: Theme.side
    property real visorY: Theme.topStrap
    readonly property real x0: visorX
    readonly property real x1: visorX + visorW
    readonly property real y0: visorY
    readonly property real y1: visorY + visorH

    preferredRendererType: Shape.CurveRenderer

    Behavior on color {
        ColorAnimation {
            duration: Theme.durationMed
        }
    }

    ShapePath {
        fillColor: frame.color
        fillRule: ShapePath.OddEvenFill
        strokeColor: "transparent"

        PathSvg {
            path: {
                const f = frame;
                const outer = `M 0 0 H ${f.width} V ${f.height} H 0 Z`;
                // Flat bottom when the notch button is hidden (no SteamVR on library/settings/mods).
                // Otherwise the notch follows the curve of the nose cut-out in kaon.svg.
                const bottom = f.nh > 0
                    ? `H ${f.cx + f.nw} C ${f.cx + f.nw * 0.42} ${f.y1} ${f.cx + f.nw * 0.62} ${f.y1 - f.nh} ${f.cx} ${f.y1 - f.nh} `
                        + `C ${f.cx - f.nw * 0.62} ${f.y1 - f.nh} ${f.cx - f.nw * 0.42} ${f.y1} ${f.cx - f.nw} ${f.y1} H ${f.x0 + f.r}`
                    : `H ${f.x0 + f.r}`;
                const inner = `M ${f.x0 + f.r} ${f.y0} H ${f.x1 - f.r} A ${f.r} ${f.r} 0 0 1 ${f.x1} ${f.y0 + f.r} V ${f.y1 - f.r} `
                    + `A ${f.r} ${f.r} 0 0 1 ${f.x1 - f.r} ${f.y1} ${bottom} `
                    + `A ${f.r} ${f.r} 0 0 1 ${f.x0} ${f.y1 - f.r} V ${f.y0 + f.r} A ${f.r} ${f.r} 0 0 1 ${f.x0 + f.r} ${f.y0} Z`;
                return outer + " " + inner;
            }
        }
    }
}
