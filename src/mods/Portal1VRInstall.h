#pragma once

#include <QString>
#include <QStringList>

// Installs an extracted LorenDB/portal1vr release the way Install.ps1 and
// L4D2VR/copy-to-portal.ps1 do. Dropping the zip into the game folder leaves the
// runtime, materials, and bindings in the package layout, where Portal never loads them.
//
// portalDir is the folder that contains hl2.exe. packageRoot is the extracted zip.
// installedFiles lists every file and directory this run wrote. Directories end with
// '/'. An existing config.txt is updated in place and left off the list, so uninstall
// does not delete settings the player already had.
bool installPortal1VRPackage(const QString &portalDir,
                             const QString &packageRoot,
                             QStringList &installedFiles,
                             QString &error);
