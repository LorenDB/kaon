#pragma once

#include <QString>
#include <QStringList>

// What VR runtime a Windows game started by Kaon could talk to, read from the
// host's OpenVR/OpenXR configuration rather than from inside any Wine prefix.
//
// OpenVR games (UEVR's OpenVR mode, UUVR, Portal VR) find their runtime through
// ~/.config/openvr/openvrpaths.vrpath, or through VR_OVERRIDE (xrizer,
// OpenComposite). OpenXR games (UEVR's OpenXR mode) use XR_RUNTIME_JSON when it
// is set, otherwise ~/.config/openxr/1/active_runtime.json, which SteamVR keeps
// pointed at its own vrclient when it is the active runtime (Monado, WiVRn and
// ALVR register there the same way).
//
// A hand-added game is started by Kaon with plain `wine exe`
// (CustomGames::launch via Wine::runInWine), never with Steam's `proton run`
// wrapper, so Proton's VR bridge (vrclient/wineopenxr plus the volatile
// HKCU\Software\Wine\VR key) is missing even when a runtime is installed.
// That is what GameStatus warns about; these helpers only answer the two
// questions the warning needs: is any runtime registered, and does this game
// launch the way that can reach one.
namespace VrEnvironment
{
    struct Status
    {
        bool hasOpenVr = false;
        bool hasOpenXr = false;
        // "SteamVR", "Monado", ... when the active OpenXR manifest names one
        QString activeRuntimeName;
        bool hasAny() const { return hasOpenVr || hasOpenXr; }
    };

    // Reads the host configuration. Cheap (two small JSON files) and cached for
    // a couple of seconds, since GameStatus asks once per game on every refresh.
    Status detect();

    // Proton keeps its Wine at <tool>/files/bin/wine, <tool>/files/bin-arm64/wine
    // or <tool>/dist/bin/wine (same spelling as GameStatus' compatToolName).
    // Anything else is system Wine or an unknown build.
    bool isProtonWineBinary(const QString &wineBinary);

    // True for a Proton compatdata prefix (<compatdata>/<id>/pfx with the
    // config_info/version siblings Proton maintains), false for a plain Wine
    // prefix such as ~/.wine.
    bool looksLikeProtonPrefix(const QString &prefix);

    // watchedFiles() lists what detect() reads (plus parent directories that may
    // not exist yet), so GameStatus can re-evaluate when a runtime is installed.
    QStringList watchedFiles();

    namespace Test
    {
        // The same checks against explicit contents/paths, for unit tests.
        bool hasOpenVrRuntime(const QString &vrpathJson, const QString &vrOverride);
        bool hasOpenXrRuntime(const QString &activeRuntimeJson, const QString &xrRuntimeJson);
        QString openXrRuntimeName(const QString &activeRuntimeJson);
    } // namespace Test
} // namespace VrEnvironment
