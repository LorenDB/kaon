#include "ModConfig.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "Game.h"
#include "GitHubMod.h"

namespace
{
    ConfigText::Spec setting(QString section,
                             QString label,
                             QString detail,
                             QString kind,
                             QStringList aliases,
                             QString missing,
                             QStringList choiceIds = {},
                             QStringList choiceLabels = {},
                             bool onlyIfPresent = false)
    {
        ConfigText::Spec spec;
        spec.section = std::move(section);
        spec.label = std::move(label);
        spec.detail = std::move(detail);
        spec.kind = std::move(kind);
        spec.aliases = std::move(aliases);
        spec.missing = std::move(missing);
        spec.choiceIds = std::move(choiceIds);
        spec.choiceLabels = std::move(choiceLabels);
        spec.onlyIfPresent = onlyIfPresent;
        return spec;
    }

    ConfigText::Spec boolean(QString section, QString label, QString detail, QString alias, bool missing)
    {
        return setting(std::move(section),
                       std::move(label),
                       std::move(detail),
                       "bool"_L1,
                       {std::move(alias)},
                       missing ? "true"_L1 : "false"_L1);
    }

    ConfigText::Spec number(QString section, QString label, QString detail, QString alias, QString missing)
    {
        return setting(
            std::move(section), std::move(label), std::move(detail), "number"_L1, {std::move(alias)}, std::move(missing));
    }

    ConfigText::Spec choice(
        QString section, QString label, QString detail, QString alias, QString missing, QStringList ids, QStringList labels)
    {
        return setting(std::move(section),
                       std::move(label),
                       std::move(detail),
                       "choice"_L1,
                       {std::move(alias)},
                       std::move(missing),
                       std::move(ids),
                       std::move(labels));
    }

    // Names vrperfkit's hotkey parser accepts, besides a single letter or digit.
    void perfKitKeyNames(QStringList *ids, QStringList *labels)
    {
        const auto add = [ids, labels](const QString &id, const QString &label) {
            *ids << id;
            *labels << label;
        };
        add("ctrl"_L1, "Ctrl"_L1);
        add("shift"_L1, "Shift"_L1);
        add("alt"_L1, "Alt"_L1);
        add("lctrl"_L1, "Left Ctrl"_L1);
        add("rctrl"_L1, "Right Ctrl"_L1);
        add("lshift"_L1, "Left Shift"_L1);
        add("rshift"_L1, "Right Shift"_L1);
        add("lalt"_L1, "Left Alt"_L1);
        add("ralt"_L1, "Right Alt"_L1);
        for (int i = 1; i <= 12; ++i)
            add(u"f"_s + QString::number(i), u"F"_s + QString::number(i));
        add("esc"_L1, "Esc"_L1);
        add("tab"_L1, "Tab"_L1);
        add("space"_L1, "Space"_L1);
        add("enter"_L1, "Enter"_L1);
        add("return"_L1, "Return"_L1);
        add("backspace"_L1, "Backspace"_L1);
        add("pause"_L1, "Pause"_L1);
        add("pgup"_L1, "Page Up"_L1);
        add("pgdown"_L1, "Page Down"_L1);
        add("home"_L1, "Home"_L1);
        add("end"_L1, "End"_L1);
        add("left"_L1, "Left"_L1);
        add("up"_L1, "Up"_L1);
        add("right"_L1, "Right"_L1);
        add("down"_L1, "Down"_L1);
        add("insert"_L1, "Insert"_L1);
        add("delete"_L1, "Delete"_L1);
        add("print"_L1, "Print Screen"_L1);
        for (char c = 'a'; c <= 'z'; ++c)
        {
            const auto letter = QString{QChar::fromLatin1(c)};
            add(letter, letter.toUpper());
        }
        for (char c = '0'; c <= '9'; ++c)
        {
            const auto digit = QString{QChar::fromLatin1(c)};
            add(digit, digit);
        }
        for (int i = 0; i <= 9; ++i)
            add(u"num"_s + QString::number(i), u"Num "_s + QString::number(i));
    }

    ConfigText::Spec hotkey(QString label, QString detail, QString alias, QString missing)
    {
        QStringList ids;
        QStringList labels;
        perfKitKeyNames(&ids, &labels);
        return setting("Hotkeys"_L1,
                       std::move(label),
                       std::move(detail),
                       "keys"_L1,
                       {std::move(alias)},
                       std::move(missing),
                       std::move(ids),
                       std::move(labels));
    }

