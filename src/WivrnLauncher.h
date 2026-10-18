#pragma once

// WiVRn's in-headset launcher lists .desktop files whose Categories contain
// X-WiVRn-VR. A system install already ships that file; a build-tree or
// AppImage run does not, so make sure one exists under the user's
// applications directory when nothing else advertises Kaon.
#if defined(KAON_OPENXR)
void ensureWivrnLauncherEntry();
#else
inline void ensureWivrnLauncherEntry() {}
#endif
