#pragma once

#include <QStringList>

class Game;

// The VR plugins Unreal Engine games carry in Engine/Binaries/ThirdParty, whether or not the game ever uses them. UEVR's
// injector stops to warn that they cause issues with the mod and suggests deleting or renaming them. Renaming a plugin's
// folder hides it from the game and the injector, and is undone by renaming it back.
namespace UnrealVrPlugins
{
    // Plugins still in place, by folder name: "OpenVR", "OpenXR" or "Oculus"
    QStringList present(const Game *game);
    // Plugins whose folder has been renamed out of the way
    QStringList disabled(const Game *game);

    // Both return false if a folder could not be renamed
    bool disable(const Game *game);
    bool restore(const Game *game);
} // namespace UnrealVrPlugins