    QList<ConfigText::Spec> portalSpecs(bool portal1)
    {
        const auto aim = portal1 ?
                             choice("View"_L1,
                                    "Aiming"_L1,
                                    "What the portal gun points with."_L1,
                                    "AimMode"_L1,
                                    "2"_L1,
                                    {"0"_L1, "1"_L1, "2"_L1},
                                    {"None"_L1, "Crosshair"_L1, "Aim dot"_L1}) :
                             choice("View"_L1,
                                    "Aiming"_L1,
                                    "What the portal gun points with. The crosshair does not sit on the headset view."_L1,
                                    "AimMode"_L1,
                                    "2"_L1,
                                    {"0"_L1, "1"_L1, "2"_L1},
                                    {"None"_L1, "Crosshair"_L1, "Laser"_L1});

        const auto windowDetail = portal1 ?
                                      "Draws the game a third time for the window and the pause menu. Leave this on."_L1 :
                                      "Draws the game a third time so the window shows it too."_L1;
        QList<ConfigText::Spec> specs{
            number("Movement"_L1, "Turn speed"_L1, "How fast smooth turning is."_L1, "TurnSpeed"_L1, "0.15"_L1),
            boolean("Movement"_L1, "Snap turning"_L1, "Turn in steps instead of smoothly."_L1, "SnapTurning"_L1, false),
            number("Movement"_L1, "Snap angle"_L1, "Degrees per snap."_L1, "SnapTurnAngle"_L1, "45.0"_L1),
            boolean("Movement"_L1, "Left handed"_L1, "Puts the portal gun in the left hand."_L1, "LeftHanded"_L1, false),
            boolean("Movement"_L1, "6DoF"_L1, "Lean and crouch with the headset."_L1, "6DOF"_L1, true),
        };
        if (portal1)
        {
            specs << boolean("Movement"_L1,
                             "Physical crouch"_L1,
                             "Ducking the headset also ducks. Off leaves crouch on the button."_L1,
                             "PhysicalCrouch"_L1,
                             true);
            specs << number("Movement"_L1,
                            "Crouch distance"_L1,
                            "How far the headset drops, in meters, before a crouch starts."_L1,
                            "PhysicalCrouchDrop"_L1,
                            "0.25"_L1);
            specs << boolean("Movement"_L1,
                             "Roomscale"_L1,
                             "Walking in the room walks the player, so collision and portals follow."_L1,
                             "Roomscale"_L1,
                             true);
            specs << boolean("Movement"_L1,
                             "Auto calibration"_L1,
                             "Keeps slow tracking drift from sliding the play space."_L1,
                             "AutoCalibration"_L1,
                             true);
        }
        specs << number("View"_L1,
                        "World scale"_L1,
                        "Source units per real-world unit. 43.2 is the usual size."_L1,
                        "VRScale"_L1,
                        "43.2"_L1);
        specs << number("View"_L1, "IPD scale"_L1, "Scales the distance between the eyes."_L1, "IPDScale"_L1, "1.0"_L1);
        specs << aim;
        if (!portal1)
        {
            specs << number("HUD"_L1, "HUD distance"_L1, "How far away the HUD sits."_L1, "HudDistance"_L1, "1.3"_L1);
            specs << number("HUD"_L1, "HUD size"_L1, "How large the HUD is."_L1, "HudSize"_L1, "4.0"_L1);
            specs << boolean("HUD"_L1,
                             "HUD always visible"_L1,
                             "Shows the HUD without looking up or down."_L1,
                             "HudAlwaysVisible"_L1,
                             false);
        }
        if (portal1)
        {
            specs << boolean(
                "Body"_L1, "Show hands"_L1, "Draws the hands and arms. Off leaves the portal gun."_L1, "ShowHands"_L1, true);
            specs << boolean("Body"_L1,
                             "First-person body"_L1,
                             "Draws the player when you look down, in the headset and in the window."_L1,
                             "FirstPersonBody"_L1,
                             true);
            specs << number("Body"_L1,
                            "Body offset"_L1,
                            "Moves the torso backward, in Source units. Restart Portal after changing it."_L1,
                            "FirstPersonBodyBackOffset"_L1,
                            "8"_L1);
            specs << boolean("Body"_L1,
                             "Hide head and arms"_L1,
                             "Keeps the head and untracked arms out of the camera, and leaves the torso and legs."_L1,
                             "FirstPersonBodyHideUpper"_L1,
                             true);
        }
        specs << choice("Rendering"_L1,
                        "Anti-aliasing"_L1,
                        "Multisample anti-aliasing. Higher is smoother and costs more."_L1,
                        "AntiAliasing"_L1,
                        "0"_L1,
                        {"0"_L1, "2"_L1, "4"_L1, "8"_L1},
                        {"Off"_L1, "2x"_L1, "4x"_L1, "8x"_L1});
        // The file stores 1 and 0. Writing true/false would fail the mod's integer parse and fall back to the default.
        specs << choice("Rendering"_L1,
                        "Desktop window"_L1,
                        windowDetail,
                        "RenderWindow"_L1,
                        // The game uses 0 when the line is missing. Portal 1's shipped file turns the window on.
                        "0"_L1,
                        {"0"_L1, "1"_L1},
                        {"Off"_L1, "On"_L1});
        if (portal1)
        {
            specs << boolean("Portal gun"_L1,
                             "Support hand"_L1,
                             "The other hand can grip the gun. It stays tracked on its own until it does."_L1,
                             "LeftHandGunGrip"_L1,
                             true);
            specs << number("Portal gun"_L1,
                            "Grip radius"_L1,
                            "How close the other hand has to be, in Source units."_L1,
                            "LeftHandGunGripRadius"_L1,
                            "6"_L1);
        }
        specs << number("Portal gun"_L1,
                        "Position X"_L1,
                        "Moves the gun forward, in Source units."_L1,
                        "ViewmodelPosCustomOffsetX"_L1,
                        "0.0"_L1);
        specs << number(
            "Portal gun"_L1, "Position Y"_L1, "Moves the gun to the right."_L1, "ViewmodelPosCustomOffsetY"_L1, "0.0"_L1);
        specs << number("Portal gun"_L1, "Position Z"_L1, "Moves the gun up."_L1, "ViewmodelPosCustomOffsetZ"_L1, "0.0"_L1);
        specs << number("Portal gun"_L1, "Angle X"_L1, "Tilts the gun."_L1, "ViewmodelAngCustomOffsetX"_L1, "0.0"_L1);
        specs << number("Portal gun"_L1, "Angle Y"_L1, "Turns the gun."_L1, "ViewmodelAngCustomOffsetY"_L1, "0.0"_L1);
        specs << number("Portal gun"_L1, "Angle Z"_L1, "Rolls the gun."_L1, "ViewmodelAngCustomOffsetZ"_L1, "0.0"_L1);
        if (portal1)
        {
            specs << number("Menu"_L1,
                            "Menu fallback X"_L1,
                            "Used only when the menu camera has no floor under it. 0, 0, 0 turns the fallback off."_L1,
                            "MenuSpawnX"_L1,
                            "0.0"_L1);
            specs << number(
                "Menu"_L1, "Menu fallback Y"_L1, "Same fallback, on the other axis."_L1, "MenuSpawnY"_L1, "0.0"_L1);
            specs << number(
                "Menu"_L1, "Menu fallback Z"_L1, "Same fallback, on the vertical axis."_L1, "MenuSpawnZ"_L1, "0.0"_L1);
            specs << number("Menu"_L1,
                            "Menu distance"_L1,
                            "How far in front of the head the menu sits, in meters."_L1,
                            "MenuPanelDistance"_L1,
                            "0.7"_L1);
            specs << number(
                "Menu"_L1, "Menu width"_L1, "How wide the menu is, in meters."_L1, "MenuPanelWidth"_L1, "1.0"_L1);
        }
        return specs;
    }

