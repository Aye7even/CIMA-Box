// Push-to-talk key: global on Windows, window-focused elsewhere.
#pragma once
#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Works while a game or Discord has focus.
inline bool keyDown(int vk) { return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0; }
inline int scanKey() {
    for (int vk = 4; vk < 255; ++vk)  // skip left/right mouse buttons
        if (GetAsyncKeyState(vk) & 0x8000) return vk;
    return 0;
}
inline std::string keyName(int vk) {
    if (vk <= 0) return "not set";
    if (vk == VK_MBUTTON) return "Mouse 3";
    if (vk == VK_XBUTTON1) return "Mouse 4";
    if (vk == VK_XBUTTON2) return "Mouse 5";
    char buf[64] = {};
    const UINT sc = MapVirtualKeyA(UINT(vk), MAPVK_VK_TO_VSC);
    if (sc && GetKeyNameTextA(LONG(sc << 16), buf, sizeof(buf)) > 0) return buf;
    std::snprintf(buf, sizeof(buf), "Key 0x%02X", vk);
    return buf;
}
inline const bool kGlobalHotkey = true;
#else
#include <imgui.h>

inline bool isMouseKey(int k) { return k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY; }
inline bool keyDown(int k) {
    return k >= ImGuiKey_NamedKey_BEGIN && k < ImGuiKey_NamedKey_END && ImGui::IsKeyDown(ImGuiKey(k));
}
inline int scanKey() {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
        if (!isMouseKey(k) && ImGui::IsKeyDown(ImGuiKey(k))) return k;
    return 0;
}
inline std::string keyName(int k) {
    if (k < ImGuiKey_NamedKey_BEGIN || k >= ImGuiKey_NamedKey_END) return "not set";
    return ImGui::GetKeyName(ImGuiKey(k));
}
inline const bool kGlobalHotkey = false;
#endif
