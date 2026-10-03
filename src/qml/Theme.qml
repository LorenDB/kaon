pragma Singleton

import QtCore
import QtQuick

// Kaon is drawn as the headset: a plastic shell around a black glass visor, cut with the nose notch from the app icon.
// The shell follows the desktop's colour scheme unless Settings overrides it. The visor is black either way, because
// game art reads best on it. Colour is reserved for status lights, and a switch that's on uses the ready light.
QtObject {
    id: theme

    // "system", "light" or "dark"
    property alias appearance: appearanceSettings.shell
    readonly property FontLoader bold: FontLoader {
        source: "fonts/MPLUSRounded1c-Bold.ttf"
    }
    readonly property int bottomStrap: 60
    readonly property int button: 58
    readonly property bool dark: appearance === "dark" || (appearance === "system" && systemDark)
    readonly property FontLoader extraBold: FontLoader {
        source: "fonts/MPLUSRounded1c-ExtraBold.ttf"
    }
    readonly property string font: regular.name
    readonly property color glass: "#050608"
    readonly property color glassFaint: "#666d7a"
    readonly property color glassHover: "#16191f"
    readonly property color glassLine: "#2c3039"
    readonly property color glassMuted: "#9aa1ae"
    readonly property color glassPanel: "#121419"
    readonly property color glassRaised: "#1d2027"
    readonly property color glassText: "#f3f5f8"
    readonly property color ink: dark ? "#e8eaee" : "#15171b"
    readonly property color inkMuted: dark ? "#9aa1ac" : "#555c67"
    readonly property color ledAmber: "#ffb224"
    readonly property color ledBlue: "#5aa9ff"
    readonly property color ledGreen: "#32d77f"
    readonly property color ledOff: "#525866"
    readonly property color ledRed: "#ff5a52"
    readonly property FontLoader medium: FontLoader {
        source: "fonts/MPLUSRounded1c-Medium.ttf"
    }
    readonly property int notchHalfWidth: 58
    readonly property int notchHeight: 36
    readonly property int pad: 20
    readonly property SystemPalette palette: SystemPalette {
    }
    readonly property int radius: 22
    readonly property FontLoader regular: FontLoader {
        source: "fonts/MPLUSRounded1c-Regular.ttf"
    }
    // Qt.ColorScheme: 0 unknown, 1 light, 2 dark. Fall back to the palette when the platform doesn't say.
    readonly property int scheme: Application.styleHints.colorScheme
    readonly property Settings settings: Settings {
        id: appearanceSettings

        property string shell: "system"

        category: "appearance"
    }
    readonly property color shell: dark ? "#25272c" : "#e1e4e8"
    readonly property color shellDeep: dark ? "#30333a" : "#d2d6dc"
    readonly property color shellLine: dark ? "#40444c" : "#bcc2cb"
    readonly property int side: 8
    readonly property bool systemDark: scheme === 2 || (scheme === 0 && palette.window.hslLightness < 0.5)
    readonly property int topStrap: 54

    function led(group) {
        return group === "ready" ? ledGreen : group === "setup" ? ledAmber : group === "native" ? ledBlue : ledOff;
    }

    // Mod info strings use Markdown links. Text only honours linkColor for StyledText, so convert them.
    function linkify(text) {
        return text.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2">$1</a>');
    }

    function stepLed(state) {
        return state === "ok" || state === "busy" ? ledGreen : state === "warn" || state === "todo" ? ledAmber : state
                                                                                                      === "block" ? ledRed :
                                                                                                                    ledOff;
    }
}