    QList<ConfigText::Spec> perfKitSpecs()
    {
        const auto keys = "Click the box and press the keys together. The game forgets hotkey changes when it closes."_L1;
        return {
            boolean("Upscaling"_L1,
                    "Upscaling"_L1,
                    "Renders lower, then scales the image back up."_L1,
                    "upscaling.enabled"_L1,
                    true),
            choice("Upscaling"_L1,
                   "Method"_L1,
                   "FSR, NIS and CAS all run on any GPU. Sharpness feels different for each."_L1,
                   "upscaling.method"_L1,
                   "cas"_L1,
                   {"fsr"_L1, "nis"_L1, "cas"_L1},
                   {"FSR"_L1, "NIS"_L1, "CAS"_L1}),
            number("Upscaling"_L1,
                   "Render scale"_L1,
                   "Applied to both width and height. 0.9 is a small cut. This is not SteamVR's render scale."_L1,
                   "upscaling.renderScale"_L1,
                   "0.9"_L1),
            number("Upscaling"_L1,
                   "Sharpness"_L1,
                   "How hard the upscaler sharpens. Tune it again after changing the method."_L1,
                   "upscaling.sharpness"_L1,
                   "0.7"_L1),
            number("Upscaling"_L1,
                   "Radius"_L1,
                   "The costly upscaler runs inside this circle. A large value, such as 100, runs it on the whole image."_L1,
                   "upscaling.radius"_L1,
                   "0.6"_L1),
            boolean("Upscaling"_L1,
                    "MIP bias"_L1,
                    "Samples textures as if the game rendered at full size. Turn it off if a game draws garbage."_L1,
                    "upscaling.applyMipBias"_L1,
                    true),
            boolean("Fixed foveated rendering"_L1,
                    "Fixed foveated"_L1,
                    "Drops resolution toward the edges. Needs an NVIDIA RTX or GTX 16-series GPU."_L1,
                    "fixedFoveated.enabled"_L1,
                    true),
            number("Fixed foveated rendering"_L1,
                   "Inner radius"_L1,
                   "The center, out to this radius, stays at full resolution."_L1,
                   "fixedFoveated.innerRadius"_L1,
                   "0.6"_L1),
            number("Fixed foveated rendering"_L1,
                   "Middle radius"_L1,
                   "From the inner radius to here, the image is at half resolution."_L1,
                   "fixedFoveated.midRadius"_L1,
                   "0.8"_L1),
            number("Fixed foveated rendering"_L1,
                   "Outer radius"_L1,
                   "From the middle radius to here, the image is at quarter resolution. Past it, at a sixteenth."_L1,
                   "fixedFoveated.outerRadius"_L1,
                   "1.0"_L1),
            boolean("Fixed foveated rendering"_L1,
                    "Favor horizontal"_L1,
                    "Keeps horizontal resolution when it drops, instead of vertical."_L1,
                    "fixedFoveated.favorHorizontal"_L1,
                    true),
            setting("Fixed foveated rendering"_L1,
                    "Eye order"_L1,
                    "Automatic guesses left for the first half of the draws, then right. Otherwise each draw is left, "
                    "right, or skip, in the order the game renders them. The toolkit ignores a list of the wrong length."_L1,
                    "text"_L1,
                    {"fixedFoveated.overrideSingleEyeOrder"_L1},
                    {},
                    {"L"_L1, "R"_L1, "S"_L1},
                    {"Left"_L1, "Right"_L1, "Skip"_L1}),
            boolean("Debug"_L1,
                    "Debug"_L1,
                    "Draws the upscaling circle and logs how long the post-process takes."_L1,
                    "debugMode"_L1,
                    false),
            boolean("Hotkeys"_L1,
                    "Hotkeys"_L1,
                    "Turn these off if they collide with keys the game uses."_L1,
                    "hotkeys.enabled"_L1,
                    true),
            hotkey("Toggle debug"_L1, keys, "hotkeys.toggleDebugMode"_L1, "ctrl, f1"_L1),
            hotkey("Cycle method"_L1, keys, "hotkeys.cycleUpscalingMethod"_L1, "ctrl, f2"_L1),
            hotkey("Increase radius"_L1, keys, "hotkeys.increaseUpscalingRadius"_L1, "ctrl, f3"_L1),
            hotkey("Decrease radius"_L1, keys, "hotkeys.decreaseUpscalingRadius"_L1, "ctrl, f4"_L1),
            hotkey("Increase sharpness"_L1, keys, "hotkeys.increaseUpscalingSharpness"_L1, "ctrl, f5"_L1),
            hotkey("Decrease sharpness"_L1, keys, "hotkeys.decreaseUpscalingSharpness"_L1, "ctrl, f6"_L1),
            hotkey("Toggle MIP bias"_L1, keys, "hotkeys.toggleUpscalingApplyMipBias"_L1, "ctrl, f7"_L1),
            hotkey("Capture frame"_L1, "Saves a DDS image next to the DLL."_L1, "hotkeys.captureOutput"_L1, "ctrl, f8"_L1),
            hotkey("Toggle foveation"_L1, keys, "hotkeys.toggleFixedFoveated"_L1, "alt, f1"_L1),
            hotkey("Toggle favor horizontal"_L1, keys, "hotkeys.toggleFFRFavorHorizontal"_L1, "alt, f2"_L1),
        };
    }

