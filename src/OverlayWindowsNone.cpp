// The font editor links Overlay.cpp but has no game to build menus from.
#include "OverlayWindows.h"

void OverlayWindows::Init(void *) {}
void OverlayWindows::Tick() {}
void OverlayWindows::TogglePalette() {}
void OverlayWindows::SetPassThrough(bool) {}
void OverlayWindows::Notify(const char *) {}
bool OverlayWindows::HideTPTMenus() { return false; }
void OverlayWindows::ApplySimMasks(uint32_t *, int) {}
void OverlayWindows::ClearUnderWindows(uint32_t *, int, int, int, int) {}
void OverlayWindows::SetPlayArea(int, int, int, int) {}
bool OverlayWindows::AreaMouse(int, int, int) { return false; }
int OverlayWindows::Form(const String &, const String &, const std::vector<String> &, int &, String &, const std::vector<String> &,
                         std::function<bool(int, const String &)>, int) { return -1; }
bool OverlayWindows::ShowConsole() { return false; }
void OverlayWindows::HideConsole() {}