    ConfigText::Spec doorstopBool(
        QString label, QString detail, QString camel, QString snake, bool missing, bool onlyIfPresent)
    {
        return setting("Loader"_L1,
                       std::move(label),
                       std::move(detail),
                       "bool"_L1,
                       {std::move(camel), std::move(snake)},
                       missing ? "true"_L1 : "false"_L1,
                       {},
                       {},
                       onlyIfPresent);
    }

    QList<ConfigText::Spec> doorstopSpecs()
    {
        return {
            doorstopBool("Enabled"_L1,
                         "Doorstop starts BepInEx. Off, and the game loads with none of its plugins."_L1,
                         "enabled"_L1,
                         "enabled"_L1,
                         true,
                         false),
            doorstopBool("Redirect log"_L1,
                         "Writes Unity's log to output_log.txt next to the game."_L1,
                         "redirectOutputLog"_L1,
                         "redirect_output_log"_L1,
                         false,
                         false),
            doorstopBool("Ignore disable switch"_L1,
                         "Ignores DOORSTOP_DISABLE. Leave this off unless something told you to turn it on."_L1,
                         "ignoreDisableSwitch"_L1,
                         "ignore_disable_switch"_L1,
                         false,
                         true),
            setting("Loader"_L1,
                    "Target assembly"_L1,
                    "The DLL Doorstop runs. Leave this on the BepInEx preloader."_L1,
                    "text"_L1,
                    {"targetAssembly"_L1, "target_assembly"_L1},
                    {},
                    {},
                    {},
                    true),
            setting("Loader"_L1,
                    "DLL search path"_L1,
                    "Kaon fills this in for games that ship their own MonoMod. A value already here was set on purpose."_L1,
                    "text"_L1,
                    {"dllSearchPathOverride"_L1, "dll_search_path_override"_L1},
                    {},
                    {},
                    {},
                    true),
            setting("Loader"_L1,
                    "Boot config override"_L1,
                    "A different boot.config. Empty uses the game's."_L1,
                    "text"_L1,
                    {"boot_config_override"_L1},
                    {},
                    {},
                    {},
                    true),
            setting("Debugging"_L1,
                    "Mono debugger"_L1,
                    "Starts the Mono debugger server. For attaching a debugger, not for playing."_L1,
                    "bool"_L1,
                    {"debug_enabled"_L1},
                    "false"_L1,
                    {},
                    {},
                    true),
            setting("Debugging"_L1,
                    "Start debugger server"_L1,
                    "Doorstop opens the debugger port. If a debug build of the game crashes here, turn this off."_L1,
                    "bool"_L1,
                    {"debug_start_server"_L1},
                    "true"_L1,
                    {},
                    {},
                    true),
            setting("Debugging"_L1,
                    "Debugger address"_L1,
                    "Host and port the debugger listens on."_L1,
                    "text"_L1,
                    {"debug_address"_L1},
                    "127.0.0.1:10000"_L1,
                    {},
                    {},
                    true),
        };
    }


    // OptiScaler defaults many keys to "auto". Bool would drop those lines, so use choice with Auto.
    ConfigText::Spec autoBool(QString section, QString label, QString detail, QString alias)
    {
        return choice(std::move(section),
                      std::move(label),
                      std::move(detail),
                      std::move(alias),
                      "auto"_L1,
                      {"auto"_L1, "true"_L1, "false"_L1},
                      {"Auto"_L1, "On"_L1, "Off"_L1});
    }

    QList<ConfigText::Spec> optiScalerSpecs()
    {
        const auto dx11 = choice(
            "Upscalers"_L1,
            "DX11 upscaler"_L1,
            "Native and Dx11-on-12 options. Auto picks FSR 2.2."_L1,
            "Dx11Upscaler"_L1,
            "auto"_L1,
            {"auto"_L1,
             "fsr22"_L1,
             "fsr31"_L1,
             "xess"_L1,
             "xess_12"_L1,
             "fsr21_12"_L1,
             "fsr22_12"_L1,
             "ffx_12"_L1,
             "dlss"_L1},
            {"Auto"_L1,
             "FSR 2.2"_L1,
             "FSR 3.1"_L1,
             "XeSS"_L1,
             "XeSS (Dx12)"_L1,
             "FSR 2.1 (Dx12)"_L1,
             "FSR 2.2 (Dx12)"_L1,
             "FidelityFX (Dx12)"_L1,
             "DLSS"_L1});
        const auto dx12 = choice("Upscalers"_L1,
                                 "DX12 upscaler"_L1,
                                 "Auto picks DLSS, FSR 4, or XeSS from the GPU. AFW games need a DLSS path."_L1,
                                 "Dx12Upscaler"_L1,
                                 "auto"_L1,
                                 {"auto"_L1, "xess"_L1, "fsr21"_L1, "fsr22"_L1, "ffx"_L1, "dlss"_L1},
                                 {"Auto"_L1, "XeSS"_L1, "FSR 2.1"_L1, "FSR 2.2"_L1, "FidelityFX"_L1, "DLSS"_L1});
        const auto vulkan = choice(
            "Upscalers"_L1,
            "Vulkan upscaler"_L1,
            "Native and Vulkan-on-12 options. Auto picks FSR 2.2."_L1,
            "VulkanUpscaler"_L1,
            "auto"_L1,
            {"auto"_L1, "fsr21"_L1, "fsr22"_L1, "ffx"_L1, "xess"_L1, "fsr21_12"_L1, "ffx_12"_L1, "dlss"_L1},
            {"Auto"_L1,
             "FSR 2.1"_L1,
             "FSR 2.2"_L1,
             "FidelityFX"_L1,
             "XeSS"_L1,
             "FSR 2.1 (Dx12)"_L1,
             "FidelityFX (Dx12)"_L1,
             "DLSS"_L1});

        return {
            dx11,
            dx12,
            vulkan,
            autoBool("Frame generation"_L1,
                     "Frame generation"_L1,
                     "Master switch. Off leaves FGInput/FGOutput alone for fine control."_L1,
                     "FrameGen.Enabled"_L1),
            choice("Frame generation"_L1,
                   "FG input"_L1,
                   "Where motion vectors and depth come from. nofg turns generation off."_L1,
                   "FGInput"_L1,
                   "auto"_L1,
                   {"auto"_L1, "nofg"_L1, "dlssg"_L1, "nvngxfg"_L1, "fsrfg"_L1, "upscaler"_L1, "fsrfg30"_L1},
                   {"Auto"_L1, "Off"_L1, "DLSSG"_L1, "Nvngx FG"_L1, "FSR FG"_L1, "Upscaler"_L1, "FSR FG 3.0"_L1}),
            choice("Frame generation"_L1,
                   "FG output"_L1,
                   "Which frame generator draws the interpolated frames."_L1,
                   "FGOutput"_L1,
                   "auto"_L1,
                   {"auto"_L1, "nofg"_L1, "fsrfg"_L1, "xefg"_L1, "dlssg"_L1},
                   {"Auto"_L1, "Off"_L1, "FSR FG"_L1, "XeFG"_L1, "DLSSG"_L1}),
            choice("Frame generation"_L1,
                   "Nvngx FG replacement"_L1,
                   "Used when FG input or output needs an Nvngx DLSSG provider."_L1,
                   "FGNvngxReplacement"_L1,
                   "auto"_L1,
                   {"auto"_L1, "None"_L1, "Nukems"_L1, "Arturs"_L1, "FFX"_L1, "Combo"_L1},
                   {"Auto"_L1, "None"_L1, "Nukems"_L1, "Arturs"_L1, "FFX"_L1, "Combo"_L1}),
            setting("Framerate"_L1,
                    "Framerate limit"_L1,
                    "Caps FPS through Reflex when available. Auto or 0 leaves it uncapped."_L1,
                    "text"_L1,
                    {"FramerateLimit"_L1},
                    "auto"_L1),
            autoBool("Menu"_L1,
                     "Overlay menu"_L1,
                     "In-game ImGui menu. Insert opens it when the shortcut is left on Auto."_L1,
                     "OverlayMenu"_L1),
            setting("Menu"_L1,
                    "Menu shortcut"_L1,
                    "Win32 virtual-key code. Auto is Insert (0x2D). -1 disables the shortcut."_L1,
                    "text"_L1,
                    {"ShortcutKey"_L1},
                    "auto"_L1),
            autoBool("Menu"_L1, "Show FPS"_L1, "Draws the FPS overlay."_L1, "ShowFps"_L1),
            choice("Menu"_L1,
                   "FPS overlay"_L1,
                   "How much the FPS overlay shows."_L1,
                   "FpsOverlayType"_L1,
                   "auto"_L1,
                   {"auto"_L1, "0"_L1, "1"_L1, "2"_L1, "3"_L1, "4"_L1, "5"_L1, "6"_L1},
                   {"Auto"_L1,
                    "FPS only"_L1,
                    "Simple"_L1,
                    "Detailed"_L1,
                    "Detailed + graph"_L1,
                    "Full"_L1,
                    "Full + graph"_L1,
                    "Reflex timings"_L1}),
            choice("Sharpness"_L1,
                   "Sharpener"_L1,
                   "RCAS is the usual pick. DA and LCDA soften edges across depth breaks."_L1,
                   "Shader"_L1,
                   "auto"_L1,
                   {"auto"_L1, "rcas"_L1, "da"_L1, "lcda"_L1},
                   {"Auto"_L1, "RCAS"_L1, "DA"_L1, "LCDA"_L1}),
            autoBool("Sharpness"_L1,
                     "Override sharpness"_L1,
                     "Ignores the game's DLSS sharpness and uses the value below."_L1,
                     "OverrideSharpness"_L1),
            setting("Sharpness"_L1,
                    "Sharpness"_L1,
                    "0.0 to 1.3. Auto leaves OptiScaler's default (0.3)."_L1,
                    "text"_L1,
                    {"Sharpness"_L1},
                    "auto"_L1),
            autoBool("Output scaling"_L1,
                     "Output scaling"_L1,
                     "Renders below the display size, then scales up. DX12 and DX11-on-12 only."_L1,
                     "OutputScaling.Enabled"_L1),
            setting("Output scaling"_L1,
                    "Scale multiplier"_L1,
                    "0.5 to 3.0. Auto is 1.5."_L1,
                    "text"_L1,
                    {"Multiplier"_L1},
                    "auto"_L1),
            choice("Output scaling"_L1,
                   "Downscaler"_L1,
                   "Filter used when scaling the image back up."_L1,
                   "Downscaler"_L1,
                   "auto"_L1,
                   {"auto"_L1, "0"_L1, "1"_L1, "2"_L1, "3"_L1, "4"_L1, "5"_L1, "6"_L1, "7"_L1},
                   {"Auto"_L1,
                    "FSR 1"_L1,
                    "Bicubic"_L1,
                    "Catmull-Rom"_L1,
                    "Lanczos 2"_L1,
                    "Lanczos 3"_L1,
                    "Kaiser 2"_L1,
                    "Kaiser 3"_L1,
                    "MAGIC"_L1}),
            autoBool("Upscale ratio"_L1,
                     "Force upscale ratio"_L1,
                     "Overrides the internal resolution scale the upscaler asked for."_L1,
                     "UpscaleRatioOverrideEnabled"_L1),
            setting("Upscale ratio"_L1,
                    "Upscale ratio"_L1,
                    "Applied when force upscale ratio is on. Auto is 1.3."_L1,
                    "text"_L1,
                    {"UpscaleRatioOverrideValue"_L1},
                    "auto"_L1),
            autoBool("Quality overrides"_L1,
                     "Custom quality ratios"_L1,
                     "Replaces DLSS/FSR quality-mode scales with the values below."_L1,
                     "QualityRatioOverrideEnabled"_L1),
            setting("Quality overrides"_L1,
                    "DLAA ratio"_L1,
                    "Auto is 1.0."_L1,
                    "text"_L1,
                    {"QualityRatioDLAA"_L1},
                    "auto"_L1),
            setting("Quality overrides"_L1,
                    "Ultra Quality ratio"_L1,
                    "Auto is 1.3."_L1,
                    "text"_L1,
                    {"QualityRatioUltraQuality"_L1},
                    "auto"_L1),
            setting("Quality overrides"_L1,
                    "Quality ratio"_L1,
                    "Auto is 1.5."_L1,
                    "text"_L1,
                    {"QualityRatioQuality"_L1},
                    "auto"_L1),
            setting("Quality overrides"_L1,
                    "Balanced ratio"_L1,
                    "Auto is 1.7."_L1,
                    "text"_L1,
                    {"QualityRatioBalanced"_L1},
                    "auto"_L1),
            setting("Quality overrides"_L1,
                    "Performance ratio"_L1,
                    "Auto is 2.0."_L1,
                    "text"_L1,
                    {"QualityRatioPerformance"_L1},
                    "auto"_L1),
            setting("Quality overrides"_L1,
                    "Ultra Performance ratio"_L1,
                    "Auto is 3.0."_L1,
                    "text"_L1,
                    {"QualityRatioUltraPerformance"_L1},
                    "auto"_L1),
            autoBool("HDR"_L1, "Force HDR"_L1, "Forces an HDR color space."_L1, "ForceHDR"_L1),
            autoBool("HDR"_L1, "Use HDR10"_L1, "Uses R10G10B10A2 instead of float16."_L1, "UseHDR10"_L1),
            choice("Log"_L1,
                   "Log level"_L1,
                   "How much OptiScaler writes when logging is on."_L1,
                   "LogLevel"_L1,
                   "auto"_L1,
                   {"auto"_L1, "0"_L1, "1"_L1, "2"_L1, "3"_L1, "4"_L1},
                   {"Auto"_L1, "Trace"_L1, "Debug"_L1, "Info"_L1, "Warning"_L1, "Error"_L1}),
            autoBool("Log"_L1, "Log to file"_L1, "Writes OptiScaler.log next to the DLL."_L1, "LogToFile"_L1),
            autoBool("Log"_L1, "Log to console"_L1, "Prints to an attached console."_L1, "LogToConsole"_L1),
        };
    }

    struct Schema
    {
        ConfigText::Syntax syntax;
        QString note;
        QList<ConfigText::Spec> specs;
    };

    Schema schemaFor(const Mod *mod)
    {
        const auto group = mod->settingsGroup();
        if (group == "portal1vr"_L1)
            return {ConfigText::Syntax::Equals, "Portal reads this file when it starts."_L1, portalSpecs(true)};
        if (group == "portal2vr"_L1)
            return {ConfigText::Syntax::Equals,
                    "Portal reads this file when it starts. Portal Stories: Mel keeps the gun offsets Kaon set for it."_L1,
                    portalSpecs(false)};
        if (group == "vrperfkit"_L1)
            return {ConfigText::Syntax::Yaml,
                    "The game reads this file when it starts. Hotkey changes inside the headset are not written here."_L1,
                    perfKitSpecs()};
        if (group == "bepinex"_L1)
            return {ConfigText::Syntax::Ini, "Doorstop reads this file when the game starts."_L1, doorstopSpecs()};
        if (group == "optiscaler"_L1)
            return {ConfigText::Syntax::Ini,
                    "OptiScaler reads OptiScaler.ini next to the game when it starts. Auto leaves each setting on "
                    "OptiScaler's own default. The in-game menu (Insert) can change more than Kaon shows here."_L1,
                    optiScalerSpecs()};
        return {};
    }
} // namespace

ModConfigDocument::ModConfigDocument(QString title,
                                     QString path,
                                     QString note,
                                     ConfigText::Syntax syntax,
                                     QList<ConfigText::Spec> specs,
                                     ConfigText::Document document,
                                     QByteArray shippedBytes,
                                     QObject *parent)
    : QAbstractListModel{parent},
      m_title{std::move(title)},
      m_path{std::move(path)},
      m_note{std::move(note)},
      m_syntax{syntax},
      m_specs{std::move(specs)},
      m_document{std::move(document)},
      m_shippedBytes{std::move(shippedBytes)}
{
    applyShippedValues();
}

void ModConfigDocument::applyShippedValues()
{
    m_shippedValues.clear();
    if (m_shippedBytes.isEmpty())
        return;
    const auto shipped = ConfigText::loadText(m_syntax, QString::fromUtf8(m_shippedBytes), m_specs);
    QHash<QString, QString> byAlias;
    for (const auto &field : shipped.fields)
    {
        if (!field.spec.aliases.isEmpty())
            byAlias.insert(field.spec.aliases.at(0), field.value);
    }
    for (const auto &field : m_document.fields)
    {
        const auto alias = field.spec.aliases.value(0);
        m_shippedValues << (byAlias.contains(alias) ? byAlias.value(alias) : field.spec.missing);
    }
}

bool ModConfigDocument::dirty() const
{
    return std::any_of(m_document.fields.cbegin(), m_document.fields.cend(), [](const ConfigText::Field &field) {
        return field.value != field.original;
    });
}

int ModConfigDocument::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_document.fields.size();
}

QVariant ModConfigDocument::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_document.fields.size())
        return {};
    const auto &field = m_document.fields.at(index.row());
    switch (role)
    {
    case Heading:
        if (index.row() == 0 || field.spec.section != m_document.fields.at(index.row() - 1).spec.section)
            return field.spec.section;
        return QString{};
    case Label:
        return field.spec.label;
    case Detail:
        return field.spec.detail;
    case Kind:
        return field.spec.kind;
    case Value:
        return field.value;
    case Choices:
    {
        QVariantList choices;
        for (int i = 0; i < field.spec.choiceIds.size(); ++i)
        {
            QVariantMap choice;
            choice.insert("id"_L1, field.spec.choiceIds.at(i));
            choice.insert("label"_L1,
                          i < field.spec.choiceLabels.size() ? field.spec.choiceLabels.at(i) : field.spec.choiceIds.at(i));
            choices << choice;
        }
        return choices;
    }
    case Shipped:
        return index.row() < m_shippedValues.size() ? QVariant{m_shippedValues.at(index.row())} : QVariant{QString{}};
    default:
        return {};
    }
}

QHash<int, QByteArray> ModConfigDocument::roleNames() const
{
    return {{Heading, "heading"_ba},
            {Label, "label"_ba},
            {Detail, "detail"_ba},
            {Kind, "kind"_ba},
            {Value, "value"_ba},
            {Choices, "choices"_ba},
            {Shipped, "shipped"_ba}};
}

void ModConfigDocument::setField(int row, const QString &value)
{
    if (row < 0 || row >= m_document.fields.size() || m_document.fields.at(row).value == value)
        return;
    m_document.fields[row].value = value;
    if (!m_error.isEmpty())
    {
        m_error.clear();
        emit errorChanged();
    }
    emit dataChanged(index(row), index(row), {Value, Choices});
    emit dirtyChanged();
}

void ModConfigDocument::resetField(int row)
{
    if (!defaultsReady() || row < 0 || row >= m_shippedValues.size())
        return;
    setField(row, m_shippedValues.at(row));
}

void ModConfigDocument::discardChanges()
{
    auto changed = false;
    for (auto &field : m_document.fields)
    {
        if (field.value == field.original)
            continue;
        field.value = field.original;
        changed = true;
    }
    if (!changed)
        return;
    emit dataChanged(index(0), index(m_document.fields.size() - 1), {Value, Choices});
    emit dirtyChanged();
}

bool ModConfigDocument::resetToDownload()
{
    if (m_shippedBytes.isEmpty())
    {
        m_error = "Kaon doesn't have the downloaded copy of these settings."_L1;
        emit errorChanged();
        return false;
    }

    QSaveFile out{m_path};
    if (!out.open(QIODevice::WriteOnly) || out.write(m_shippedBytes) != m_shippedBytes.size() || !out.commit())
    {
        m_error = "Couldn't reset %1. Quit the game and try again."_L1.arg(QFileInfo{m_path}.fileName());
        emit errorChanged();
        return false;
    }

    beginResetModel();
    m_document = ConfigText::loadText(m_syntax, QString::fromUtf8(m_shippedBytes), m_specs);
    applyShippedValues();
    endResetModel();
    m_error.clear();
    emit errorChanged();
    emit dirtyChanged();
    return true;
}

bool ModConfigDocument::save()
{
    QString text;
    QString error;
    if (!ConfigText::saveText(m_document, &text, &error))
    {
        m_error = error;
        emit errorChanged();
        return false;
    }

    QSaveFile out{m_path};
    const auto bytes = text.toUtf8();
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit())
    {
        m_error = "Couldn't save %1. Quit the game and try again."_L1.arg(QFileInfo{m_path}.fileName());
        emit errorChanged();
        return false;
    }

    beginResetModel();
    m_document = ConfigText::loadText(m_syntax, text, m_specs);
    applyShippedValues();
    endResetModel();
    m_error.clear();
    emit errorChanged();
    emit dirtyChanged();
    return true;
}

ModConfigs::ModConfigs(QObject *parent)
    : QObject{parent}
{
    Q_ASSERT(ConfigText::selfCheck());
}

ModConfigs *ModConfigs::instance()
{
    static auto configs = new ModConfigs;
    return configs;
}

ModConfigs *ModConfigs::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

bool ModConfigs::available(Mod *mod, Game *game, int revision) const
{
    Q_UNUSED(revision)
    return mod && game && mod->isInstalledForGame(game) && !mod->configFileForGame(game).isEmpty();
}

bool ModConfigs::open(Mod *mod, Game *game)
{
    const auto fail = [this](const QString &message) {
        m_error = message;
        emit errorChanged();
        return false;
    };
    if (!mod || !game)
        return fail("Kaon couldn't open those settings."_L1);

    const auto path = mod->configFileForGame(game);
    if (path.isEmpty() || !mod->isInstalledForGame(game))
        return fail("%1 has no settings file in this game."_L1.arg(mod->displayName()));

    const auto schema = schemaFor(mod);
    if (schema.specs.isEmpty())
        return fail("Kaon can't edit %1's settings."_L1.arg(mod->displayName()));

    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
        return fail("Couldn't read %1."_L1.arg(QFileInfo{path}.fileName()));

    QByteArray shipped;
    if (const auto github = dynamic_cast<GitHubMod *>(mod))
    {
        QString shippedError;
        shipped = github->readShippedConfig(game, &shippedError);
        if (shipped.isEmpty())
            qWarning() << "No downloaded defaults for" << mod->displayName() << shippedError;
    }

    auto *next = new ModConfigDocument{mod->displayName(),
                                       path,
                                       schema.note,
                                       schema.syntax,
                                       schema.specs,
                                       ConfigText::loadText(schema.syntax, QString::fromUtf8(file.readAll()), schema.specs),
                                       std::move(shipped),
                                       this};
    auto *previous = m_document;
    m_document = next;
    m_error.clear();
    emit documentChanged();
    emit errorChanged();
    if (previous)
        previous->deleteLater();
    return true;
}
