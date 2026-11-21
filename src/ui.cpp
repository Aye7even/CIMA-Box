#include "ui.h"
#include "bidi.h"
#include "screen.h"

#ifndef CIMA_VERSION
#define CIMA_VERSION "2.6"  // normally set by CMakeLists.txt
#endif

#include <algorithm>
#include <map>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "emotion.h"
#include "hotkey.h"
#include "i18n.h"
#include "changelog.h"
#include "translate.h"

namespace {

// ---------------------------------------------------------------- palette

ImVec4 rgb(unsigned v, float a = 1.f) {
    return ImVec4(((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f, a);
}
// Kept calm on purpose: text that isn't pure white, panels told apart by their shade instead of
// outlines, and one accent colour used only for what is live or selected. These are the original
// look; Settings > Look can change them (applyLook), so they aren't constants.
ImVec4 kBg = rgb(0x14171E);          // window
ImVec4 kPanel = rgb(0x1C212B);       // the three stages and the status strip
ImVec4 kField = rgb(0x272D3A);       // inputs, buttons
ImVec4 kFieldHover = rgb(0x303747);
ImVec4 kFieldActive = rgb(0x394154);
ImVec4 kLine = rgb(0x2B3240);
ImVec4 kText = rgb(0xDDE2EB);
ImVec4 kMuted = rgb(0x8D98AD);
ImVec4 kLabel = kMuted;              // names of settings, above their control
ImVec4 kSignal = rgb(0xE8B05E);      // amber = live signal: meter, switches, the selected option
ImVec4 kSignalHover = rgb(0xF0C07A);
ImVec4 kSignalDown = rgb(0xCF9A48);
ImVec4 kOnSignal = rgb(0x1E1707);    // text on amber
ImVec4 kPopup = rgb(0x222834);
const ImVec4 kBusy = rgb(0x8FB2F2);
const ImVec4 kDanger = rgb(0xF08484);
ImVec4 kVoices[] = {kSignal, kBusy, rgb(0x7FD6A4), rgb(0xE59AD0)};  // speakers inside one caption
// The same person keeps the same colour across captions (Features > Same voice, same color).
const ImVec4 kPeople[] = {rgb(0x7FD6A4), rgb(0xE59AD0), rgb(0x8EC5FF), kBusy, rgb(0xB9A3FF), rgb(0xFF9E8A), rgb(0xD7E37A), rgb(0x6FD3D0)};
float gPanelAlpha = 1.f;             // below 1 when a background picture shows through the panels

ImFont* gSemibold = nullptr;

void guideLink(UiModel& m, int which);

ImU32 col(const ImVec4& c) { return ImGui::GetColorU32(c); }  // applies the disabled alpha
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
float em() { return ImGui::GetFontSize(); }

// ---------------------------------------------------------------- fonts

bool fileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return bool(f);
}

// (ascent - descent) / unitsPerEm from the font file's own tables, 0 if unreadable.
// Fonts disagree on this, so it is used to give merged fonts the same letter size as the main one.
float lineHeightEm(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    auto u16 = [&](std::streamoff off) -> int {
        unsigned char b[2] = {};
        f.seekg(off);
        f.read(reinterpret_cast<char*>(b), 2);
        return f ? (b[0] << 8) | b[1] : -1;
    };
    auto u32 = [&](std::streamoff off) -> std::streamoff {
        const int hi = u16(off), lo = u16(off + 2);
        return hi < 0 || lo < 0 ? 0 : (std::streamoff(hi) << 16) | lo;
    };
    auto tagAt = [&](std::streamoff off, const char* tag) {
        char b[4] = {};
        f.seekg(off);
        f.read(b, 4);
        return f && std::memcmp(b, tag, 4) == 0;
    };
    if (!f) return 0.f;
    const std::streamoff base = tagAt(0, "ttcf") ? u32(12) : 0;  // first font of a collection
    const int tables = u16(base + 4);
    std::streamoff head = 0, hhea = 0;
    for (int i = 0; i < tables && i < 200; ++i) {
        const std::streamoff rec = base + 12 + 16 * std::streamoff(i);
        if (tagAt(rec, "head")) head = u32(rec + 8);
        else if (tagAt(rec, "hhea")) hhea = u32(rec + 8);
    }
    if (!head || !hhea) return 0.f;
    const int upem = u16(head + 18), asc = u16(hhea + 4), desc = u16(hhea + 6);
    if (upem <= 0 || asc < 0 || desc < 0) return 0.f;
    return float(int16_t(asc) - int16_t(desc)) / float(upem);
}

const float kFontRefSize = 16.0f;

// Adds the first font of the list that exists. A merged font is scaled to match `baseLineEm`.
ImFont* addFirstFont(const std::vector<std::string>& candidates, bool merge, float* lineEm, float baseLineEm = 0.f) {
    for (const std::string& path : candidates) {
        if (!fileExists(path)) continue;
        const float mine = lineHeightEm(path);
        if (lineEm) *lineEm = mine;
        ImFontConfig cfg;
        cfg.MergeMode = merge;
        cfg.SizePixels = kFontRefSize;
        if (merge && mine > 0.f && baseLineEm > 0.f) cfg.SizePixels = kFontRefSize * mine / baseLineEm;
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), cfg.SizePixels, &cfg);
    }
    return nullptr;
}

// System UI font, with fonts for other writing systems merged in so translated text shows up.
// Glyphs are rasterised on demand, so the big CJK fonts cost nothing until they are used.
void loadFonts() {
    using List = std::vector<std::string>;
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::string d = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    const List regular = {d + "segoeui.ttf", d + "arial.ttf"};
    const List bold = {d + "seguisb.ttf", d + "segoeuib.ttf", d + "arialbd.ttf"};
    // Segoe UI already covers Latin, Greek, Cyrillic, Armenian, Georgian, Hebrew and Arabic letters.
    const std::vector<List> extra = {{d + "YuGothM.ttc", d + "meiryo.ttc", d + "msgothic.ttc"},
                                     {d + "malgun.ttf"},
                                     {d + "msyh.ttc", d + "simsun.ttc"},
                                     {d + "Nirmala.ttf"},    // Hindi, Bengali, Tamil, Telugu, Gujarati, ...
                                     {d + "LeelawUI.ttf"},   // Thai, Lao
                                     {d + "ebrima.ttf"},     // Amharic (Ethiopic)
                                     {d + "mmrtext.ttf"}};   // Burmese
#elif defined(__APPLE__)
    const std::string d = "/System/Library/Fonts/";
    const List regular = {d + "SFNS.ttf", d + "Helvetica.ttc", d + "Supplemental/Arial.ttf"};
    const List bold = {d + "Supplemental/Arial Bold.ttf"};
    const std::vector<List> extra = {{d + "Hiragino Sans GB.ttc"}, {d + "AppleSDGothicNeo.ttc"}, {d + "PingFang.ttc"}};
#else
    const List regular = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                          "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
    const List bold = {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                       "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"};
    const std::vector<List> extra = {{"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                                      "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                      "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc"}};
#endif
    float baseEm = 0.f;
    if (!addFirstFont(regular, false, &baseEm)) {
        ImGui::GetIO().Fonts->AddFontDefault();
        return;
    }
    for (const List& e : extra) addFirstFont(e, true, nullptr, baseEm);
    float boldEm = 0.f;
    gSemibold = addFirstFont(bold, false, &boldEm);
    if (gSemibold)  // headings in Japanese, Korean, Chinese, Hindi... need the same extra fonts
        for (const List& e : extra) addFirstFont(e, true, nullptr, boldEm > 0.f ? boldEm : baseEm);
}

// ---------------------------------------------------------------- small widgets

void gap(float ems) { ImGui::Dummy(ImVec2(0, em() * ems)); }

// The visible part of a translated "Label##id".
std::string shown(const char* label) {
    const char* t = T(label);
    const char* hash = std::strstr(t, "##");
    return hash ? std::string(t, size_t(hash - t)) : std::string(t);
}

// ---------------------------------------------------------------- finding a setting
// "Find a setting" (the magnifier, or Ctrl+F) switches to the right page, then the row with that
// label scrolls into view as it is drawn and glows for a moment. Any fieldLabel, toggleRow,
// subheading or feature card can be found this way: they all call jumpAnchor() with their label.
struct JumpState {
    std::string want;      // label to scroll to (English, as written in the code)
    int framesLeft = 0;    // give up if it isn't drawn within a few frames (hidden by another choice)
    int hits = 0;
    std::string glow;      // label glowing now
    double glowUntil = 0;
};
JumpState gJump;

// Call right before drawing the row with this label. Returns true while it should glow.
bool jumpAnchor(const char* label) {
    if (!gJump.want.empty() && gJump.want == label) {
        // A page that hasn't been shown yet doesn't know how tall it is on its first frame, so the
        // scroll can't reach far down yet: keep scrolling for a few frames.
        ImGui::SetScrollHereY(0.25f);
        if (gJump.glow != label || gJump.glowUntil < ImGui::GetTime()) {
            gJump.glow = label;
            gJump.glowUntil = ImGui::GetTime() + 2.0;
        }
        if (++gJump.hits >= 3) gJump.want.clear();
    }
    return gJump.glowUntil > ImGui::GetTime() && gJump.glow == label;
}

// A soft amber wash behind a row that was just found, fading out.
void jumpGlow(ImVec2 a, ImVec2 b) {
    const float t = float(std::max(0.0, gJump.glowUntil - ImGui::GetTime()) / 2.0);
    const float pad = em() * 0.35f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x - pad, a.y - pad * 0.5f), ImVec2(b.x + pad, b.y + pad * 0.5f),
                                              ImGui::GetColorU32(ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.22f * std::min(1.f, t * 2.f))),
                                              em() * 0.35f);
}

void fieldLabel(const char* text) {
    gap(0.22f);
    const bool glow = jumpAnchor(text);
    ImGui::TextColored(kLabel, "%s", shown(text).c_str());
    if (glow) jumpGlow(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + ImGui::GetContentRegionAvail().x,
                                                     ImGui::GetItemRectMax().y + ImGui::GetFrameHeight()));
}

// Small muted help text. Translated when the whole text is a known one.
void note(const std::string& text, const ImVec4& color = kMuted) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.9f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(T(text));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void heading(const char* text, float scale) {
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * scale);
    ImGui::TextUnformatted(T(text));
    ImGui::PopFont();
}

// The title of a card or page section: larger than the text around it, with a short bar in the
// accent colour before it, so the eye finds where each part starts.
void cardTitle(const char* text, float scale = 1.3f) {
    const float size = ImGui::GetStyle().FontSizeBase * scale;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float barW = std::max(3.f, em() * 0.22f), barGap = em() * 0.5f;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + size * 0.16f), ImVec2(p.x + barW, p.y + size * 1.02f),
                                              col(kSignal), barW * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + barW + barGap);
    heading(text, scale);
}

bool button(const char* label, bool primary = false, float width = -FLT_MIN) {
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, kSignal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kSignalHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kSignalDown);
        ImGui::PushStyleColor(ImGuiCol_Text, kOnSignal);
    }
    const bool pressed = ImGui::Button(T(label), ImVec2(width, 0));
    if (primary) ImGui::PopStyleColor(4);
    return pressed;
}

void drawSwitch(ImVec2 pos, float h, bool on, bool hovered, ImGuiID animId) {
    float* t = ImGui::GetStateStorage()->GetFloatRef(animId, on ? 1.f : 0.f);
    const float step = ImGui::GetIO().DeltaTime * 10.f;
    *t = on ? std::min(1.f, *t + step) : std::max(0.f, *t - step);
    const float w = h * 1.8f, r = h * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 track = mix(hovered ? kFieldActive : kFieldHover, hovered ? kSignalHover : kSignal, *t);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), col(track), r);
    dl->AddCircleFilled(ImVec2(pos.x + r + (w - h) * *t, pos.y + r), r - std::max(2.f, h * 0.16f),
                        col(mix(kMuted, kOnSignal, *t)), 24);
}

// A label on the left, a switch on the right; the whole row is clickable.
bool toggleRow(const char* label, bool* v) {
    const bool glow = jumpAnchor(label);
    ImGui::PushID(label);
    const float h = ImGui::GetFrameHeight(), w = ImGui::GetContentRegionAvail().x;
    const float sh = std::floor(em() * 1.2f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (glow) jumpGlow(p, ImVec2(p.x + w, p.y + h));
    const bool pressed = ImGui::InvisibleButton("##row", ImVec2(w, h));
    if (pressed) *v = !*v;
    const std::string text = shown(label);
    ImGui::GetWindowDrawList()->PushClipRect(p, ImVec2(p.x + w - sh * 2.2f, p.y + h), true);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, p.y + (h - em()) * 0.5f), col(kText), text.c_str());
    ImGui::GetWindowDrawList()->PopClipRect();
    drawSwitch(ImVec2(p.x + w - sh * 1.8f, p.y + (h - sh) * 0.5f), sh, *v, ImGui::IsItemHovered(), ImGui::GetID("##anim"));
    ImGui::PopID();
    return pressed;
}

// One choice out of a few, shown side by side.
bool segmented(const char* id, const char* const* labels, int count, int* value, float width = 0.f) {
    ImGui::PushID(id);
    const float h = ImGui::GetFrameHeight(), w = width > 0.f ? width : ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = ImGui::GetStyle().FrameRounding;
    const float pad = std::floor(h * 0.1f), segW = (w - pad * 2) / count;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(kField), r);
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const ImVec2 a(p.x + pad + segW * i, p.y + pad), b(a.x + segW, p.y + h - pad);
        ImGui::SetCursorScreenPos(ImVec2(a.x, p.y));
        if (ImGui::InvisibleButton(labels[i], ImVec2(segW, h)) && *value != i) {
            *value = i;
            changed = true;
        }
        const bool selected = *value == i, hovered = ImGui::IsItemHovered();
        if (selected) dl->AddRectFilled(a, b, col(kSignal), r - 1);
        else if (hovered) dl->AddRectFilled(a, b, col(kFieldHover), r - 1);
        const std::string text = shown(labels[i]);
        ImVec2 ts = ImGui::CalcTextSize(text.c_str());
        // Longer words (German, Russian...) shrink a little to fit rather than being cut off.
        const float room = segW - pad * 2, fit = ts.x > room ? std::max(0.74f, room / ts.x) : 1.f;
        if (fit < 1.f) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * fit);
            ts = ImGui::CalcTextSize(text.c_str());
        }
        dl->PushClipRect(a, b, true);
        dl->AddText(ImVec2(a.x + std::max(0.f, (segW - ts.x) * 0.5f), p.y + (h - ts.y) * 0.5f),
                    col(selected ? kOnSignal : hovered ? kText : kMuted), text.c_str());
        if (fit < 1.f) ImGui::PopFont();
        if (hovered && ts.x > segW - 2) ImGui::SetTooltip("%s", text.c_str());  // still cut off: show it whole
        dl->PopClipRect();
    }
    ImGui::PopID();
    return changed;
}

void sliderHeader(const char* label, const char* valueText) {
    gap(0.1f);
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::TextColored(kLabel, "%s", shown(label).c_str());
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(right - ImGui::CalcTextSize(valueText).x, ImGui::GetCursorScreenPos().y));
    ImGui::TextUnformatted(valueText);
}

void hideSliderParts() {
    const ImVec4 none(0, 0, 0, 0);
    for (ImGuiCol c : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab,
                       ImGuiCol_SliderGrabActive})
        ImGui::PushStyleColor(c, none);
}

// Draws a thin track and a round knob over the (invisible) slider that was just submitted.
void sliderOverlay(float t) {
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const bool hot = ImGui::IsItemActive() || ImGui::IsItemHovered();
    const float cy = std::floor((a.y + b.y) * 0.5f), r = (b.y - a.y) * 0.27f;
    const float th = std::max(3.f, std::floor((b.y - a.y) * 0.14f));
    const float x0 = a.x + r, x1 = b.x - r, x = x0 + (x1 - x0) * clamp01(t);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), col(kFieldActive), th * 0.5f);
    dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x, cy + th * 0.5f), col(kSignal), th * 0.5f);
    dl->AddCircleFilled(ImVec2(x, cy), ImGui::IsItemActive() ? r * 1.15f : r, col(hot ? kSignalHover : kText), 24);
}

bool sliderF(const char* label, float* v, float lo, float hi, const char* fmt, float shownScale = 1.f) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), TF(fmt), *v * shownScale);
    const bool glow = jumpAnchor(label);
    const ImVec2 top(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + em() * 0.1f);
    const float rowW = ImGui::GetContentRegionAvail().x;
    sliderHeader(label, buf);
    if (glow) jumpGlow(top, ImVec2(top.x + rowW, ImGui::GetItemRectMax().y + ImGui::GetFrameHeight()));
    ImGui::PushID(label);
    hideSliderParts();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = ImGui::SliderFloat("##s", v, lo, hi, "");
    ImGui::PopStyleColor(5);
    sliderOverlay((*v - lo) / (hi - lo));
    ImGui::PopID();
    return changed;
}

bool sliderI(const char* label, int* v, int lo, int hi, const char* fmt) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), TF(fmt), *v);
    const bool glow = jumpAnchor(label);
    const ImVec2 top(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + em() * 0.1f);
    const float rowW = ImGui::GetContentRegionAvail().x;
    sliderHeader(label, buf);
    if (glow) jumpGlow(top, ImVec2(top.x + rowW, ImGui::GetItemRectMax().y + ImGui::GetFrameHeight()));
    ImGui::PushID(label);
    hideSliderParts();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = ImGui::SliderInt("##s", v, lo, hi, "");
    ImGui::PopStyleColor(5);
    sliderOverlay(float(*v - lo) / float(hi - lo));
    ImGui::PopID();
    return changed;
}

bool containsNoCase(const std::string& hay, const std::string& needle) {
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
    return it != hay.end();
}

bool deviceCombo(const char* id, const std::vector<std::string>& list, std::string& selected) {
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, selected.empty() ? T("System default") : selected.c_str())) {
        if (ImGui::Selectable(T("System default"), selected.empty())) { selected.clear(); changed = true; }
        for (size_t i = 0; i < list.size(); ++i) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(list[i].c_str(), selected == list[i])) { selected = list[i]; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Language picker with a search box. With `model`, languages that model can't speak are marked.
void languageCombo(const char* id, std::string& code, bool allowAuto, const std::string& model = std::string()) {
    static std::map<std::string, std::string> filters;  // one search per picker
    std::string& filter = filters[id];
    const Language* cur = findLanguage(code);
    const bool onlyV3V4 = !model.empty() && !modelTakesTags(model);  // v3/v4 speak them all; others ~32
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, cur ? cur->name : allowAuto ? T("Detect automatically") : T("Pick a language"), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", T("Search languages"), &filter);
        if (allowAuto && filter.empty() && ImGui::Selectable(T("Detect automatically"), cur == nullptr)) code.clear();
        int shown = 0;
        for (const Language& l : languages()) {
            if (!languageMatches(l, filter)) continue;
            ++shown;
            ImGui::PushID(l.code);
            if (ImGui::Selectable(l.name, cur == &l)) code = l.code;
            if (onlyV3V4 && !l.everyModel) {
                ImGui::SameLine();
                ImGui::TextColored(kMuted, "%s", T("v3 / v4 only"));
            }
            ImGui::PopID();
        }
        if (shown == 0) ImGui::TextColored(kMuted, TF("No language matches \"%s\"."), filter.c_str());
        ImGui::EndCombo();
    }
    if (onlyV3V4 && cur && !cur->everyModel) {
        char b[256];
        std::snprintf(b, sizeof(b), TF("%s is only spoken by Eleven v3 and v4. Pick one of those models."), cur->name);
        note(b, kBusy);
    }
}

// A small play button for a voice's free sample, in your headphones (a square while it plays).
bool sampleButton(const char* id, const Voice& v, const UiModel& m, float size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hot = ImGui::IsItemHovered(), playing = m.previewing == v.id;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), col(hot ? kFieldHover : kField), size * 0.25f);
    const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    const float r = size * 0.2f;
    if (playing) dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r * 0.8f), ImVec2(c.x + r * 0.8f, c.y + r * 0.8f), col(kSignal), 1.5f);
    else dl->AddTriangleFilled(ImVec2(c.x - r * 0.7f, c.y - r), ImVec2(c.x - r * 0.7f, c.y + r), ImVec2(c.x + r, c.y), col(hot ? kSignal : kMuted));
    if (hot) ImGui::SetTooltip("%s", T(playing ? "Stop the sample" : "Hear a sample (free, only in your headphones)"));
    return pressed;
}

void voiceCombo(UiModel& m) {
    Settings& c = m.cfg;
    std::string current = c.voiceId.empty() ? T("Pick a voice") : c.voiceId;
    const Voice* picked = nullptr;
    for (const Voice& v : m.voices)
        if (v.id == c.voiceId) { current = v.name; picked = &v; }
    const float side = ImGui::GetFrameHeight();
    const bool canHear = picked && !picked->previewUrl.empty();
    ImGui::SetNextItemWidth(canHear ? ImGui::GetContentRegionAvail().x - side - ImGui::GetStyle().ItemSpacing.x : -FLT_MIN);
    if (ImGui::BeginCombo("##voice", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", T("Search voices"), &m.voiceFilter);
        const float rowH = ImGui::GetTextLineHeight();
        for (const Voice& v : m.voices) {
            if (!m.voiceFilter.empty() && !containsNoCase(v.name, m.voiceFilter)) continue;
            ImGui::PushID(v.id.c_str());
            const float w = ImGui::GetContentRegionAvail().x - (v.previewUrl.empty() ? 0.f : rowH + ImGui::GetStyle().ItemSpacing.x);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##pick", v.id == c.voiceId, 0, ImVec2(w, 0))) c.voiceId = v.id;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(at, col(kText), v.name.c_str());
            if (!v.category.empty())
                dl->AddText(ImVec2(at.x + ImGui::CalcTextSize(v.name.c_str()).x + em() * 0.6f, at.y), col(kMuted), v.category.c_str());
            if (!v.previewUrl.empty()) {
                ImGui::SameLine();
                if (sampleButton("##sample", v, m, rowH)) m.previewWanted = m.previewing == v.id ? std::string("stop") : v.id;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (canHear) {
        ImGui::SameLine();
        if (sampleButton("##samplepicked", *picked, m, side)) m.previewWanted = m.previewing == picked->id ? std::string("stop") : picked->id;
    }
}

void modelCombo(const std::vector<Model>& models, bool wantSts, std::string& selected) {
    std::string current = selected;
    for (const Model& mo : models)
        if (mo.id == selected) current = mo.name;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##model", current.c_str())) {
        for (const Model& mo : models) {
            if (wantSts ? !mo.voiceConversion : !mo.textToSpeech) continue;
            ImGui::PushID(mo.id.c_str());
            if (ImGui::Selectable(mo.name.c_str(), selected == mo.id)) selected = mo.id;
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "%s", mo.id.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

// Sound level with the opening threshold marked on it.
void meter(float levelDb, float& shownDb, float thresholdDb, bool open, bool showThreshold) {
    shownDb = std::max(levelDb, shownDb - 45.f * ImGui::GetIO().DeltaTime);  // jumps up, falls smoothly
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f dB", std::max(-99.f, shownDb));
    sliderHeader("Level", buf);  // (the meter)

    const float w = ImGui::GetContentRegionAvail().x, h = std::floor(em() * 0.7f), over = std::floor(em() * 0.25f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h + over * 2));
    p.y += over;
    auto xAt = [&](float db) { return p.x + w * clamp01((db + 70.f) / 70.f); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(kField), h * 0.5f);
    const bool live = open || (showThreshold && shownDb > thresholdDb);
    const float x = xAt(shownDb);
    if (x > p.x + h) dl->AddRectFilled(p, ImVec2(x, p.y + h), col(live ? kSignal : kMuted), h * 0.5f);
    if (showThreshold) {
        const float tx = std::floor(xAt(thresholdDb));
        dl->AddRectFilled(ImVec2(tx - 1, p.y - over), ImVec2(tx + 1, p.y + h + over), col(kText), 1.f);
    }
}

// The model manager for the translation service "Local" (defined further down, where the model
// and the actions are known).
void localModels(Settings& c);

// Shared by both pages: which service turns text into the other language.
extern UiModel* gUi;  // the frame's model (set in uiDraw)
void translationService(Settings& c, bool forCaptions) {
    fieldLabel("Translation service");
    {   // A dropdown: each service with one line on what it is and what it needs.
        struct Choice { int service; const char* name; const char* what; };
        static const Choice kChoices[] = {
            {kMyMemory, "Free", "No sign-up. Rough wording, small daily limit."},
            {kDeepL, "DeepL", "Accurate, textbook wording. Free key."},
            {kMicrosoft, "Microsoft", "Accurate, 100+ languages. Free Azure key."},
            {kClaude, "Claude", "AI: natural wording and slang. Paid key."},
            {kGemini, "Gemini", "AI: natural wording and slang. Has a free plan."},
            {kOpenAI, "OpenAI", "AI: natural wording and slang. Paid key."},
            {kLocal, "Local", "AI on this PC: free, private, no key."},
        };
        const Choice* cur = &kChoices[0];
        for (const Choice& ch : kChoices)
            if (ch.service == c.translateService) cur = &ch;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##service", T(cur->name), ImGuiComboFlags_HeightLarge)) {
            const float rowH = ImGui::GetTextLineHeight() * 2.f + em() * 0.45f;
            for (const Choice& ch : kChoices) {
                ImGui::PushID(ch.service);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##pick", ch.service == c.translateService, 0, ImVec2(0, rowH))) c.translateService = ch.service;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddText(ImVec2(at.x + em() * 0.3f, at.y + em() * 0.1f), col(ch.service == c.translateService ? kSignal : kText), T(ch.name));
                dl->AddText(ImVec2(at.x + em() * 0.3f, at.y + em() * 0.15f + ImGui::GetTextLineHeight()), col(kMuted), T(ch.what));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    const bool ai = isAiService(c.translateService);
    // The key can be pasted right here the first time; after that it lives in Settings.
    std::string* key = c.translateService == kDeepL    ? &c.deeplKey
                     : c.translateService == kMicrosoft ? &c.msKey
                     : c.translateService == kClaude ? &c.claudeKey
                     : c.translateService == kGemini ? &c.geminiKey
                     : c.translateService == kOpenAI ? &c.openaiKey
                                                     : nullptr;
    if (key && key->empty()) {
        // One field per service: with a shared ID, a key being typed when the service changed could
        // land in the wrong service's key.
        ImGui::PushID(c.translateService);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##servicekey", c.translateService == kDeepL    ? T("Paste your DeepL API key")
                                                 : c.translateService == kMicrosoft ? T("Paste your Microsoft Translator key (Azure)")
                                                 : c.translateService == kClaude ? T("Paste your Claude API key (console.anthropic.com)")
                                                 : c.translateService == kOpenAI ? T("Paste your OpenAI API key (platform.openai.com)")
                                                                                 : T("Paste your Gemini API key (aistudio.google.com)"),
                                 key, ImGuiInputTextFlags_Password);
        ImGui::PopID();
    }
    if (c.translateService == kMyMemory)
        note(forCaptions ? "MyMemory, no sign-up. Its 5,000 characters a day last only minutes of steady captions."
                         : "MyMemory, no sign-up. About 5,000 characters a day, and rougher wording.");
    else if (c.translateService == kDeepL)
        note("Accurate, textbook wording. A free DeepL API key covers 500,000 characters a month.");
    else if (c.translateService == kMicrosoft) {
        note("Accurate, textbook wording in 100+ languages. A free Azure key covers 2 million characters a month.");
        if (!c.msKey.empty()) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##msregion2", T("Region, only if Azure shows one (like westus)"), &c.msRegion);
        }
    } else if (c.translateService == kLocal) {
        note("An AI that runs on this PC: free, private, no key, and it works offline once downloaded.");
        localModels(c);
        // Small models are at their best in the big languages. For the others, said up front.
        if (!forCaptions) {
            static const std::string kWellKnown = " en es pt fr de it nl ru uk pl cs sv da no fi el tr ar he fa hi bn ur ta te mr id ms vi th fil ja ko zh yue ro hu bg hr sr sk ca ";
            const Language* to = findLanguage(c.targetLang);
            const bool known = !to || kWellKnown.find(" " + std::string(to->code) + " ") != std::string::npos;
            if (!known) {
                char b[300];
                std::snprintf(b, sizeof(b), TF("Local models are best at widely spoken languages. %s may come out rough or wrong here; an API key does it much better."), T(to->name));
                note(b, kBusy);
            }
        }
    } else
        note(forCaptions ? "An AI that understands slang, so captions read the way people meant them."
                         : "An AI that says it the way a native speaker would, slang included. If it takes longer than the "
                           "time limit, that phrase uses DeepL (or Free) instead.");
    if (!forCaptions) {
        toggleRow("Show what my translated line means", &c.showMeaning);
        note("Under \"They heard\", the line is put back into your language by a plain translator, so you can see "
             "it says what you meant. The voice doesn't wait for it.");
        note("Uses extra translation characters: every line is translated a second time. The free translator goes first "
             "(about 5,000 characters a day), then your Microsoft or DeepL key. No AI tokens and no ElevenLabs credits.",
             c.showMeaning ? kBusy : kMuted);
    }
    if (!ai) return;

    if (ai && c.deeplKey.empty() && c.translateService != kLocal)
        note("Tip: add a DeepL key in Settings too. It steps in when the AI is slow.");
    // A key for a second AI: it can cover the lines this one says no to or fails on.
    int otherAis = 0;
    for (int svc : {kClaude, kGemini, kOpenAI})
        if (svc != c.translateService && !(svc == kClaude ? c.claudeKey : svc == kGemini ? c.geminiKey : c.openaiKey).empty())
            ++otherAis;
    if (otherAis > 0 && c.translateService == kLocal) {
        toggleRow("Use my AI key as a backup", &c.aiBackup);
        note("Only for lines the local model says no to or fails on, so the key is hardly used.");
    } else if (otherAis > 0) {
        toggleRow("Use my other AI key as a backup", &c.aiBackup);
        note("Only for lines this AI says no to or fails on, never just slow ones, so no line is paid for twice.");
    }
    // A model on this PC that's downloaded can take the lines an online AI says no to.
    if (c.translateService != kLocal && gUi) {
        bool have = false;
        for (const LocalModelState& ms : gUi->local.models) have = have || ms.stage == kModelInstalled;
        if (have) {
            toggleRow("Send lines this AI declines or is too slow for to my local model", &c.localOnDecline);
            note(c.localOnDecline ? "The local model stays loaded for this, so it uses your graphics card's memory while you play. "
                                    "If it says no or is too slow as well, DeepL, Microsoft or Free translate the line as before."
                                  : "Off: those lines go to DeepL, Microsoft or Free.");
        }
    }
    if (!forCaptions) {
        toggleRow("Start translating before I finish talking", &c.earlyTranslate);
        note(!c.earlyTranslate ? "Off: a line is translated once you've finished it."
             : c.liveStt       ? "During the pause at the end of a sentence, the words so far are translated ahead of time. They're "
                                 "used only if you really had finished, so the voice says the same thing, sooner. If you go on "
                                 "talking, that request is thrown away: it can cost up to two extra AI requests per line. Lines that used it say "
                                 "\"Translated (early)\"."
                               : "Only helps sentences that wait for their rest right now. Turn on Live transcription (Microphone) "
                                 "for the full effect. If you go on talking, the early request is thrown away and still costs tokens.");
        fieldLabel("How you talk");
        static const char* const kLevels[] = {"Polite", "Casual", "Very casual", "My own"};
        segmented("##level", kLevels, 4, &c.speechLevel);
        if (c.speechLevel == kLevelCustom) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##customstyle", T("Describe how you talk"), &c.customStyle);
            const bool described = c.customStyle.find_first_not_of(" \t") != std::string::npos;
            note(described ? "Said your way. Describe who you're talking to, it works best: \"like my best friend\"."
                           : "For example: laid back and a bit sarcastic, like talking to a close friend while gaming.");
        } else {
            note(c.speechLevel == kLevelPolite ? "Friendly and respectful, like with someone you just met in a lobby."
                 : c.speechLevel == kLevelCasual ? "Like talking with a friend you hang out with a lot."
                                                 : "Like your closest friend: loose, slang, swearing kept.");
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##persona", T("Sound like... (optional)"), &c.persona);
        if (c.persona.empty()) note("For example: a guy in his 20s from Seoul. Region and age change the slang.");
        else note("Describe a person here, not what to do. For instructions like \"more slang\", pick My own above.");
        toggleRow("Match how they talk to me", &c.mirror);
        note(c.mirror ? "Uses the last few lines from the Listen captions to pick up their words. Your style above "
                        "stays; it only leans a little toward theirs. Needs Listen on."
                      : "Translates each phrase on its own.");
    }
    float seconds = c.aiTimeoutMs / 1000.f;
    if (sliderF("Time limit", &seconds, 0.8f, 4.f, "%.1f s")) c.aiTimeoutMs = int(seconds * 1000.f + 0.5f);
    if (c.translateService == kLocal) {  // its model is picked above, by name
        note("A local model gets at least 5 seconds: its first lines are slow, then it speeds up.");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    const bool open = ImGui::TreeNodeEx(T(forCaptions ? "AI model##listenmodel" : "AI model##voicemodel"),
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor(2);
    if (open) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (c.translateService == kClaude) ImGui::InputText("##claudemodel", &c.claudeModel);
        else if (c.translateService == kOpenAI) ImGui::InputText("##openaimodel", &c.openaiModel);
        else ImGui::InputText("##geminimodel", &c.geminiModel);
        note(c.translateService == kClaude   ? "claude-haiku-5-5 is the fastest. claude-sonnet-5-5 words things a bit better, but is slower."
             : c.translateService == kOpenAI ? "gpt-6-luna is the fastest and cheapest. gpt-6.2-sol words things a bit better, but is slower and costs more."
                                             : "gemini-3.5-flash-lite is the fastest. gemini-3.6-flash words things a bit better, but is slower.");
    }
}

// Translate to: which regional way of speaking (Mexican Spanish, Kuwaiti Arabic...).
void dialectPicker(Settings& c) {
    const std::vector<const Variety*> list = varietiesOf(c.targetLang);
    std::string& id = c.variety[c.targetLang];
    const Variety* cur = findVariety(id);
    if (cur && cur->lang != c.targetLang) cur = nullptr;
    const bool custom = id == "custom";
    fieldLabel("Dialect");
    std::string preview = custom ? T("Other (type it)") : cur ? T(cur->name) : T("Standard");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##dialect", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(T("Standard"), !custom && !cur)) id.clear();
        for (const Variety* v : list) {
            ImGui::PushID(v->id);
            if (ImGui::Selectable(T(v->name), cur == v)) id = v->id;
            ImGui::PopID();
        }
        if (ImGui::Selectable(T("Other (type it)"), custom)) id = "custom";
        ImGui::EndCombo();
    }
    if (custom) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##customdialect", T("For example ar-KW, or Rioplatense"), &c.customVariety[c.targetLang]);
    }
    if (id.empty()) c.variety.erase(c.targetLang);
    const bool ai = isAiService(c.translateService);
    if (!ai && (custom || cur))
        note("Dialects need Claude, Gemini or OpenAI. DeepL and Free use the standard language (DeepL knows a few regional "
             "spellings, like Latin American Spanish or Brazilian Portuguese).", kBusy);
    else if (custom || cur)
        note("The words come out in this dialect. The accent comes from the voice: for the real thing, pick a voice "
             "from that region.");
}

const char* const kTalkingLabels[] = {"Auto (their voice)", "Don't know", "A guy", "A girl", "A group"};

// "I am" and "Talking to": for languages whose words change with gender.
void genderPicker(UiModel& m) {
    Settings& c = m.cfg;
    fieldLabel("I am");
    static const char* const kMe[] = {"Not set", "A man", "A woman", "Nonbinary"};
    segmented("##mygender", kMe, 4, &c.myGender);
    fieldLabel("Talking to");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##talkingto", T(kTalkingLabels[c.talkingTo]))) {
        for (int i = 0; i < kTalkCount; ++i)
            if (ImGui::Selectable(T(kTalkingLabels[i]), c.talkingTo == i)) c.talkingTo = i;
        ImGui::EndCombo();
    }
    if (c.talkingTo == kTalkAuto) {
        if (m.talkingGuessed)
            note(m.talkingResolved == kListenerMan ? "Right now: a guy, guessed from the last voice Listen heard."
                                                   : "Right now: a girl, guessed from the last voice Listen heard.", kSignal);
        else
            note("Can't tell yet: it goes by the last voice Listen heard, so turn Listen on. Until then, wording that "
                 "fits anyone.");
    }
    char b[400];
    std::snprintf(b, sizeof(b), TF("Switch it fast with your hotkey (%s) or from the CIMA Box tab in the SteamVR menu."),
                  c.talkKey ? keyName(c.talkKey).c_str() : T("set one in Settings"));
    note(b);
}

// A button that waits for the next key press. `which` tells two binders apart.
void keyBinder(UiModel& m, int which, int& key) {
    const bool mine = m.binding && m.bindingWhich == which;
    const float clearW = em() * 4.2f;
    const std::string label = mine ? std::string(T("Press a key or mouse button")) : key ? keyName(key) : std::string(T("Not set"));
    ImGui::PushID(which);
    if (button((label + "##bind").c_str(), false, ImGui::GetContentRegionAvail().x - clearW - ImGui::GetStyle().ItemSpacing.x)) {
        m.binding = true;
        m.bindArmed = false;
        m.bindingWhich = which;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(key == 0 && !mine);
    if (button(mine ? "Cancel" : "Clear", false, clearW)) {
        if (!mine) key = 0;
        m.binding = false;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    if (mine) {
        const int k = scanKey();
        if (!m.bindArmed) m.bindArmed = (k == 0);  // wait until whatever was held is released
        else if (k != 0) { key = k; m.binding = false; }
    }
}

void beginPanel(const char* id, ImVec2 size, const char* title) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.05f, em() * 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));  // its shade sets it apart; no outline needed
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    const bool glow = jumpAnchor(title);
    cardTitle(title);
    if (glow) jumpGlow(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + ImGui::GetContentRegionAvail().x, ImGui::GetItemRectMax().y));
    gap(0.4f);
}

// A smaller heading inside a panel, with a little room above it.
void subheading(const char* text) {
    gap(0.75f);
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), col(kLine));
    }
    gap(0.65f);
    const bool glow = jumpAnchor(text);
    heading(text, 1.12f);
    if (glow) jumpGlow(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + ImGui::GetContentRegionAvail().x, ImGui::GetItemRectMax().y));
    gap(0.1f);
}

// The less-used settings of a section, folded under one row that says what's inside, so the
// everyday ones stay easy to see. Open or closed is remembered while the app runs. Finding a
// setting (search) opens it, since the setting may be in there.
bool moreRow(const char* id, const char* label, const char* inside) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID(id);
    bool open = st->GetBool(key, false);
    if (!gJump.want.empty()) open = true;
    gap(0.55f);
    const float w = ImGui::GetContentRegionAvail().x, h = em() * (open ? 2.0f : 2.9f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    if (ImGui::InvisibleButton("##more", ImVec2(w, h))) open = !open;
    const bool hot = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = em() * 0.5f;
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), col(hot ? kMuted : kLine), r, 0, 1.3f);
    const float tx = p.x + em() * 0.8f;
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
    const char* title = T(open ? "Fewer settings" : label);
    dl->AddText(ImVec2(tx, p.y + (open ? (h - em()) * 0.5f : em() * 0.45f)), col(hot ? kSignal : kText), title);
    ImGui::PopFont();
    if (!open) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.86f);
        dl->PushClipRect(p, ImVec2(p.x + w - em() * 2.f, p.y + h), true);
        dl->AddText(ImVec2(tx, p.y + em() * 1.55f), col(kMuted), T(inside));
        dl->PopClipRect();
        ImGui::PopFont();
    }
    // A chevron on the right: down when closed, up when open.
    const ImVec2 c(p.x + w - em() * 1.0f, p.y + h * 0.5f);
    const float s = em() * 0.3f, d = open ? -1.f : 1.f;
    dl->AddLine(ImVec2(c.x - s, c.y - s * 0.5f * d), ImVec2(c.x, c.y + s * 0.5f * d), col(hot ? kSignal : kMuted), 1.8f);
    dl->AddLine(ImVec2(c.x, c.y + s * 0.5f * d), ImVec2(c.x + s, c.y - s * 0.5f * d), col(hot ? kSignal : kMuted), 1.8f);
    st->SetBool(key, open);
    if (open) gap(0.2f);
    return open;
}

// ---------------------------------------------------------------- the three stages

void inputPanel(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    fieldLabel("Microphone");
    if (deviceCombo("##mic", m.inputDevices, c.inDevice)) act.reopenInput = true;

    meter(m.levelDb, m.meterDb, c.thresholdDb, m.recording, c.activation == kVoiceActivity);

    fieldLabel("Start talking by");
    static const char* const kModes[] = {"Voice activation", "Push to talk"};
    segmented("##activation", kModes, 2, &c.activation);

    if (c.activation == kVoiceActivity) {
        sliderF("Opens above", &c.thresholdDb, -70.f, 0.f, "%.0f dB");
        note("Set the marker just right of where the meter sits when you are silent.");
    } else {
        fieldLabel("Key");
        keyBinder(m, 0, c.pttKey);
        note(kGlobalHotkey ? "Works while another window has focus. Recording runs a moment past release so the last word isn't cut."
                           : "Works while this window has focus. Recording runs a moment past release so the last word isn't cut.");
        fieldLabel("Hold or tap");
        static const char* const kHow[] = {"Hold the key", "Tap to start and stop"};
        int how = c.pttTap ? 1 : 0;
        if (segmented("##ptthow", kHow, 2, &how)) c.pttTap = how == 1;
        note(c.pttTap ? "Tap once to start recording and again to stop. Handy for long stories, or a VR button."
                      : "Records while you hold it down.");
        // A controller button (SteamVR) as push to talk as well.
        toggleRow("A VR controller button too", &c.vr.pttButton);
        if (c.vr.pttButton) {
            const VrState& v = m.vr;
            const char* state = !v.connected ? v.status.c_str() : v.button.empty() ? "Connecting to SteamVR" : v.button.c_str();
            const ImVec4 sc = !v.connected ? kMuted : v.buttonDown ? kSignal : v.buttonBound ? kText : kBusy;
            ImGui::TextColored(sc, "%s", T(state));
            ImGui::BeginDisabled(!v.connected);
            if (button("Pick the button")) act.openVrBindings = true;
            ImGui::EndDisabled();
            note(v.connected ? "Opens SteamVR's controller bindings for CIMA Box: choose \"Push to talk\" and press the button you want. "
                               "The game still gets that button too, so pick one it doesn't use much (like a thumbstick click)."
                             : "Start SteamVR, then pick the button here.");
            toggleRow("Buzz when recording starts and stops", &c.vr.buzz);
        }
        fieldLabel(c.pttTap ? "While not recording" : "While not holding the key");
        static const char* const kUp[] = {"Silent", "My real voice"};
        int up = c.pttRealVoice ? 1 : 0;
        if (segmented("##pttup", kUp, 2, &up)) c.pttRealVoice = up == 1;
        note(c.pttRealVoice ? "People hear your own voice. Hold the key for a line in the new voice: your mic goes quiet from the press until that line has played."
                            : "People hear nothing until you hold the key, then only the new voice.");
    }
    if (!moreRow("##moremic", "More microphone settings",
                 c.activation == kVoiceActivity ? "Pause length, sentence wait, longest phrase, live transcription"
                                                : "Longest phrase, live transcription"))
        return;
    if (c.activation == kVoiceActivity) {
        sliderI("Pause that ends a phrase", &c.holdMs, 150, 1500, "%d ms");
        note("Shorter replies sooner, but splits sentences at small pauses.");
        sliderI("Wait for the rest of a sentence", &c.sentenceWaitMs, 0, 2000, c.sentenceWaitMs > 0 ? "%d ms" : "Off");
        note(c.sentenceWaitMs > 0
                 ? "When you pause mid-sentence (after a comma, \"and\", \"because\"...), Translate and Re-speak wait up to this long for you to go on, then say the whole sentence at once. Finished sentences never wait. Raise it if you often pause to think."
                 : "Off: a pause ends a phrase even mid-sentence. Sentences cut by a phrase that runs too long are still kept whole.");
    }
    sliderI("Longest phrase", &c.maxSeconds, 3, 30, "%d s");
    toggleRow("Live transcription", &c.liveStt);
    note(c.liveStt
             ? "Translate and Re-speak: your voice goes to ElevenLabs while you talk, so your words are ready the moment you stop, and \"You said\" fills in as you speak. If it has any problem, that phrase is uploaded the regular way. New in 3.0."
             : "Translate and Re-speak: send your voice to ElevenLabs while you talk, so the words are ready the moment you stop (a little less delay). New in 3.0.");
}

void voicePanel(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    static const char* const kEngines[] = {"Changer", "Re-speak", "Translate"};
    segmented("##engine", kEngines, 3, &c.engine);
    note(c.engine == kVoiceChanger ? "Keeps your own timing and emotion. The quickest mode."
         : c.engine == kRespeak    ? "Writes down what you say, then the voice reads it."
                                   : "Writes down what you say, translates it, then the voice reads it.");

    fieldLabel("Voice");
    voiceCombo(m);
    if (m.voices.empty()) {
        if (c.apiKey.empty()) {
            note("Add your ElevenLabs API key in Settings (bottom left) to see your voices.");
        } else {
            ImGui::BeginDisabled(m.loading);
            if (button(m.loading ? "Loading voices" : "Load voices and models", true)) act.loadCatalog = true;
            ImGui::EndDisabled();
        }
    }
    if (c.engine != kVoiceChanger) {
        fieldLabel("I speak");
        languageCombo("##spoken", c.spokenLang, true);
    }
    if (c.engine == kTranslate) {
        fieldLabel("Translate to");
        languageCombo("##target", c.targetLang, false, c.ttsModel);
        dialectPicker(c);
        translationService(c, false);
    }
    if (!moreRow("##morevoice", "More voice settings",
                 c.engine == kVoiceChanger ? "Model, fine-tuning the voice"
                 : c.engine == kTranslate  ? "Model, a voice per language, gender, emotion, fine-tuning"
                                           : "Model, emotion, fine-tuning"))
        return;
    if (c.engine == kTranslate) {
        toggleRow("A voice for each language", &c.voicePerLanguage);
        if (c.voicePerLanguage) note("Picking another language brings back the voice you last used with it.");
    }

    fieldLabel("Model");
    modelCombo(m.models, c.engine == kVoiceChanger, c.engine == kVoiceChanger ? c.stsModel : c.ttsModel);

    if (c.engine == kTranslate) {
        if (isAiService(c.translateService)) {
            subheading("Gender");
            note("Many languages change words for who is speaking and who is listening (cansado / cansada).");
            genderPicker(m);
        }
    }
    if (c.engine != kVoiceChanger) {
        gap(0.2f);
        toggleRow("Carry my emotion", &c.emotion);
        if (c.emotion) {
            static const char* const kStrengths[] = {"Subtle", "Strong"};
            segmented("##emotionstrength", kStrengths, 2, &c.emotionStrength);
            note(c.emotionStrength == kEmotionStrong ? "Picks up smaller changes in your voice."
                                                     : "Only picks up clear changes in your voice.");
            if (modelTakesTags(c.ttsModel))
                note("Follows the words you stress, words you draw out, your pauses, laughs, and how each part of a "
                     "sentence was said: excited, whispered, shouted, hesitant, rushed, slow, sad, flat. It compares you "
                     "with your own usual voice and how much it normally varies, so it works on any mic. Adds no delay.");
            else
                note("This model would read tags out loud and spell out capitals, so only pauses, questions and drawn-out "
                     "words carry over. Pick a v3 or v4 model for the full effect.",
                     kBusy);
            if (!c.baseline.ready()) {
                char b[320];
                std::snprintf(b, sizeof(b), TF("Learning your usual voice. Phrases still needed (just talk normally): %d"),
                              VoiceBaseline::kReady - c.baseline.phrases);
                note(b);
            }
            if (button("Relearn my usual voice")) act.resetBaseline = true;
            note("It keeps learning as you talk, and relearns the mic part by itself when you switch microphones.");
        } else {
            note("Off: the voice reads only your words, in its own tone.");
        }
    }

    const bool sts = c.engine == kVoiceChanger;
    const bool v4 = !sts && c.ttsModel.find("v4") != std::string::npos;
    gap(0.3f);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    const bool tune = ImGui::TreeNodeEx(T("Fine-tune the voice##tune"), ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor();
    if (!tune) {
        note("Stability, similarity, style, speed and more. The defaults suit most voices.");
        return;
    }
    sliderF("Stability", &c.vs.stability, 0.f, 1.f, "%.2f");
    sliderF("Similarity", &c.vs.similarity, 0.f, 1.f, "%.2f");
    ImGui::BeginDisabled(v4);
    sliderF("Style", &c.vs.style, 0.f, 1.f, "%.2f");
    if (!sts) sliderF("Speed", &c.vs.speed, 0.7f, 1.2f, "%.2f");
    toggleRow("Speaker boost", &c.vs.speakerBoost);
    ImGui::EndDisabled();
    if (sts) toggleRow("Remove background noise", &c.removeNoise);
    if (v4) note("v4 models take stability and similarity (plus clone strength for professional clones).");

    // Clone strength: Eleven v4 with a professional voice clone only.
    bool professional = false;
    for (const Voice& v : m.voices) professional = professional || (v.id == c.voiceId && v.category == "professional");
    if (v4 && professional) {
        toggleRow("Set clone strength", &c.vs.setCloneStrength);
        if (c.vs.setCloneStrength) {
            sliderF("Clone strength", &c.vs.cloneStrength, 0.f, 1.f, "%.2f");
            note("Higher follows your training recordings more closely; lower smooths the voice and lets it act more. "
                 "ElevenLabs suggests 0 for most voices, 0.5 for strong accents or unusual voices.");
        } else {
            note("Off: uses the clone strength saved with this voice on ElevenLabs.");
        }
    }

    gap(0.2f);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    const bool open = ImGui::TreeNodeEx(T("Voice and model IDs"), ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor(2);
    if (open) {
        fieldLabel("Voice ID");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##voiceid", &c.voiceId);
        fieldLabel("Model ID");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##modelid", sts ? &c.stsModel : &c.ttsModel);
    }
}

void outputPanel(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    fieldLabel("Send the new voice to");
    if (deviceCombo("##out", m.outputDevices, c.outDevice)) act.reopenOutput = true;
    note("For Discord or a game, pick a virtual cable here and choose the same cable as the microphone there.");
    {
        const bool glow = jumpAnchor("Check my setup");
        if (button("Check my setup", false, ImGui::CalcTextSize(T("Check my setup")).x + em() * 2.4f)) act.startCheck = true;
        if (glow) jumpGlow(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::SameLine(0, em() * 0.6f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kMuted, "%s", T("A one-minute test of your mic, cable and keys."));
    }
    sliderF("Volume##outvol", &c.outVolume, 0.f, 2.f, "%.0f%%", 100.f);

    gap(0.1f);
    if (toggleRow("Hear it yourself too", &c.monitor)) act.reopenMonitor = true;
    if (c.monitor) {
        if (deviceCombo("##mon", m.outputDevices, c.monDevice)) act.reopenMonitor = true;
        sliderF("Volume##monvol", &c.monVolume, 0.f, 2.f, "%.0f%%", 100.f);
    }

    gap(0.3f);
    toggleRow("Show in VRChat chatbox", &c.chatbox);
    if (c.chatbox) {
        static const char* const kShow[] = {"What I said", "Spoken as", "Both"};
        segmented("##chatboxshow", kShow, 3, &c.chatboxShow);
        note(c.engine == kVoiceChanger ? "Changer mode doesn't write anything down, so this also transcribes each phrase "
                                         "(at the same time as the voice, so no added delay, but it is billed)."
                                       : "Needs OSC turned on in VRChat. At most one message every 1.6 seconds.");
    }
    if (!moreRow("##moreout", "More output settings", "Audio quality, pass my mic through, match my mic, rescan devices")) return;
    fieldLabel("Audio quality");
    static const char* const kQualities[] = {"Standard", "High", "Best"};
    segmented("##quality", kQualities, 3, &c.quality);
    note(c.quality == kQualityStandard ? "24 kHz. Smallest download and upload, so the lowest delay. Any plan."
         : c.quality == kQualityHigh   ? "44.1 kHz MP3 at 192 kbps, like the website. Needs Creator or above. Changer also "
                                         "gets your voice at full quality (a little more upload time)."
                                       : "44.1 kHz uncompressed, the best ElevenLabs sends. Needs Pro or above. Changer also "
                                         "gets your voice at full quality (a little more upload time).");
    if (!m.qualityNote.empty()) note(m.qualityNote, kBusy);
    toggleRow("Pass my mic through when off", &c.passthrough);
    note(c.passthrough ? "With the voice changer switched off, your own voice goes to the output instead, so other "
                         "apps can stay on this device all the time."
                       : "With the voice changer switched off, the output stays silent.");
    toggleRow("Match my mic", &c.matchMic);
    if (c.matchMic) {
        sliderF("Amount", &c.matchAmount, 0.f, 1.f, "%.2f");
        if (m.matchProgress < 1.f) {
            char b[320];
            std::snprintf(b, sizeof(b), TF("Learning how your mic and the new voice sound: %d%%. Keep talking."),
                          int(m.matchProgress * 100.f + 0.5f));
            note(b);
        } else {
            note("Gives the new voice your mic's tone and loudness, so it sounds like it came through the same mic as "
                 "your real voice. Relearns when you pick another mic. No added delay.");
        }
        if (button("Relearn my mic")) act.resetMatch = true;
    }
    gap(0.1f);
    if (button("Rescan devices")) act.rescanDevices = true;
}

// ---------------------------------------------------------------- service status

ImVec4 healthColor(int h) {
    return h == kHealthOk ? kVoices[2] : h == kHealthMinor ? kSignal : h == kHealthMajor ? kDanger
         : h == kHealthMaintenance ? kBusy : kMuted;
}

const char* healthWord(int h) {
    return h == kHealthOk ? "Working" : h == kHealthMinor ? "Some problems" : h == kHealthMajor ? "Outage"
         : h == kHealthMaintenance ? "Maintenance" : "Couldn't check";
}

// The first service in trouble that this setup actually uses (ElevenLabs always, plus the
// translation service when one is picked). Null when everything in use is fine.
const ServiceStatus* troubleInUse(const UiModel& m) {
    const char* translator = m.cfg.translateService == kClaude   ? "Claude"
                           : m.cfg.translateService == kGemini   ? "Gemini"
                           : m.cfg.translateService == kOpenAI   ? "OpenAI"
                           : !m.cfg.deeplKey.empty()             ? "DeepL"
                                                                 : "";
    for (const ServiceStatus& st : m.services)
        if ((st.name == "ElevenLabs" || st.name == translator) && (st.health == kHealthMinor || st.health == kHealthMajor))
            return &st;
    return nullptr;
}

// One line per service: a dot, the name, what its status page says. Click to open the page.
void serviceRows(UiModel& m, UiActions& act) {
    for (const ServiceStatus& st : m.services) {
        ImGui::PushID(st.name.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rowH = ImGui::GetTextLineHeight(), w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##row", ImVec2(w, rowH + em() * 0.15f))) act.openUrl = st.page;
        const bool hot = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(p.x + em() * 0.3f, p.y + rowH * 0.55f), em() * 0.24f, col(healthColor(st.health)), 16);
        const float nameX = p.x + em() * 0.9f;
        dl->AddText(ImVec2(nameX, p.y), col(hot ? kSignal : kText), st.name.c_str());
        const std::string what = T(healthWord(st.health));
        const float whatX = nameX + em() * 5.4f;
        dl->PushClipRect(ImVec2(whatX, p.y), ImVec2(p.x + w, p.y + rowH + 2), true);
        dl->AddText(ImVec2(whatX, p.y), col(healthColor(st.health)), what.c_str());
        dl->PopClipRect();
        if (hot) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(em() * 26.f);
            ImGui::TextUnformatted(T(st.detail));
            ImGui::TextColored(kMuted, "%s", T("Click to open the status page"));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------- what's left (bottom of every page)

// ---------------------------------------------------------------- spending limit notice

// A thin amber bar under the header, on every page, until it's acknowledged.
void spendBanner(UiModel& m, UiActions& act) {
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() + em() * 0.7f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec4 c = m.spendStopped ? kDanger : kSignal;
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(ImVec4(c.x, c.y, c.z, 0.12f)), r);
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(ImVec4(c.x, c.y, c.z, 0.7f)), r);
    std::string text = m.spendAlert;
    if (m.spendStopped) text += std::string(" ") + T("Talking and listening were switched off.");
    const float okW = em() * 5.f, setW = ImGui::CalcTextSize(T("Change limit")).x + em() * 1.6f;
    const float textW = w - okW - setW - em() * 2.4f;
    ImGui::PushClipRect(a, ImVec2(a.x + em() + textW, a.y + h), true);
    dl->AddText(ImVec2(a.x + em() * 0.9f, a.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col(kText), text.c_str());
    ImGui::PopClipRect();
    if (ImGui::CalcTextSize(text.c_str()).x > textW) {  // cut off: the whole of it on hover
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y));
        ImGui::InvisibleButton("##spendtext", ImVec2(textW, h));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
    }
    const float by = a.y + (h - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(a.x + w - okW - setW - em() * 1.0f, by));
    if (button("Change limit", false, setW)) {
        m.cfg.page = kPageSettings;
        m.scrollToSpend = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(a.x + w - okW - em() * 0.4f, by));
    if (button("OK", true, okW)) act.spendOk = true;
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + em() * 0.5f));
    ImGui::Dummy(ImVec2(0, 0));
}

// ---------------------------------------------------------------- header and status strip

// ---------------------------------------------------------------- find a setting
// Every setting someone might look for, where it lives, and other words people might type for it.
// `label` must be the exact English label the page draws (it's how the row is found and glows).
// `toggle`: for simple on/off settings, the switch is right in the search results.
struct Findable {
    const char* label;
    int page;
    const char* section;   // the panel or card it's in (a label that is translated already)
    const char* words;     // more ways to say it, English, lower case
    bool* (*toggle)(Settings&);
};
#define TOGGLE(expr) [](Settings& c) -> bool* { return &c.expr; }
const Findable kFindables[] = {
    // Home
    {"Quick toggles", kPageHome, "Home", "shortcuts tiles quick home toggles edit", nullptr},
    // My voice
    {"Microphone", kPageVoice, "Microphone", "mic input device", nullptr},
    {"Local models", kPageVoice, "Voice and language", "local offline on this pc private free no key download model gemma llama graphics card", nullptr},
    {"Start talking by", kPageVoice, "Microphone", "push to talk voice activation ptt", nullptr},
    {"Hold or tap", kPageVoice, "Microphone", "push to talk tap toggle hold press once start stop", nullptr},
    {"While not holding the key", kPageVoice, "Microphone", "push to talk real voice between presses passthrough", nullptr},
    {"A VR controller button too", kPageVoice, "Microphone", "vr controller button steamvr index quest touch push to talk ptt vibration buzz", TOGGLE(vr.pttButton)},
    {"Pause that ends a phrase", kPageVoice, "Microphone", "silence pause hold phrase end split", nullptr},
    {"Wait for the rest of a sentence", kPageVoice, "Microphone", "sentence pause split broken grammar story wait join", nullptr},
    {"Longest phrase", kPageVoice, "Microphone", "long phrase max seconds split story", nullptr},
    {"Live transcription", kPageVoice, "Microphone", "realtime real time streaming speech to text faster delay latency scribe", TOGGLE(liveStt)},
    {"Voice", kPageVoice, "Voice and language", "voice pick choose elevenlabs", nullptr},
    {"A voice for each language", kPageVoice, "Voice and language", "remember voice per language", TOGGLE(voicePerLanguage)},
    {"Model", kPageVoice, "Voice and language", "eleven model v3 v4 flash", nullptr},
    {"Translate to", kPageVoice, "Voice and language", "target language translation output language", nullptr},
    {"I speak", kPageVoice, "Voice and language", "my language source", nullptr},
    {"Translation service", kPageVoice, "Voice and language", "claude gemini openai deepl free ai", nullptr},
    {"How you talk", kPageVoice, "Voice and language", "style polite casual very casual formal slang my own", nullptr},
    {"Match how they talk to me", kPageVoice, "Voice and language", "mirror match style", TOGGLE(mirror)},
    {"Start translating before I finish talking", kPageVoice, "Voice and language", "early faster speed latency delay ahead head start pause", TOGGLE(earlyTranslate)},
    {"Dialect", kPageVoice, "Voice and language", "accent region variety kansai gulf egyptian mexican", nullptr},
    {"I am", kPageVoice, "Voice and language", "gender man woman me", nullptr},
    {"Talking to", kPageVoice, "Voice and language", "gender listener who", nullptr},
    {"Carry my emotion", kPageVoice, "Voice and language", "emotion tone feelings excited whisper", TOGGLE(emotion)},
    {"Remove background noise", kPageVoice, "Voice and language", "noise", TOGGLE(removeNoise)},
    {"Check my setup", kPageVoice, "Where it goes", "test check setup cable crackle double echo hear twice not working problem", nullptr},
    {"Send the new voice to", kPageVoice, "Where it goes", "output cable virtual device speakers", nullptr},
    {"Hear it yourself too", kPageVoice, "Where it goes", "monitor hear myself headphones", TOGGLE(monitor)},
    {"Pass my mic through when off", kPageVoice, "Where it goes", "passthrough real voice off", TOGGLE(passthrough)},
    {"Match my mic", kPageVoice, "Where it goes", "mic tone level sound like my mic", TOGGLE(matchMic)},
    {"Show in VRChat chatbox", kPageVoice, "Where it goes", "chatbox osc vrchat text bubble", TOGGLE(chatbox)},
    // Text
    {"Say it out loud", kPageText, "Voice", "typed text speak", TOGGLE(textSpeak)},
    // Listen
    {"Listen to", kPageListen, "Source", "source app game speakers capture", nullptr},
    {"Translate into", kPageListen, "Source", "captions language my language", nullptr},
    {"They speak", kPageListen, "Source", "their language detect", nullptr},
    {"Tell speakers apart", kPageListen, "Source", "speakers who", TOGGLE(listen.tellApart)},
    {"Show the original words", kPageListen, "Source", "original text", TOGGLE(listen.showOriginal)},
    {"Skip speech in my language", kPageListen, "Source", "skip ignore my language", TOGGLE(listen.onlyOtherLanguages)},
    {"Send to VRChat chatbox", kPageListen, "Source", "chatbox osc captions", TOGGLE(listen.chatbox)},
    // VR
    {"Reset VR captions", kPageVR, "Placement", "vr captions stuck frozen not updating stopped reset reconnect", nullptr},
    {"Where", kPageVR, "Placement", "vr panel position head hand fixed", nullptr},
    {"What I say", kPageVR, "Placement", "vr show me", TOGGLE(vr.showMe)},
    {"CIMA Box tab in the SteamVR menu", kPageVR, "Placement", "steamvr dashboard menu tab", TOGGLE(vr.dashboard)},
    {"Text size", kPageVR, "Look", "vr font size bigger", nullptr},
    {"Glow", kPageVR, "Look", "vr glow", TOGGLE(vr.style.glow)},
    // Features
    {"Cancel before it's spoken", kPageFeatures, "Talking", "preview stop cancel wait", nullptr},
    {"Answer in their language", kPageFeatures, "Talking", "auto target reply their language", TOGGLE(fx.answerTheirs)},
    {"Languages", kPageFeatures, "Talking", "languages i speak around me detect", nullptr},
    {"Clean up my swearing", kPageFeatures, "Talking", "swear curse profanity bleep beep censor", nullptr},
    {"Words for DeepL", kPageFeatures, "Talking", "deepl words swear profanity slang softened refused", TOGGLE(fx.deeplWordsOn)},
    {"Best translations", kPageFeatures, "Talking", "best quality bigger model sonnet", TOGGLE(fx.best)},
    {"Try a line", kPageFeatures, "Talking", "test compare styles", nullptr},
    {"More hotkeys", kPageFeatures, "Talking", "hotkey key real voice stop speaking", nullptr},
    {"Screen translation", kPageFeatures, "Hearing and reading", "screen ocr screenshot snapshot chat box read picture", nullptr},
    {"Hear them in my ear", kPageFeatures, "Hearing and reading", "ear read aloud tts headphones", TOGGLE(fx.hearThem)},
    {"Captions while they talk", kPageFeatures, "Hearing and reading", "live captions words", TOGGLE(fx.liveCaptions)},
    {"Hide swearing (f***)", kPageFeatures, "Captions", "mask swear captions censor", TOGGLE(fx.maskCaptions)},
    {"Sound labels ([laughs], [music])", kPageFeatures, "Captions", "laughs music sounds", TOGGLE(fx.soundLabels)},
    {"Skip music and noise", kPageFeatures, "Captions", "music noise ignore", TOGGLE(fx.ignoreNoise)},
    {"Big, high-contrast captions", kPageFeatures, "Captions", "big contrast readable large", TOGGLE(fx.highContrast)},
    {"Same voice, same color", kPageFeatures, "Captions", "voice colors numbers mute person", TOGGLE(fx.voiceIds)},
    {"Guess gender from their words", kPageFeatures, "Captions", "gender", TOGGLE(fx.genderFromWords)},
    {"Caption window over the game", kPageFeatures, "Hearing and reading", "overlay window click through see-through", nullptr},
    {"Quick phrases", kPageFeatures, "Handy", "phrases buttons shortcuts", nullptr},
    {"My word list", kPageFeatures, "Handy", "names words spelling wrong", nullptr},
    {"Learning mode", kPageFeatures, "Handy", "learn romaji pinyin study replay", TOGGLE(fx.learning)},
    {"Keep a history", kPageFeatures, "Handy", "history log save conversation", TOGGLE(fx.keepHistory)},
    {"Profiles", kPageFeatures, "Handy", "profile save setup load share code", nullptr},
    // Settings
    {"App language", kPageSettings, "App language", "language interface ui translate app", nullptr},
    {"Look", kPageSettings, "App language", "look theme colors colour dark design appearance background picture wallpaper font size", nullptr},
    {"Text size", kPageSettings, "Look", "font size bigger smaller text readable zoom", nullptr},
    {"Colors", kPageSettings, "Look", "theme colour accent midnight forest rose", nullptr},
    {"Background picture", kPageSettings, "Look", "wallpaper photo image background", nullptr},
    {"About", kPageSettings, "App language", "version setup tutorial report problem log costs", nullptr},
    {"Accounts", kPageSettings, "Accounts", "api key elevenlabs deepl claude gemini openai", nullptr},
    {"Spending limit", kPageSettings, "Accounts", "spend money credits tokens limit cost budget", nullptr},
    {"Push to talk", kPageSettings, "Hotkeys", "hotkey key ptt", nullptr},
    {"Updates", kPageSettings, "Hotkeys", "update version new", nullptr},
    {"Check when CIMA Box starts", kPageSettings, "Hotkeys", "update automatically", TOGGLE(checkUpdates)},
    {"Service status", kPageSettings, "Hotkeys", "status outage down working", nullptr},
};
#undef TOGGLE

// Go to a setting by its label, as if picked in "Find a setting".
void findJumpTo(UiModel& m, const char* label) {
    for (const Findable& f : kFindables)
        if (std::strcmp(f.label, label) == 0) {
            m.cfg.page = f.page;
            gJump.want = f.label;
            gJump.framesLeft = 8;
            gJump.hits = 0;
            return;
        }
}

std::string lowerFind(std::string s) {
    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// The search window. Type a few letters: the closest settings show with where they are. Click one
// (or Enter) to go there; on/off settings can be switched right in the list.
void findWindow(UiModel& m) {
    std::string& query = m.findQuery;
    static int selected = 0;
    if (m.findOpen && !ImGui::IsPopupOpen("##find")) {
        ImGui::OpenPopup("##find");
        if (!m.findKeep) query.clear();
        selected = 0;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + em() * 3.2f), ImGuiCond_Always, ImVec2(0.5f, 0.f));
    ImGui::SetNextWindowSize(ImVec2(std::min(em() * 30.f, vp->Size.x * 0.9f), 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 0.9f, em() * 0.9f));
    if (!ImGui::BeginPopup("##find", ImGuiWindowFlags_NoMove)) {
        ImGui::PopStyleVar();
        m.findOpen = false;
        return;
    }
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##findq", T("Find a setting"), &query);
    // Score: the label (as shown, and in English) and the extra words.
    struct Hit { const Findable* f; int score; };
    std::vector<Hit> hits;
    const std::string q = lowerFind(query);
    for (const Findable& f : kFindables) {
        int score = 0;
        if (q.empty()) score = 1;
        else {
            const std::string shownLow = lowerFind(T(f.label)), en = lowerFind(f.label), words = lowerFind(f.words);
            if (shownLow.rfind(q, 0) == 0 || en.rfind(q, 0) == 0) score = 100;
            else if (shownLow.find(q) != std::string::npos || en.find(q) != std::string::npos) score = 60;
            else if (words.find(q) != std::string::npos) score = 30;
            else if (lowerFind(T(f.section)).find(q) != std::string::npos) score = 10;
        }
        if (score > 0) hits.push_back({&f, score});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (hits.size() > 9) hits.resize(9);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) selected = std::min(int(hits.size()) - 1, selected + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) selected = std::max(0, selected - 1);
    selected = std::max(0, std::min(selected, int(hits.size()) - 1));
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    static const char* const kPageNames[] = {"My voice", "Listen", "VR", "Settings", "Text", "Features", "Home"};
    gap(0.3f);
    if (hits.empty()) note("Nothing by that name. Try another word, like \"swear\", \"key\" or \"chatbox\".");
    for (size_t i = 0; i < hits.size(); ++i) {
        const Findable& f = *hits[i].f;
        ImGui::PushID(int(i));
        const float rowH = em() * 2.5f, w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float sh = std::floor(em() * 1.2f), sw = sh * 1.8f;
        const float rowW = f.toggle ? w - sw - em() * 0.8f : w;
        const bool go = ImGui::InvisibleButton("##go", ImVec2(rowW, rowH));
        const bool hot = ImGui::IsItemHovered();
        if (hot) selected = int(i);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (int(i) == selected) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), col(kFieldHover), em() * 0.35f);
        dl->AddText(ImVec2(p.x + em() * 0.5f, p.y + em() * 0.2f), col(kText), T(f.label));
        const std::string where = std::string(T(kPageNames[f.page])) + "  ›  " + T(f.section);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
        dl->AddText(ImVec2(p.x + em() * 0.5f, p.y + em() * 1.3f), col(kMuted), where.c_str());
        ImGui::PopFont();
        if (f.toggle) {
            bool* v = f.toggle(m.cfg);
            const ImVec2 sp(p.x + w - sw - em() * 0.4f, p.y + (rowH - sh) * 0.5f);
            ImGui::SetCursorScreenPos(sp);
            if (ImGui::InvisibleButton("##sw", ImVec2(sw, sh))) *v = !*v;
            drawSwitch(sp, sh, *v, ImGui::IsItemHovered(), ImGui::GetID("##swanim"));
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
            ImGui::Dummy(ImVec2(0, 0));
        }
        if (go || (enter && int(i) == selected)) {
            m.cfg.page = f.page;
            gJump.want = f.label;
            gJump.framesLeft = 8;
            gJump.hits = 0;
            m.findOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { m.findOpen = false; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
    ImGui::PopStyleVar();
}

std::string timingLine(const UiModel& m) {
    if (!m.cfg.enabled) return T("The voice changer is off.");
    if (m.totalMs <= 0) return T("Say something to hear the new voice.");
    char b[320];
    std::snprintf(b, sizeof(b), TF("Last phrase took %.2f s to process"), m.totalMs / 1000.0);
    std::string s = b, parts;
    auto add = [&](const char* name, int ms) {
        if (ms <= 0) return;
        std::snprintf(b, sizeof(b), "%s%s %.2f s", parts.empty() ? "" : ", ", name, ms / 1000.0);  // name: translated
        parts += b;
    };
    add(m.sttLive ? T("transcribe (live)") : T("transcribe"), m.sttMs);
    {
        const std::string tr = m.earlyUsed ? T("translate (early)") : T("translate");
        add(m.translateVia.empty() ? tr.c_str() : (tr + " (" + m.translateVia + ")").c_str(), m.translateMs);
    }
    add(T("voice"), m.voiceMs);
    if (!parts.empty() && (m.sttMs > 0 || m.translateMs > 0)) s += " (" + parts + ")";
    return s + ".";
}

// One clipped line: a muted label, then text. The full text shows on hover.
void statusLine(const char* label, const std::string& text, const ImVec4& color) {
    ImGui::TextColored(kMuted, "%s", T(label));
    ImGui::SameLine();
    ImGui::TextColored(color, "%s", text.c_str());
    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(text.c_str()).x > ImGui::GetContentRegionAvail().x) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(em() * 32.f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void chevron(ImDrawList* dl, ImVec2 c, float s, ImU32 color) {
    dl->AddTriangleFilled(ImVec2(c.x - s * 0.45f, c.y - s), ImVec2(c.x - s * 0.45f, c.y + s), ImVec2(c.x + s * 0.55f, c.y), color);
}

// ---------------------------------------------------------------- Listen: captions for other people

std::string dots() { return std::string(size_t(ImGui::GetTime() * 3.0) % 4, '.'); }

// ---- Models on this PC (translation service "Local")
// Set at the start of every frame, for panels that are only handed the settings.
UiModel* gUi = nullptr;
static UiActions* gAct = nullptr;

static std::string gigabytes(uint64_t bytes) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f GB", double(bytes) / 1e9);
    return b;
}

void localModels(Settings& c) {
    if (!gUi || !gAct) return;
    UiModel& m = *gUi;
    UiActions& act = *gAct;
    const LocalState& ls = m.local;
    ImGui::PushID("localmodels");
    // Said plainly, before anything is downloaded.
    note("Good to know: local models work, but they run on your PC's own power, next to your game. For the best "
         "translations and speed with almost no load on your PC, an API key (Gemini, Claude or OpenAI) is the better choice.", kBusy);
    const std::vector<LocalModel>& all = localCatalog();
    // This PC, and how each model would do on it.
    const PcSpecs& pc = m.pc;
    auto fitOf = [&](const LocalModel& lm) { return rateLocalFit(pc, lm.memoryGb); };
    auto fitColor = [](int fit) { return fit == kFitGood ? kSignal : fit == kFitTight ? kBusy : kDanger; };
    auto fitWord = [](int fit) -> const char* {
        return fit == kFitGood ? "Good fit" : fit == kFitTight ? "Tight" : fit == kFitSlow ? "Slow here" : fit == kFitNo ? "Not for this PC" : "";
    };
    if (pc.known) {
        char line[300];
        if (!pc.gpu.empty())
            std::snprintf(line, sizeof(line), TF("This PC: %s with %.0f GB, and %.0f GB of memory."), pc.gpu.c_str(),
                          double(pc.gpuBytes) / 1073741824.0, double(pc.ramBytes) / 1073741824.0);
        else
            std::snprintf(line, sizeof(line), TF("This PC: no separate graphics card found, and %.0f GB of memory."),
                          double(pc.ramBytes) / 1073741824.0);
        note(line);
    }
    fieldLabel("Model");
    auto stateOf = [&](const std::string& id) -> const LocalModelState* {
        for (const LocalModelState& s : ls.models)
            if (s.id == id) return &s;
        return nullptr;
    };
    // The one showing: the one in use, or the first on the list until something is picked.
    // Until something is picked: the one in use, else the biggest one that's a good fit for this PC
    // (the lightest when none is).
    if (!findLocalModel(m.localView)) {
        m.localView = all[0].id;
        for (const LocalModel& lm : all)
            if (fitOf(lm) == kFitGood) m.localView = lm.id;
        if (findLocalModel(c.localModel)) m.localView = c.localModel;
    }
    auto statusOf = [&](const LocalModel& lm) -> std::string {
        const LocalModelState* s = stateOf(lm.id);
        if (!s) return {};
        if (s->stage == kModelInstalled) return T(c.localModel == lm.id ? "In use" : "Downloaded");
        if (s->stage == kModelDownloading && s->total > 0) return std::to_string(int(100.0 * double(s->done) / double(s->total))) + "%";
        if (s->stage == kModelQueued || s->stage == kModelChecking || s->stage == kModelDownloading) return "...";
        return {};
    };
    auto sizeLine = [&](const LocalModel& lm) {
        const LocalModelState* s = stateOf(lm.id);
        char line[160];
        std::snprintf(line, sizeof(line), TF("%.1f GB download, needs about %d GB of memory"),
                      s && s->total > 0 ? double(s->total) / 1e9 : lm.sizeGb, lm.memoryGb);
        return std::string(line);
    };
    const LocalModel& view = *findLocalModel(m.localView);
    const std::string preview = std::string(T(view.level)) + " \xC2\xB7 " + view.name;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##localmodel", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        const float rowH = ImGui::GetTextLineHeight() * 2.f + em() * 0.45f;
        for (const LocalModel& lm : all) {
            ImGui::PushID(lm.id);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable("##pick", m.localView == lm.id, 0, ImVec2(0, rowH))) {
                m.localView = lm.id;
                m.localDeletePick.clear();
                const LocalModelState* s = stateOf(lm.id);
                if (s && s->stage == kModelInstalled) c.localModel = lm.id;  // a downloaded one: picking it uses it
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool inUse = c.localModel == lm.id && stateOf(lm.id) && stateOf(lm.id)->stage == kModelInstalled;
            dl->AddText(ImVec2(at.x + em() * 0.3f, at.y + em() * 0.1f), col(inUse ? kSignal : kText),
                        (std::string(T(lm.level)) + " \xC2\xB7 " + lm.name).c_str());
            const std::string st = statusOf(lm);
            if (!st.empty())
                dl->AddText(ImVec2(at.x + rowW - ImGui::CalcTextSize(st.c_str()).x - em() * 0.3f, at.y + em() * 0.1f),
                            col(inUse ? kSignal : kMuted), st.c_str());
            const float y2 = at.y + em() * 0.15f + ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(at.x + em() * 0.3f, y2), col(kMuted), sizeLine(lm).c_str());
            const int fit = fitOf(lm);
            if (fit != kFitUnknown) {  // how it would do on this PC
                const char* word = T(fitWord(fit));
                dl->AddText(ImVec2(at.x + rowW - ImGui::CalcTextSize(word).x - em() * 0.3f, y2), col(fitColor(fit)), word);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    {   // The one showing: what it's for, and its one button.
        const LocalModel& lm = view;
        static const LocalModelState none;
        const LocalModelState& s = stateOf(lm.id) ? *stateOf(lm.id) : none;
        ImGui::PushID(lm.id);
        const bool installed = s.stage == kModelInstalled;
        const bool busy = s.stage == kModelQueued || s.stage == kModelDownloading || s.stage == kModelChecking;
        const float full = ImGui::GetContentRegionAvail().x, gapX = ImGui::GetStyle().ItemSpacing.x;
        note(sizeLine(lm) + ".");
        note(lm.blurb);
        const int fit = fitOf(lm);
        if (fit != kFitUnknown)
            note(fit == kFitGood    ? "On this PC: a good fit."
                 : fit == kFitTight ? "On this PC: it fits your graphics card, but it's tight while a game is running. Expect some stutter."
                 : fit == kFitSlow  ? "On this PC: too big for your graphics card. It would run on the processor: slow, and the lines come late."
                                    : "On this PC: not recommended. There isn't enough memory for it.",
                 fitColor(fit));
        if (busy) {
            const float frac = s.total > 0 ? float(double(s.done) / double(s.total)) : 0.f;
            const std::string label = s.stage == kModelChecking ? std::string(T("Checking the download")) + dots()
                                    : s.stage == kModelQueued   ? std::string(T("Waiting for the other download"))
                                                                : gigabytes(s.done) + " / " + gigabytes(s.total);
            const float bw = ImGui::CalcTextSize(T("Stop")).x + ImGui::GetStyle().FramePadding.x * 2.f + em();
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.55f));
            ImGui::ProgressBar(s.stage == kModelChecking ? 1.f : frac, ImVec2(full - bw - gapX, 0), label.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (button("Stop", false, bw)) act.localCancel = lm.id;
        } else if (!installed) {
            if (s.stage == kModelFailed) note(T(s.message), kDanger);
            char dl[120];
            if (s.done > 0) std::snprintf(dl, sizeof(dl), TF("Continue the download (%s left)"), gigabytes(s.total > s.done ? s.total - s.done : 0).c_str());
            else std::snprintf(dl, sizeof(dl), TF("Download (%s)"), gigabytes(s.total > 0 ? s.total : uint64_t(lm.sizeGb * 1e9)).c_str());
            const bool partial = s.done > 0;
            const float half = (full - gapX) * 0.5f;
            if (button(dl, true, partial ? half : full)) act.localDownload = lm.id;
            if (partial) {
                ImGui::SameLine();
                if (button("Delete", false, half)) act.localRemove = lm.id;
            }
        } else if (m.localDeletePick == lm.id) {
            char q[160];
            std::snprintf(q, sizeof(q), TF("Delete %s? It frees %s."), lm.name, gigabytes(s.bytes).c_str());
            note(q, kSignal);
            const float half = (full - gapX) * 0.5f;
            if (button("Delete", true, half)) {
                act.localRemove = lm.id;
                m.localDeletePick.clear();
            }
            ImGui::SameLine();
            if (button("Cancel", false, half)) m.localDeletePick.clear();
        } else {
            if (button("Delete", false, full)) m.localDeletePick = lm.id;
        }
        gap(0.25f);
        ImGui::PopID();
    }
    // A download that just finished and nothing in use yet: it's used (the main loop picks it).
    // What the chosen one is doing right now.
    if (ls.engine == kEngineGetting) {
        const std::string got = ls.engineTotal > 0 ? " (" + std::to_string(int(100.0 * double(ls.engineDone) / double(ls.engineTotal))) + "%)" : std::string();
        note(std::string(T("Getting the engine that runs the models")) + got + dots(), kBusy);
    } else if (ls.engine == kEngineStarting) {
        note(std::string(T("Loading the model")) + dots(), kBusy);
    } else if (ls.engine == kEngineReady) {
        // Which graphics card it's on (any make: NVIDIA, AMD, Intel), so it's plain that it's being used.
        char on[200];
        if (!ls.engineMessage.empty()) note(T(ls.engineMessage), kBusy);
        else if (!ls.device.empty()) { std::snprintf(on, sizeof(on), TF("Ready, on %s."), ls.device.c_str()); note(on, kSignal); }
        else note(T(c.localGpu ? "Ready, on the graphics card." : "Ready, on the processor."), kSignal);
    } else if (ls.engine == kEngineFailed) {
        note(T(ls.engineMessage), kDanger);
    }
    toggleRow("Use the graphics card", &c.localGpu);
    note("Works with NVIDIA, AMD and Intel cards, and is much faster. Turn it off if your game stutters: the model then runs on the processor, slowly.");
    if (ls.diskBytes > 0) {
        char d[120];
        std::snprintf(d, sizeof(d), TF("Local models use %s on this PC. Deleting them all frees everything."), gigabytes(ls.diskBytes).c_str());
        note(d);
    }
    note("Until a model is loaded, and for lines it can't do, your backup translates (DeepL, Microsoft or Free).");
    ImGui::PopID();
}

// Listen's status names a program or device; the sentence around it is translated.
// (T("the default speakers"), T("the default input"): names Listen puts into its status.)
std::string listenStatusText(const std::string& s) {
    auto between = [&](const char* pre, const char* post, std::string& mid) {
        const size_t a = std::strlen(pre), b = std::strlen(post);
        if (s.size() < a + b || s.compare(0, a, pre) != 0 || s.compare(s.size() - b, b, post) != 0) return false;
        mid = s.substr(a, s.size() - a - b);
        return true;
    };
    std::string name;
    char out[512];
    if (between("Listening to everything playing on ", ".", name)) {
        std::snprintf(out, sizeof(out), TF("Listening to everything playing on %s."), T(name));
        return out;
    }
    if (between("Waiting for ", " to start.", name)) {
        std::snprintf(out, sizeof(out), TF("Waiting for %s to start."), name.c_str());
        return out;
    }
    if (s != "Listening to one application needs Windows." && between("Listening to ", ".", name)) {
        std::snprintf(out, sizeof(out), TF("Listening to %s."), T(name));
        return out;
    }
    return T(s);
}

void appCombo(UiModel& m, UiActions& act, std::string& app, const char* id = "##app") {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, app.empty() ? T("Pick an application") : app.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) act.refreshApps = true;  // the list arrives on the next frame
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##appname", T("Or type its file name, like VRChat.exe"), &app);
        for (const AppInfo& a : m.apps) {
            ImGui::PushID(int(a.pid));
            if (ImGui::Selectable(a.exe.c_str(), a.exe == app)) app = a.exe;
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "%s", a.title.c_str());
            ImGui::PopID();
        }
        if (m.apps.empty())
            ImGui::TextColored(kMuted, "%s", T(kAppCaptureSupported ? "No open programs found." : "Not available on this system."));
        ImGui::EndCombo();
    }
}
void appCombo(UiModel& m, UiActions& act) { appCombo(m, act, m.cfg.listen.app); }

void listenSourcePanel(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    ListenSettings& l = c.listen;

    fieldLabel("Listen to");
    static const char* const kSources[] = {"App", "Speakers", "Input"};
    segmented("##source", kSources, 3, &l.source);
    if (l.source == kSrcApp) {
        appCombo(m, act);
        note(kAppCaptureSupported ? "Only this program's sound is captioned. It reconnects by itself when the program restarts."
                                  : "Needs Windows. Pick an input device on this system.");
    } else if (l.source == kSrcSpeakers) {
        deviceCombo("##listenspeakers", m.outputDevices, l.speakers);
        note("Everything playing there is captioned, including the new voice if you hear it on the same device.");
    } else {
        deviceCombo("##listeninput", m.inputDevices, l.input);
        note("For a virtual cable or a second microphone.");
    }

    meter(m.listen.levelDb, m.listenMeterDb, l.thresholdDb, m.listen.hearing, true);
    sliderF("Opens above", &l.thresholdDb, -70.f, 0.f, "%.0f dB");
    note("Set the marker above the game's background sound and below the voices.");

    fieldLabel("Captions appear");
    static const char* const kPaces[] = {"Sooner", "Balanced", "Fuller"};
    segmented("##pace", kPaces, 3, &l.pace);
    note(l.pace == kPaceQuick  ? "Splits at short pauses. Quickest, but half sentences translate worse."
         : l.pace == kPaceFull ? "Waits for whole sentences. Best wording, a few seconds later."
                               : "Splits at natural pauses.");

    gap(0.9f);
    cardTitle("Translation");
    fieldLabel("They speak");
    languageCombo("##listenfrom", l.fromLang, true);
    fieldLabel("Translate into");
    languageCombo("##listento", l.toLang, false);
    translationService(c, true);

    gap(0.2f);
    toggleRow("Tell speakers apart", &l.tellApart);
    toggleRow("Show the original words", &l.showOriginal);
    toggleRow("Skip speech in my language", &l.onlyOtherLanguages);
    toggleRow("Send to VRChat chatbox", &l.chatbox);
    if (l.chatbox) note("Shows above your avatar, so people near you can read it too. Needs OSC turned on in VRChat.");
    sliderF("Caption size", &l.textScale, 0.9f, 2.4f, "%.1fx");
}

// Text with a solid black box behind it (Features > Big, high-contrast captions).
void contrastBox(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(ImVec2(a.x - em() * 0.3f, a.y - em() * 0.1f), ImVec2(b.x + em() * 0.3f, b.y + em() * 0.1f),
                      IM_COL32(0, 0, 0, 235), em() * 0.25f);
}

// Right-click a line from someone: mute that voice for this session.
void muteMenu(UiModel& m, int voiceId) {
    if (voiceId < 0 || !ImGui::BeginPopupContextItem("##mutemenu")) return;
    if (ImGui::MenuItem(T("Mute this voice"))) m.mutedVoices.push_back(voiceId);
    ImGui::EndPopup();
}

// `nameIt`: say whose line it is ("Voice 2"). Off for a line right after another by the same
// voice: the coloured bar already says so, and a list of the same label over and over is noise.
void captionLine(UiModel& m, const CaptionLine& line, float fontSize, bool showOriginal, bool live = false, bool nameIt = true) {
    const bool people = m.cfg.fx.voiceIds;  // voices are followed across captions
    const bool person = line.voiceId >= 0 && people;
    if (person && std::find(m.mutedVoices.begin(), m.mutedVoices.end(), line.voiceId) != m.mutedVoices.end()) return;
    // Every line gets the same indent and a bar, so the text always starts at the same place: a
    // voice's colour when it's known, a quiet bar and no number when it isn't (too short, between
    // two voices, or only one person talking) rather than a number that would be a guess.
    const bool tagged = true;
    const bool unnamed = people ? !person : line.speaker < 0;
    const ImVec4 color = person ? kPeople[line.voiceId % 8] : unnamed ? kLine : kVoices[line.speaker % 4];
    const bool contrast = m.cfg.fx.highContrast;
    if (contrast) fontSize *= 1.2f;
    const float indent = em() * 0.9f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (contrast) dl->ChannelsSplit(2), dl->ChannelsSetCurrent(1);
    if (tagged) ImGui::Indent(indent);
    ImGui::BeginGroup();
    if (tagged && !unnamed && nameIt) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
        ImGui::TextColored(color, TF("Voice %d"), (person ? line.voiceId : line.speaker) + 1);
        ImGui::PopFont();
    }
    ImGui::PushFont(contrast ? gSemibold : nullptr, fontSize);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, live ? kMuted : contrast ? ImVec4(1, 1, 1, 1) : kText);
    ImGui::TextUnformatted(line.text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (showOriginal && !line.original.empty()) {
        ImGui::PushFont(nullptr, std::max(ImGui::GetStyle().FontSizeBase * 0.9f, fontSize * 0.72f));
        ImGui::PushStyleColor(ImGuiCol_Text, contrast ? ImVec4(0.85f, 0.85f, 0.85f, 1) : kMuted);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(line.original.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    if (contrast) {
        dl->ChannelsSetCurrent(0);
        contrastBox(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        dl->ChannelsMerge();
    }
    muteMenu(m, person ? line.voiceId : -1);
    if (tagged) {
        ImGui::Unindent(indent);
        const float bar = std::max(2.f, std::floor(em() * 0.2f));
        // The same voice going on: one unbroken bar down its lines, joined to the line above.
        const float top = nameIt ? start.y + 2 : start.y - ImGui::GetStyle().ItemSpacing.y;
        dl->AddRectFilled(ImVec2(start.x, top), ImVec2(start.x + bar, ImGui::GetItemRectMax().y - 2), col(color), bar * 0.5f);
    }
}

// The feed itself, newest at the bottom. Call inside the scrolling child.
void captionFeed(UiModel& m, float scale, const char* emptyHint) {
    const ListenSettings& l = m.cfg.listen;
    const float base = ImGui::GetStyle().FontSizeBase;
    if (m.captions.empty() && !m.listen.hearing) {
        gap(0.6f);
        note(emptyHint);
        return;
    }
    // The language and time are said once and then only when they change, and a voice is named
    // once per run of its lines: a conversation reads as a conversation, not as a list of labels.
    std::string lastHead;
    int lastVoice = -2;
    for (const Caption& c : m.captions) {
        ImGui::PushID(int(c.id & 0x7FFFFFFF));
        const std::string what = c.screen ? T("On screen") : c.pending ? T("Just heard") : c.language;
        const std::string head = what + "|" + c.time;
        if (head != lastHead) {
            if (!lastHead.empty()) gap(0.35f);
            ImGui::PushFont(nullptr, base * 0.85f);
            ImGui::TextColored(kMuted, "%s", what.c_str());
            const float timeW = ImGui::CalcTextSize(c.time.c_str()).x;
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - timeW));
            ImGui::TextColored(kMuted, "%s", c.time.c_str());
            ImGui::PopFont();
            lastHead = head;
            lastVoice = -2;
        }
        if (c.pending && !c.live) ImGui::TextColored(kBusy, TF("Translating%s"), dots().c_str());
        for (size_t i = 0; i < c.lines.size(); ++i) {
            const CaptionLine& cl = c.lines[i];
            const int voice = m.cfg.fx.voiceIds ? cl.voiceId : (cl.speaker >= 0 ? 1000 + int(c.id % 1000) * 8 + cl.speaker : -1);
            ImGui::PushID(int(i));
            captionLine(m, cl, base * scale, l.showOriginal, c.live, voice != lastVoice);
            ImGui::PopID();
            lastVoice = voice;
        }
        gap(0.45f);
        ImGui::PopID();
    }
    if (m.listen.hearing) ImGui::TextColored(kSignal, TF("Hearing speech%s"), dots().c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f) ImGui::SetScrollHereY(1.0f);  // follow, unless scrolled up
}

void listenStatus(const UiModel& m) {
    if (!m.listen.error.empty()) {
        note(T(m.listen.error), kDanger);
        return;
    }
    std::string s = listenStatusText(m.listen.status);
    if (m.cfg.listen.on && m.listen.lastMs > 0) {
        char b[320];
        std::snprintf(b, sizeof(b), TF(" Last caption took %.1f s."), m.listen.lastMs / 1000.0);
        s += b;
    }
    note(s);
}

void captionsPanel(UiModel& m, UiActions& act, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.05f, em() * 0.9f));
    ImGui::BeginChild("captions", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    const float onlyW = em() * 8.4f, clearW = em() * 4.4f, spacing = ImGui::GetStyle().ItemSpacing.x;
    cardTitle("Captions");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - onlyW - clearW - spacing));
    if (button("Captions only", false, onlyW)) act.setCaptionsOnly = 1;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("A small window that stays on top, to pin in VR or keep over a game"));
    ImGui::SameLine();
    if (button("Clear", false, clearW)) act.clearCaptions = true;
    listenStatus(m);
    gap(0.2f);

    ImGui::BeginChild("feed", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
    captionFeed(m, m.cfg.listen.textScale,
                m.cfg.listen.on ? "Captions show up here when someone speaks."
                                : "Turn Listen on with the switch at the top right to start captioning.");
    ImGui::EndChild();
    ImGui::EndChild();
}

void listenPage(UiModel& m, UiActions& act) {
    const float gapW = em() * 1.6f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float colW = std::floor((avail.x - gapW * 2) / 3.f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    beginPanel("listensource", ImVec2(colW, avail.y), "Source");
    listenSourcePanel(m, act);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + colW + gapW, origin.y));
    captionsPanel(m, act, ImVec2(avail.x - colW - gapW, avail.y));

    chevron(ImGui::GetWindowDrawList(), ImVec2(origin.x + colW + gapW * 0.5f, origin.y + avail.y * 0.5f), em() * 0.42f,
            col(m.listen.hearing || m.listen.busy ? kSignal : kLine));
}

// Nothing but the captions, for a small always-on-top window.
void captionsOnlyView(UiModel& m, UiActions& act) {
    const ListenSettings& l = m.cfg.listen;
    const float backW = em() * 8.4f;
    const ImVec4 color = !l.on ? kMuted : !m.listen.error.empty() ? kDanger : m.listen.hearing ? kSignal : kText;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float rowH = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + em() * 0.35f, p.y + rowH * 0.5f), em() * 0.26f, col(color), 16);
    ImGui::SetCursorScreenPos(ImVec2(p.x + em() * 1.0f, p.y + (rowH - em()) * 0.5f));
    const std::string line = !m.listen.error.empty() ? std::string(T(m.listen.error)) : listenStatusText(m.listen.status);
    ImGui::PushClipRect(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x - backW - em() * 0.5f, p.y + rowH), true);
    ImGui::TextColored(m.listen.error.empty() ? kMuted : kDanger, "%s", line.c_str());
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x - backW, p.y));
    if (button("Show controls", false, backW)) act.setCaptionsOnly = 0;
    gap(0.1f);

    ImGui::BeginChild("feed", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);
    captionFeed(m, l.textScale * 1.15f, l.on ? "Captions show up here when someone speaks." : "Listen is off. Show the controls to turn it on.");
    ImGui::EndChild();
}

// ---------------------------------------------------------------- VR: captions inside the headset

// A colour swatch that opens a picker. Colours are stored as 0xRRGGBB.
bool colorRow(const char* label, uint32_t* c) {
    float rgb[3] = {((*c >> 16) & 0xFF) / 255.f, ((*c >> 8) & 0xFF) / 255.f, (*c & 0xFF) / 255.f};
    ImGui::PushID(label);
    const bool changed = ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(T(label));
    ImGui::PopID();
    if (changed)
        *c = (uint32_t(std::lround(rgb[0] * 255.f)) << 16) | (uint32_t(std::lround(rgb[1] * 255.f)) << 8) |
             uint32_t(std::lround(rgb[2] * 255.f));
    return changed;
}

void vrPlacementPanel(UiModel& m, UiActions& act) {
    VrSettings& v = m.cfg.vr;
    if (!m.vrSupported) note("VR captions need the Windows build with SteamVR.", kDanger);
    note(v.on ? T(m.vr.status) : T("Switch it on at the top right. It only connects while SteamVR is running."));
    if (v.on) {  // when the panel stops showing new lines: start the headset side over
        const bool glow = jumpAnchor("Reset VR captions");
        if (button("Reset VR captions", false, ImGui::CalcTextSize(T("Reset VR captions")).x + em() * 2.4f)) act.resetVr = true;
        if (glow) jumpGlow(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        note("If new captions stop showing in the headset.");
        if (m.vr.recoveries > 0) {
            char b[200];
            std::snprintf(b, sizeof(b), TF("Fixed itself %d times this session (SteamVR had dropped or refused the panel)."), m.vr.recoveries);
            note(b);
        }
    }
    gap(0.3f);

    fieldLabel("Where");
    static const char* const kPlaces[] = {"Head", "Left hand", "Right hand", "Fixed"};
    segmented("##place", kPlaces, 4, &v.placement);
    if (v.placement == kVrHead) {
        note("Moves with your head, always in view.");
    } else if (v.placement == kVrFixed) {
        note("Stays in one spot in the room, like a screen.");
        if (button("Put it in front of me")) act.placeVrHere = true;
    } else {
        note("On the back of your hand: look at your wrist to read it.");
    }

    if (v.placement == kVrHead || v.placement == kVrFixed) {
        sliderF("Distance", &v.distance, 0.4f, 3.f, "%.2f m");
        sliderF("Height", &v.height, -1.f, 0.8f, "%+.2f m");
        sliderF("Sideways", &v.sideways, -1.f, 1.f, "%+.2f m");
        sliderF("Size", &v.width, 0.25f, 2.5f, "%.2f m wide");
        if (v.placement == kVrFixed) note("After changing these, press \"Put it in front of me\" again.");
    } else {
        sliderF("Size", &v.handWidth, 0.1f, 0.5f, "%.2f m wide");
    }

    gap(0.6f);
    cardTitle("Show");
    toggleRow("What I say", &v.showMe);
    toggleRow("Original words under translations", &v.showOriginal);
    sliderF("Hide after", &v.fadeSeconds, 0.f, 30.f, v.fadeSeconds <= 0.f ? "never" : "%.0f s quiet");
    note("Captions come from the Listen page, so turn Listen on too.");
    toggleRow("Show who I'm talking to", &v.showTalkingTo);
    note("A small note at the top of the panel when translations are set for a guy, a girl or a group.");

    subheading("In VR");
    toggleRow("CIMA Box tab in the SteamVR menu", &v.dashboard);
    note("Open the SteamVR menu (the system button) and pick the CIMA Box tab to switch who you're talking to "
         "with your laser pointer.");
}

void vrLookPanel(UiModel& m) {
    VrSettings& v = m.cfg.vr;
    PanelStyle& st = v.style;

    // Preview: the panel exactly as it is drawn in the headset, over a dark "world".
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = m.previewW > 0 ? w * float(m.previewH) / float(m.previewW) : em() * 6.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h), col(rgb(0x2B3A52)), col(rgb(0x2B3A52)), col(rgb(0x1A2230)),
                                col(rgb(0x1A2230)));
    if (m.previewTexture != 0)
        dl->AddImage(ImTextureRef(ImTextureID(m.previewTexture)), p, ImVec2(p.x + w, p.y + h), ImVec2(0, 0), ImVec2(1, 1),
                     IM_COL32(255, 255, 255, int(255 * v.opacity)));
    ImGui::Dummy(ImVec2(w, h));
    note("Preview. It updates as you change things here.");
    gap(0.4f);

    sliderF("Text size", &st.textPx, 24.f, 64.f, "%.0f");
    sliderI("Lines", &st.maxLines, 1, 8, "%d");
    sliderF("Panel", &st.panelOpacity, 0.f, 1.f, st.panelOpacity <= 0.01f ? "none (floating text)" : "%.2f");
    sliderF("Overall opacity", &v.opacity, 0.2f, 1.f, "%.2f");
    toggleRow("Glow", &st.glow);
    gap(0.2f);
    colorRow("Text", &st.textColor);
    colorRow("Accent (edge, glow)", &st.accentColor);
    colorRow("My lines", &st.meColor);
    if (button("Default look")) {
        const PanelStyle d;
        st = d;
        v.opacity = VrSettings().opacity;
    }
}

void vrPage(UiModel& m, UiActions& act) {
    const float gapW = em() * 1.6f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float leftW = std::floor((avail.x - gapW) * 0.42f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    beginPanel("vrplace", ImVec2(leftW, avail.y), "Placement");
    vrPlacementPanel(m, act);
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + leftW + gapW, origin.y));
    beginPanel("vrlook", ImVec2(avail.x - leftW - gapW, avail.y), "Look");
    vrLookPanel(m);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- What's new

// A window over everything: the changes since `since` (after an update), or every version.
void changesWindow(UiModel& m) {
    static bool all = false;
    static std::string since;
    if (m.showChanges && (m.cfg.usageNotice < kUsageNotice || m.showUsageNotice)) return;  // the notice first, then this
    if (m.showChanges) {
        all = m.showChanges == 2;
        since = m.changesSince;
        m.showChanges = 0;
        ImGui::OpenPopup("##changes");
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(vp->WorkSize.x * 0.8f, em() * 38.f), std::min(vp->WorkSize.y * 0.8f, em() * 32.f)),
                             ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.4f, em() * 1.1f));
    if (!ImGui::BeginPopupModal("##changes", nullptr, ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::PopStyleVar();
    char title[200];
    std::snprintf(title, sizeof(title), all ? "%s" : TF("What's new in CIMA Box %s"), all ? T("Version history") : CIMA_VERSION);
    heading(title, 1.3f);
    gap(0.3f);
    const float closeH = ImGui::GetFrameHeight() + em() * 0.6f;
    ImGui::BeginChild("list", ImVec2(0, ImGui::GetContentRegionAvail().y - closeH), 0);
    bool any = false;
    for (const ChangeEntry& e : changelog()) {
        if (!all && compareVersions(e.version, since) <= 0) continue;
        if (!all && compareVersions(e.version, CIMA_VERSION) > 0) continue;
        any = true;
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.05f);
        ImGui::TextUnformatted(e.version.c_str());
        ImGui::PopFont();
        if (!e.date.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "%s", e.date.c_str());
        }
        for (const std::string& item : e.items) {
            const float x = ImGui::GetCursorPosX();
            ImGui::TextColored(kSignal, "%s", "\xE2\x80\xA2");  // a bullet
            ImGui::SameLine(x + em() * 1.0f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(item.c_str());
            ImGui::PopTextWrapPos();
        }
        gap(0.6f);
    }
    if (!any) note("No notes for this version.");
    ImGui::EndChild();
    gap(0.2f);
    if (button("Close", true) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Settings (the gear)

// Two buttons side by side, or one above the other when their names don't fit (long languages).
void buttonPair(const char* left, const char* right, bool& leftClicked, bool& rightClicked) {
    const float sp = ImGui::GetStyle().ItemSpacing.x, half = (ImGui::GetContentRegionAvail().x - sp) * 0.5f;
    const float need = std::max(ImGui::CalcTextSize(T(left)).x, ImGui::CalcTextSize(T(right)).x) + ImGui::GetStyle().FramePadding.x * 2 + em() * 0.4f;
    const bool side = need <= half;
    if (button(left, false, side ? half : -FLT_MIN)) leftClicked = true;
    if (side) ImGui::SameLine();
    if (button(right, false, side ? half : -FLT_MIN)) rightClicked = true;
}

void apiKeyField(const char* label, const char* id, std::string& key, bool show, const char* where) {
    fieldLabel(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint(id, T("Not added yet"), &key, show ? 0 : ImGuiInputTextFlags_Password);
    note(where);
}

void settingsPage(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    const float gapW = em() * 1.6f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float colW = std::floor((avail.x - gapW * 2) / 3.f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    beginPanel("setapp", ImVec2(colW, avail.y), "App language");
    note("The language of these menus. Translations, captions and voices have their own language settings.");
    gap(0.2f);
    ImGui::BeginChild("langs", ImVec2(0, std::min(ImGui::GetContentRegionAvail().y * 0.5f, em() * 15.f)), ImGuiChildFlags_Borders);
    for (const UiLanguage& l : uiLanguages()) {
        ImGui::PushID(l.code.c_str());
        if (ImGui::Selectable(l.name.c_str(), c.uiLanguage == l.code)) {
            c.uiLanguage = l.code;
            setUiLanguage(l.code);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    note("Missing yours? A translation file in the \"lang\" folder next to CIMABox.exe adds it (see the Read Me).");

    subheading("Look");
    LookSettings& look = c.look;
    float pct = look.textScale * 100.f;
    if (sliderF("Text size", &pct, 85.f, 130.f, "%.0f%%")) look.textScale = std::round(pct / 5.f) * 0.05f;
    fieldLabel("Colors");
    static const char* const kPresetNames[] = {"Default", "Midnight", "Forest", "Rose"};
    int preset = look.preset;
    if (segmented("##lookpreset", kPresetNames, kLookCount, &preset)) lookPreset(look, preset);
    gap(0.2f);
    bool changed = false;
    changed |= colorRow("Accent", &look.accent);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
    changed |= colorRow("Text", &look.text);
    changed |= colorRow("Background", &look.background);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
    changed |= colorRow("Panels", &look.panel);
    if (changed) look.preset = -1;  // picked by hand: no preset is selected
    fieldLabel("Background picture");
    if (look.picture.empty()) {
        if (button("Choose a picture")) act.pickPicture = true;
        note("A JPEG or PNG. It's copied into CIMA Box's settings folder, so you can move or delete the original.");
    } else {
        buttonPair("Choose another", "Remove", act.pickPicture, act.clearPicture);
        float show = look.pictureShow * 100.f;
        if (sliderF("Picture strength", &show, 10.f, 80.f, "%.0f%%")) look.pictureShow = show / 100.f;
    }
    if (!m.pictureMsg.empty()) note(m.pictureMsg, kDanger);
    if (button("Back to the original look")) {
        look = LookSettings();  // the original look has no picture either
        act.clearPicture = true;
    }

    subheading("About");
    char b[320];
    std::snprintf(b, sizeof(b), TF("CIMA Box %s"), CIMA_VERSION);
    ImGui::TextUnformatted(b);
    {
        bool a = false, b = false;
        buttonPair("Version history", "Run setup again", a, b);
        if (a) m.showChanges = 2;
        if (b) m.setupStep = 0;
    }
    if (button("About costs and your keys")) m.showUsageNotice = true;
    fieldLabel("Save settings in");
    static const char* const kPlaces[] = {"Settings folder", "Documents", "With the program"};
    int place = m.settingsPlace;
    if (segmented("##settingsplace", kPlaces, 3, &place)) act.setSettingsPlace = place;
    note(m.settingsPlace == kPlaceDocuments ? "A \"CIMA Box Config\" folder in Documents: easy to find and back up."
         : m.settingsPlace == kPlaceProgram ? "Next to CIMABox.exe, so everything stays in one folder (Desktop, a USB stick...)."
                                            : "Your own settings folder, out of the way. Updates and moving the program never touch it.");
    note(m.settingsPath);
    if (!m.settingsError.empty()) note(T(m.settingsError), kDanger);
    buttonPair("Open settings folder", "Open log file", act.openSettingsFolder, act.openLog);
    note("Settings save by themselves. The file holds your API keys: don't share it. The log file has no keys "
         "in it: send it to whoever helps you when something goes wrong.");
    gap(0.2f);
    if (button("Report a problem")) act.reportProblem = true;
    note(m.reportPath.empty() ? "Opens a new report on CIMA Box's GitHub page with the basics filled in, and puts a file on your Desktop to drag into it. No keys, nothing anyone said. Needs a free GitHub account; reports there are public."
                              : "Say what happened on the page that opened, drag \"CIMA Box problem report.txt\" from your Desktop into it, and press Submit.");
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + colW + gapW, origin.y));
    beginPanel("setkeys", ImVec2(colW, avail.y), "Accounts");
    apiKeyField("ElevenLabs API key", "##apikey", c.apiKey, m.showKey, "Required: every voice and caption comes from ElevenLabs.");
    guideLink(m, kGuideEleven);
    ImGui::BeginDisabled(m.loading || c.apiKey.empty());
    if (button(m.loading ? "Loading voices" : "Load voices and models", true)) act.loadCatalog = true;
    ImGui::EndDisabled();
    if (!m.catalogMsg.empty()) note(T(m.catalogMsg));
    apiKeyField("DeepL API key", "##deeplkey", c.deeplKey, m.showKey,
                "Recommended: fast translations, and the backup when the AI is slow or busy.");
    guideLink(m, kGuideDeepL);
    apiKeyField("Claude API key", "##claudekey", c.claudeKey, m.showKey,
                "Recommended (or Gemini or OpenAI): natural translations with dialects and slang.");
    guideLink(m, kGuideClaude);
    apiKeyField("Gemini API key", "##geminikey", c.geminiKey, m.showKey,
                "Recommended (or Claude): the same, free to start.");
    guideLink(m, kGuideGemini);
    apiKeyField("OpenAI API key", "##openaikey", c.openaiKey, m.showKey,
                "Another choice for the same: natural translations with dialects and slang.");
    guideLink(m, kGuideOpenAI);
    apiKeyField("Microsoft Translator key (optional)", "##mskey", c.msKey, m.showKey,
                "A free backup: 2 million characters a month and 100+ languages. Needs a Microsoft Azure account.");
    if (!c.msKey.empty()) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##msregion", T("Region, only if Azure shows one (like westus)"), &c.msRegion);
    }
    gap(0.2f);
    toggleRow("Show keys", &m.showKey);
    subheading("Spending limit");
    if (m.scrollToSpend) {  // came from "Change limit": bring this part into view
        ImGui::SetScrollHereY(0.0f);
        m.scrollToSpend = false;
    }
    toggleRow("Warn me about spending", &c.spendWarn);
    if (c.spendWarn) {
        fieldLabel("ElevenLabs credits per session");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputInt("##spendcredits", &c.spendCredits, 1000, 10000);
        c.spendCredits = std::max(500, c.spendCredits);
        fieldLabel("AI translation per session (US$)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputFloat("##spenddollars", &c.spendDollars, 0.25f, 1.f, "%.2f");
        c.spendDollars = std::max(0.05f, c.spendDollars);
        toggleRow("Stop talking and listening at the limit", &c.spendStop);
        note(c.spendStop ? "When a limit is reached, My voice and Listen switch off until you press OK."
                         : "When a limit is reached, a notice appears at the top. Nothing stops.");
        char b[220];
        if (m.sessionCredits >= 0)
        {
            std::string d = std::to_string(m.sessionCredits), n;  // 20410 -> 20,410
            for (size_t i = 0; i < d.size(); ++i) n += (i > 0 && (d.size() - i) % 3 == 0 ? "," : "") + std::string(1, d[i]);
            std::snprintf(b, sizeof(b), TF("This session so far: %s credits and $%.2f."), n.c_str(), m.sessionDollars);
        }
        else
            std::snprintf(b, sizeof(b), TF("This session so far: $%.2f. (ElevenLabs credits can't be counted: the key can't read usage.)"),
                          m.sessionDollars);
        note(b);
    }
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + (colW + gapW) * 2, origin.y));
    beginPanel("setkeys2", ImVec2(avail.x - (colW + gapW) * 2, avail.y), "Hotkeys");
    fieldLabel("Push to talk");
    keyBinder(m, 0, c.pttKey);
    note("Used when My voice > Microphone is set to push to talk.");
    fieldLabel("Next \"Talking to\"");
    keyBinder(m, 1, c.talkKey);
    note(kGlobalHotkey ? "Each press steps through Auto, Don't know, A guy, A girl and A group. Works while a game has focus."
                       : "Each press steps through Auto, Don't know, A guy, A girl and A group.");
    subheading("You");
    fieldLabel("I am");
    static const char* const kMe[] = {"Not set", "A man", "A woman", "Nonbinary"};
    segmented("##mygender2", kMe, 4, &c.myGender);
    fieldLabel("Talking to");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##talkingto2", T(kTalkingLabels[c.talkingTo]))) {
        for (int i = 0; i < kTalkCount; ++i)
            if (ImGui::Selectable(T(kTalkingLabels[i]), c.talkingTo == i)) c.talkingTo = i;
        ImGui::EndCombo();
    }
    note("Used by Claude, Gemini and OpenAI when translating, for languages whose words change with gender.");
    subheading("Updates");
    if (!m.updatesConfigured) {
        note("This copy can't update itself. Download CIMA Box from its GitHub page to get automatic updates.");
    } else {
        const UpdateState& u = m.update;
        toggleRow("Check when CIMA Box starts", &c.checkUpdates);
        const bool working = u.stage == kUpdateChecking || u.stage == kUpdateDownloading || u.stage == kUpdateReady;
        if (u.stage == kUpdateAvailable) {
            char line[320];
            std::snprintf(line, sizeof(line), TF("CIMA Box %s is available."), u.latest.c_str());
            note(line, kSignal);
            if (!u.notes.empty()) {
                ImGui::BeginChild("notes", ImVec2(0, em() * 5.5f), ImGuiChildFlags_Borders);
                note(u.notes);
                ImGui::EndChild();
            }
            if (button("Update and restart", true)) act.installUpdate = true;
            note("Downloads the new version, checks its signature and restarts. Your settings and keys stay.");
        } else if (u.stage != kUpdateIdle) {
            note(std::string(T(u.message)) + (working ? dots() : std::string()), u.stage == kUpdateFailed ? kDanger : kMuted);
        }
        ImGui::BeginDisabled(working);
        if (u.stage != kUpdateAvailable && button("Check for updates")) act.checkUpdates = true;
        ImGui::EndDisabled();

        // Going back: in case a new version breaks something for you.
        gap(0.2f);
        if (!m.olderOpen) {
            ImGui::BeginDisabled(working);
            if (button("Go back to an earlier version")) {
                m.olderOpen = true;
                m.olderPick.clear();
                act.listOlder = true;
            }
            ImGui::EndDisabled();
        } else {
            fieldLabel("Go back to an earlier version");
            note("Only versions that can update themselves are listed, so you can always come back.");
            if (u.olderStage == kOlderLoading || u.olderStage == kOlderNone) {
                note(std::string(T("Looking for earlier versions")) + dots());
            } else if (u.olderStage == kOlderFailed) {
                note(T(u.olderMessage), kDanger);
                if (button("Try again")) act.listOlder = true;
            } else if (u.older.empty()) {
                note("No earlier versions to go back to.");
            } else if (!m.olderPick.empty()) {
                char line[200];
                std::snprintf(line, sizeof(line), TF("Go back to %s and restart? Your settings and keys stay."), m.olderPick.c_str());
                note(line, kSignal);
                ImGui::BeginDisabled(working);
                const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                if (button("Go back", true, half)) act.installOlder = m.olderPick;
                ImGui::SameLine();
                if (button("Cancel", false, half)) m.olderPick.clear();
                ImGui::EndDisabled();
            } else {
                size_t shown = 0;
                for (const ReleaseInfo& r : u.older) {
                    if (++shown > 6) break;  // the six most recent are plenty
                    ImGui::PushID(r.version.c_str());
                    std::string summary = r.notes;
                    // The first real line of its notes, without the markdown dash.
                    {
                        size_t a = 0;
                        while (a < summary.size()) {
                            size_t b = summary.find('\n', a);
                            if (b == std::string::npos) b = summary.size();
                            std::string ln = summary.substr(a, b - a);
                            while (!ln.empty() && (ln[0] == '-' || ln[0] == '*' || ln[0] == '#' || ln[0] == ' ')) ln.erase(ln.begin());
                            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
                            const bool heading = ln == r.version || ln == "v" + r.version || ln == "CIMA Box " + r.version;
                            if (!ln.empty() && !heading) { summary = ln; break; }
                            a = b + 1;
                        }
                        if (a >= summary.size()) summary.clear();
                        if (summary.size() > 90) summary = summary.substr(0, 87) + "...";
                    }
                    const float bw = ImGui::CalcTextSize(T("Go back")).x + ImGui::GetStyle().FramePadding.x * 2.f;
                    const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(("CIMA Box " + r.version).c_str());
                    ImGui::SameLine(rowRight - bw);
                    ImGui::BeginDisabled(working);
                    if (button("Go back", false, bw)) m.olderPick = r.version;
                    ImGui::EndDisabled();
                    if (!r.date.empty()) ImGui::TextColored(kMuted, "%s", r.date.c_str());
                    if (!summary.empty()) {
                        ImGui::PushTextWrapPos(0.f);
                        ImGui::TextColored(kMuted, "%s", summary.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    gap(0.15f);
                    ImGui::Separator();
                    gap(0.15f);
                    ImGui::PopID();
                }
            }
            if (m.olderPick.empty() && button("Hide")) m.olderOpen = false;
        }
    }
    subheading("Service status");
    note("Checked every few minutes. If something breaks while a service shows problems, it's on their side.");
    gap(0.15f);
    serviceRows(m, act);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Text: type instead of talking

void textVoicePanel(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    static const char* const kModes[] = {"As typed", "Translate"};
    int mode = c.textTranslate ? 1 : 0;
    if (segmented("##textmode", kModes, 2, &mode)) c.textTranslate = mode == 1;
    note(c.textTranslate ? "Translates what you type, then the voice reads it." : "The voice reads what you type.");

    fieldLabel("Voice");
    voiceCombo(m);
    if (m.voices.empty()) {
        if (c.apiKey.empty()) {
            note("Add your ElevenLabs API key in Settings (bottom left) to see your voices.");
        } else {
            ImGui::BeginDisabled(m.loading);
            if (button(m.loading ? "Loading voices" : "Load voices and models", true)) act.loadCatalog = true;
            ImGui::EndDisabled();
        }
    }
    fieldLabel("Model");
    modelCombo(m.models, false, c.ttsModel);
    if (c.textTranslate) {
        fieldLabel("I type in");
        languageCombo("##typedin", c.spokenLang, true);
        fieldLabel("Translate to");
        languageCombo("##texttarget", c.targetLang, false, c.ttsModel);
        dialectPicker(c);
        translationService(c, false);
        if (isAiService(c.translateService)) {
            subheading("Gender");
            genderPicker(m);
        }
    }
    gap(0.3f);
    toggleRow("Say it out loud", &c.textSpeak);
    note(c.textSpeak ? "Plays through the output picked on My voice, so others hear it in the game or call."
                     : "Off: only shows in the conversation and the chatbox.");
    toggleRow("Show in VRChat chatbox", &c.chatbox);
    note("These settings are shared with My voice.");
}

// One line of the conversation. Yours get an amber bar, other people's the color of their voice.
void chatLine(UiModel& m, UiActions& act, const ChatLine& l, float fontSize, bool showOriginal) {
    const bool person = !l.me && l.voiceId >= 0 && m.cfg.fx.voiceIds;
    const ImVec4 color = l.me ? kSignal : person ? kPeople[l.voiceId % 8] : l.speaker >= 0 ? kVoices[l.speaker % 4] : kLine;
    const bool contrast = m.cfg.fx.highContrast && !l.me;
    if (contrast) fontSize *= 1.2f;
    const float indent = em() * 0.9f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (contrast) dl->ChannelsSplit(2), dl->ChannelsSetCurrent(1);
    ImGui::Indent(indent);
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
    if (l.me) ImGui::TextColored(kSignal, "%s", T(l.typed ? "You (typed)" : "You"));
    else if (l.screen) ImGui::TextColored(kMuted, "%s", T("On screen"));
    else if (person || l.speaker >= 0) ImGui::TextColored(color, TF("Voice %d"), (person ? l.voiceId : l.speaker) + 1);
    else ImGui::TextColored(kMuted, "%s", l.language.empty() ? T("Them") : l.language.c_str());
    ImGui::PopFont();
    ImGui::PushFont(contrast ? gSemibold : nullptr, fontSize);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, contrast ? ImVec4(1, 1, 1, 1) : kText);
    ImGui::TextUnformatted(l.text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (showOriginal && !l.original.empty()) {
        ImGui::PushFont(nullptr, std::max(ImGui::GetStyle().FontSizeBase * 0.9f, fontSize * 0.72f));
        ImGui::PushStyleColor(ImGuiCol_Text, contrast ? ImVec4(0.85f, 0.85f, 0.85f, 1) : kMuted);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(l.original.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    if (m.cfg.fx.learning && !l.roman.empty()) {  // how to read it
        ImGui::PushStyleColor(ImGuiCol_Text, kBusy);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(l.roman.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    if (contrast) {
        dl->ChannelsSetCurrent(0);
        contrastBox(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        dl->ChannelsMerge();
    }
    muteMenu(m, person ? l.voiceId : -1);
    if (m.cfg.fx.learning) {  // learning mode: read it, hear it again, keep it
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
        const std::string foreign = l.me ? l.text : (l.original.empty() ? l.text : l.original);
        // Only scripts that aren't Latin letters need a reading (Greek, Cyrillic and up).
        bool nonLatin = false;
        for (unsigned char ch : foreign) nonLatin = nonLatin || ch >= 0xCE;
        if (nonLatin && l.roman.empty() && ImGui::SmallButton(T("How to read it"))) act.romanize = l.key;
        if (nonLatin && l.roman.empty()) ImGui::SameLine();
        if (ImGui::SmallButton(T("Replay"))) {  // the words in their own language
            act.replayText = foreign;
            act.replayLang = l.me ? m.cfg.targetLang : !l.original.empty() ? l.code : m.cfg.listen.toLang;
        }
        ImGui::SameLine();
        const std::string saved = foreign + (l.text != foreign ? "  =  " + l.text : std::string()) + (l.roman.empty() ? std::string() : "  (" + l.roman + ")");
        const bool have = std::find(m.cfg.fx.savedLines.begin(), m.cfg.fx.savedLines.end(), saved) != m.cfg.fx.savedLines.end();
        ImGui::BeginDisabled(have);
        if (ImGui::SmallButton(T(have ? "Saved" : "Save"))) m.cfg.fx.savedLines.push_back(saved);
        ImGui::EndDisabled();
        ImGui::PopFont();
    }
    ImGui::Unindent(indent);
    const float bar = std::max(2.f, std::floor(em() * 0.2f));
    dl->AddRectFilled(ImVec2(start.x, start.y + 2), ImVec2(start.x + bar, ImGui::GetItemRectMax().y - 2), col(color), bar * 0.5f);
}

// "About to say" with a Cancel button, while the voice waits (Features > Cancel before it's spoken).
void pendingBar(UiModel& m, UiActions& act) {
    const float cancelW = em() * 6.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.12f)), ImGui::GetStyle().FrameRounding);
    // how long is left, as a thin bar along the bottom
    const float total = std::max(0.5f, m.cfg.fx.previewMs / 1000.f);
    dl->AddRectFilled(ImVec2(p.x, p.y + h - 2), ImVec2(p.x + w * clamp01(m.pendingLeft / total), p.y + h), col(kSignal));
    char label[64];
    std::snprintf(label, sizeof(label), "%s ", T("About to say:"));
    ImGui::SetCursorScreenPos(ImVec2(p.x + em() * 0.5f, p.y + (h - em()) * 0.5f));
    ImGui::PushClipRect(p, ImVec2(p.x + w - cancelW - em() * 0.6f, p.y + h), true);
    ImGui::TextColored(kSignal, "%s", label);
    ImGui::SameLine(0, 0);
    ImGui::TextUnformatted(m.pendingSay.c_str());
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - cancelW, p.y));
    if (button("Cancel##pending", true, cancelW)) act.cancelPending = true;
    gap(0.3f);
}

void conversationPanel(UiModel& m, UiActions& act, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.05f, em() * 0.9f));
    ImGui::BeginChild("conversation", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ListenSettings& l = m.cfg.listen;

    const float listenW = em() * 9.f;
    cardTitle("Conversation");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - listenW));
    if (button(l.on ? "Stop listening" : "Start listening", !l.on, listenW)) l.on = !l.on;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", T("Other people's words come from Listen. Pick what to listen to on the Listen page."));
    listenStatus(m);
    gap(0.2f);

    const float rowH = ImGui::GetFrameHeight();
    const bool problem = !m.lastError.empty();
    const ServiceStatus* bad = problem ? troubleInUse(m) : nullptr;
    const float footH = rowH + ImGui::GetStyle().ItemSpacing.y +
                        ImGui::GetTextLineHeightWithSpacing() * ((problem ? 1.f : 0.f) + (bad ? 1.f : 0.f)) +
                        (m.cfg.fx.quickPhrases.empty() ? 0.f : ImGui::GetFrameHeightWithSpacing()) +
                        (m.pendingSay.empty() ? 0.f : ImGui::GetFrameHeightWithSpacing() + em() * 0.3f);
    ImGui::BeginChild("chatfeed", ImVec2(0, std::max(em() * 4.f, ImGui::GetContentRegionAvail().y - footH)), 0,
                      ImGuiWindowFlags_NoBackground);
    const float fontSize = ImGui::GetStyle().FontSizeBase * l.textScale;
    if (m.chat.empty() && !m.listen.hearing) {
        gap(0.6f);
        note(l.on ? "Type below to talk. What other people say shows up here, translated."
                  : "Type below to talk. Turn on Listen to see what other people say here, translated.");
    }
    for (size_t i = 0; i < m.chat.size(); ++i) {
        ImGui::PushID(int(i));
        chatLine(m, act, m.chat[i], fontSize, l.showOriginal);
        gap(0.45f);
        ImGui::PopID();
    }
    bool pending = false;
    for (const Caption& c : m.captions) {
        pending = pending || c.pending;
        if (c.pending && c.live)  // words while they're still talking
            for (const CaptionLine& cl : c.lines) {
                ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(cl.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
    }
    if (l.on && m.listen.hearing) ImGui::TextColored(kSignal, TF("Hearing speech%s"), dots().c_str());
    else if (l.on && pending) ImGui::TextColored(kBusy, TF("Translating%s"), dots().c_str());
    if (m.busy || m.queued > 0) ImGui::TextColored(kMuted, TF("Sending%s"), dots().c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f) ImGui::SetScrollHereY(1.0f);  // follow, unless scrolled up
    ImGui::EndChild();

    if (problem) statusLine("Problem", T(m.lastError), kDanger);
    if (bad) {
        char b[400];
        std::snprintf(b, sizeof(b), TF("%s's status page reports: %s"), bad->name.c_str(), bad->detail.c_str());
        statusLine("Not the app", b, healthColor(bad->health));
    }
    if (!m.pendingSay.empty()) pendingBar(m, act);
    if (!m.cfg.fx.quickPhrases.empty()) {  // quick phrases: one click to say
        const float maxX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (size_t i = 0; i < m.cfg.fx.quickPhrases.size(); ++i) {
            const std::string& q = m.cfg.fx.quickPhrases[i];
            const float w = ImGui::CalcTextSize(q.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i > 0) {
                if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < maxX) ImGui::SameLine();
                else break;  // one row only
            }
            ImGui::PushID(int(i));
            if (ImGui::SmallButton(q.c_str())) act.sendText = q;
            ImGui::PopID();
        }
    }
    const float sendW = em() * 5.f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - sendW - ImGui::GetStyle().ItemSpacing.x);
    const bool enter = ImGui::InputTextWithHint("##type", T("Type a message, then press Enter"), &m.textDraft,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (enter) ImGui::SetKeyboardFocusHere(-1);  // keep typing
    ImGui::SameLine();
    const bool clicked = button("Send", true, sendW);
    if ((enter || clicked) && m.textDraft.find_first_not_of(" \t\r\n") != std::string::npos) {
        act.sendText = m.textDraft;
        m.textDraft.clear();
    }
    ImGui::EndChild();
}

void textPage(UiModel& m, UiActions& act) {
    const float gapW = em() * 1.6f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float colW = std::floor((avail.x - gapW * 2) / 3.f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    beginPanel("textvoice", ImVec2(colW, avail.y), "Voice");
    textVoicePanel(m, act);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + colW + gapW, origin.y));
    conversationPanel(m, act, ImVec2(avail.x - colW - gapW, avail.y));

    chevron(ImGui::GetWindowDrawList(), ImVec2(origin.x + colW + gapW * 0.5f, origin.y + avail.y * 0.5f), em() * 0.42f,
            col(m.busy ? kSignal : kLine));
}

// ---------------------------------------------------------------- My voice: the three stages

// ---------------------------------------------------------------- setup walkthrough
// Language first (before a single other word), then one question per screen.

enum SetupScreen { kStepLanguage = 0, kStepWelcome, kStepKey, kStepDeepL, kStepAi, kStepUse, kStepVoice, kStepRoute, kStepRead, kStepDone, kStepCount };

bool setupShows(const UiModel& m, int step) {
    const bool speaks = (m.setupUse & (kUseVoice | kUseText)) != 0;
    if (step == kStepVoice || step == kStepRoute) return speaks;
    if (step == kStepRead) return (m.setupUse & kUseRead) != 0;
    return true;
}

int setupNext(const UiModel& m, int step, int dir) {
    for (int s = step + dir; s >= 0 && s < kStepCount; s += dir)
        if (setupShows(m, s)) return s;
    return step;
}

enum SetupIcon { kIconMic, kIconKeys, kIconBubbles, kIconHeadphones, kIconGame, kIconSliders, kIconSpark, kIconBolt };

// Small line drawings for the choice cards.
void setupIcon(ImDrawList* dl, int icon, ImVec2 c, float s, ImU32 color) {
    const float t = std::max(1.5f, s * 0.085f);
    switch (icon) {
    case kIconMic:
        dl->AddRect(ImVec2(c.x - s * 0.17f, c.y - s * 0.46f), ImVec2(c.x + s * 0.17f, c.y + s * 0.08f), color, s * 0.17f, 0, t);
        dl->PathArcTo(ImVec2(c.x, c.y - s * 0.06f), s * 0.3f, 0.f, 3.14159f, 16);
        dl->PathStroke(color, 0, t);
        dl->AddLine(ImVec2(c.x, c.y + s * 0.24f), ImVec2(c.x, c.y + s * 0.42f), color, t);
        dl->AddLine(ImVec2(c.x - s * 0.16f, c.y + s * 0.42f), ImVec2(c.x + s * 0.16f, c.y + s * 0.42f), color, t);
        break;
    case kIconKeys: {
        dl->AddRect(ImVec2(c.x - s * 0.46f, c.y - s * 0.28f), ImVec2(c.x + s * 0.46f, c.y + s * 0.28f), color, s * 0.08f, 0, t);
        for (int r = 0; r < 2; ++r)
            for (int k = 0; k < 5; ++k)
                dl->AddCircleFilled(ImVec2(c.x - s * 0.3f + k * s * 0.15f, c.y - s * 0.1f + r * s * 0.15f), t * 0.7f, color, 8);
        dl->AddLine(ImVec2(c.x - s * 0.2f, c.y + s * 0.16f), ImVec2(c.x + s * 0.2f, c.y + s * 0.16f), color, t);
        break;
    }
    case kIconBubbles:
        dl->AddRect(ImVec2(c.x - s * 0.46f, c.y - s * 0.4f), ImVec2(c.x + s * 0.18f, c.y - s * 0.02f), color, s * 0.1f, 0, t);
        dl->AddRect(ImVec2(c.x - s * 0.14f, c.y + s * 0.06f), ImVec2(c.x + s * 0.46f, c.y + s * 0.4f), color, s * 0.1f, 0, t);
        dl->AddLine(ImVec2(c.x - s * 0.33f, c.y - s * 0.21f), ImVec2(c.x + s * 0.05f, c.y - s * 0.21f), color, t);
        dl->AddLine(ImVec2(c.x - s * 0.01f, c.y + s * 0.23f), ImVec2(c.x + s * 0.33f, c.y + s * 0.23f), color, t);
        break;
    case kIconHeadphones:
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.05f), s * 0.38f, 3.14159f, 6.28318f, 20);
        dl->PathStroke(color, 0, t);
        dl->AddRectFilled(ImVec2(c.x - s * 0.44f, c.y + s * 0.02f), ImVec2(c.x - s * 0.24f, c.y + s * 0.4f), color, s * 0.06f);
        dl->AddRectFilled(ImVec2(c.x + s * 0.24f, c.y + s * 0.02f), ImVec2(c.x + s * 0.44f, c.y + s * 0.4f), color, s * 0.06f);
        break;
    case kIconGame:
        dl->AddRect(ImVec2(c.x - s * 0.46f, c.y - s * 0.24f), ImVec2(c.x + s * 0.46f, c.y + s * 0.26f), color, s * 0.24f, 0, t);
        dl->AddLine(ImVec2(c.x - s * 0.3f, c.y), ImVec2(c.x - s * 0.1f, c.y), color, t);
        dl->AddLine(ImVec2(c.x - s * 0.2f, c.y - s * 0.1f), ImVec2(c.x - s * 0.2f, c.y + s * 0.1f), color, t);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.18f, c.y - s * 0.03f), t * 0.9f, color, 10);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.29f, c.y + s * 0.07f), t * 0.9f, color, 10);
        break;
    case kIconSpark: {  // a four-pointed star
        const float o = s * 0.46f, i = s * 0.12f;
        const ImVec2 pts[8] = {{c.x, c.y - o}, {c.x + i, c.y - i}, {c.x + o, c.y}, {c.x + i, c.y + i},
                               {c.x, c.y + o}, {c.x - i, c.y + i}, {c.x - o, c.y}, {c.x - i, c.y - i}};
        dl->AddPolyline(pts, 8, color, ImDrawFlags_Closed, t);
        break;
    }
    case kIconBolt: {  // a lightning bolt
        const ImVec2 pts[6] = {{c.x + s * 0.08f, c.y - s * 0.46f}, {c.x - s * 0.26f, c.y + s * 0.06f},
                               {c.x - s * 0.01f, c.y + s * 0.06f}, {c.x - s * 0.08f, c.y + s * 0.46f},
                               {c.x + s * 0.26f, c.y - s * 0.06f}, {c.x + s * 0.01f, c.y - s * 0.06f}};
        dl->AddPolyline(pts, 6, color, ImDrawFlags_Closed, t);
        break;
    }
    default:  // sliders
        for (int i = 0; i < 3; ++i) {
            const float y = c.y - s * 0.3f + i * s * 0.3f, kx = c.x + (i == 1 ? s * 0.18f : -s * 0.14f);
            dl->AddLine(ImVec2(c.x - s * 0.44f, y), ImVec2(c.x + s * 0.44f, y), color, t);
            dl->AddCircleFilled(ImVec2(kx, y), s * 0.09f, color, 12);
        }
    }
}

// A big selectable card: icon, title, one line under it. Returns true when clicked.
bool setupCard(const char* id, int icon, const char* title, const char* desc, bool selected, bool multi) {
    const float w = ImGui::GetContentRegionAvail().x, pad = em() * 1.0f, iconBox = em() * 2.6f;
    const float textW = w - pad * 3 - iconBox - em() * 1.6f;
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.08f);
    const ImVec2 ts = ImGui::CalcTextSize(T(title), nullptr, false, textW);
    ImGui::PopFont();
    const ImVec2 ds = ImGui::CalcTextSize(T(desc), nullptr, false, textW);
    const float h = std::max(iconBox + pad * 1.2f, ts.y + ds.y + pad * 2.f + em() * 0.15f);
    const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = em() * 0.55f;
    dl->AddRectFilled(a, b, col(selected ? ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.10f) : hot ? kFieldHover : kPanel), r);
    dl->AddRect(a, b, col(selected ? kSignal : hot ? kFieldActive : kLine), r, 0, selected ? 2.f : 1.f);
    const ImVec2 ic(a.x + pad + iconBox * 0.5f, a.y + h * 0.5f);
    dl->AddCircleFilled(ic, iconBox * 0.5f, col(selected ? ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.18f) : kField), 32);
    setupIcon(dl, icon, ic, iconBox * 0.52f, col(selected ? kSignal : kText));
    const float tx = a.x + pad * 2 + iconBox, ty = a.y + (h - ts.y - ds.y - em() * 0.15f) * 0.5f;
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.08f);
    dl->AddText(nullptr, 0.f, ImVec2(tx, ty), col(kText), T(title), nullptr, textW);
    ImGui::PopFont();
    dl->AddText(nullptr, 0.f, ImVec2(tx, ty + ts.y + em() * 0.15f), col(kMuted), T(desc), nullptr, textW);
    // The tick (multi) or dot (single) on the right.
    const ImVec2 mc(b.x - pad - em() * 0.55f, a.y + h * 0.5f);
    const float mr = em() * 0.55f;
    if (multi) {
        dl->AddRect(ImVec2(mc.x - mr, mc.y - mr), ImVec2(mc.x + mr, mc.y + mr), col(selected ? kSignal : kLine), em() * 0.2f, 0, 1.5f);
        if (selected) {
            dl->AddRectFilled(ImVec2(mc.x - mr, mc.y - mr), ImVec2(mc.x + mr, mc.y + mr), col(kSignal), em() * 0.2f);
            dl->AddLine(ImVec2(mc.x - mr * 0.5f, mc.y), ImVec2(mc.x - mr * 0.1f, mc.y + mr * 0.42f), col(kOnSignal), 2.f);
            dl->AddLine(ImVec2(mc.x - mr * 0.1f, mc.y + mr * 0.42f), ImVec2(mc.x + mr * 0.55f, mc.y - mr * 0.42f), col(kOnSignal), 2.f);
        }
    } else {
        dl->AddCircle(mc, mr, col(selected ? kSignal : kLine), 24, 1.5f);
        if (selected) dl->AddCircleFilled(mc, mr * 0.5f, col(kSignal), 24);
    }
    gap(0.45f);
    return clicked;
}

void setupTitle(const char* title, const char* sub) {
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.85f);
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(T(title));
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (sub && *sub) {
        gap(0.2f);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.08f);
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(T(sub));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    gap(1.0f);
}

// A step in a short list: a small amber number, then the text. (These really are in order.)
void setupStepLine(int n, const char* text) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = em() * 0.62f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r, col(ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.16f)), 20);
    char num[4];
    std::snprintf(num, sizeof(num), "%d", n);
    const ImVec2 ns = ImGui::CalcTextSize(num);
    dl->AddText(ImVec2(p.x + r - ns.x * 0.5f, p.y), col(kSignal), num);
    ImGui::SetCursorScreenPos(ImVec2(p.x + r * 2 + em() * 0.6f, p.y));
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(T(text));
    ImGui::PopTextWrapPos();
    gap(0.25f);
}

// ---------------------------------------------------------------- key guides ("how do I get this?")

struct KeyGuideText {
    const char* name;
    const char* page;       // opens with "Open the page"
    const char* pageShown;  // the same, as shown
    const char* extraUrl;   // a second button (sign up, buy credits), or null
    const char* extraLabel;
    const char* steps[5];
    int pics[5];            // -1 = none
    int picAfter[5];        // each picture goes right after this step
};

const KeyGuideText kKeyGuides[kGuideCount] = {
    {"ElevenLabs", "https://elevenlabs.io/app/settings/api-keys", "elevenlabs.io/app/settings/api-keys", nullptr, nullptr,
     {"Sign in (or make a free account), then click Developers at the bottom of the left menu.",
      "Open the API Keys tab.",
      "Click Create Key.",
      "Turn Restrict Key off, so CIMA Box can use everything it needs.",
      "Click Create Key, then copy the key it shows. It's shown only once."},
     {kPicEleven1, kPicEleven2, kPicEleven3, -1, -1}, {2, 3, 5, 0, 0}},
    {"DeepL", "https://www.deepl.com/your-account/keys", "deepl.com/your-account/keys", "https://www.deepl.com/pro-api",
     "Sign up (free)",
     {"Sign up for the free DeepL API plan. Already have a DeepL account for the translator? Log out and make a new "
      "one for the API. It may ask for a card to check you're a real person; the free plan doesn't charge it.",
      "Open your account and the API Keys & Limits tab.",
      "Click Create key, if there's no key yet.",
      "Click the copy button next to the key.",
      nullptr},
     {kPicDeepL, -1, -1, -1, -1}, {4, 0, 0, 0, 0}},
    {"Google Gemini", "https://aistudio.google.com/apikey", "aistudio.google.com/apikey", nullptr, nullptr,
     {"Sign in with your Google account (you need to be 18 or older) and accept the terms.",
      "Click Create API key.",
      "Click the copy button next to your new key.",
      nullptr, nullptr},
     {kPicGemini, -1, -1, -1, -1}, {3, 0, 0, 0, 0}},
    {"Claude", "https://platform.claude.com/settings/keys", "platform.claude.com/settings/keys",
     "https://platform.claude.com/settings/billing", "Buy credits",
     {"Sign in (or make an account), then buy a few dollars of credits under Billing. Claude doesn't work without them.",
      "Click Keys in the left menu.",
      "Click Create key and give it a name.",
      "Copy the key it shows. It's shown only once.",
      nullptr},
     {kPicClaude, -1, -1, -1, -1}, {3, 0, 0, 0, 0}},
    {"OpenAI", "https://platform.openai.com/api-keys", "platform.openai.com/api-keys",
     "https://platform.openai.com/settings/organization/billing/overview", "Add credits",
     {"Sign in (or make an account). When it asks you to choose a plan, Free is fine to try. Click Continue.",
      "Name the key, for example CIMA Box, and click Generate API Key.",
      "Click Copy next to the key. It's shown only once.",
      "If Check key says there's no credit, click Add credits. The Build plan starts at $5, which lasts a long time.",
      "Need the key again, or a new one? Click API Keys in the left menu, then Create new secret key at the top right."},
     {kPicOpenAI1, kPicOpenAI2, kPicOpenAI3, kPicOpenAI4, kPicOpenAI5}, {1, 2, 3, 4, 5}},
};

// A small rounded label, like "Required" or "Highly recommended".
void chip(const char* text, const ImVec4& c) {
    const char* t = T(text);
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 0.85f);
    const ImVec2 ts = ImGui::CalcTextSize(t), p = ImGui::GetCursorScreenPos();
    const ImVec2 pad(em() * 0.6f, em() * 0.22f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + pad.x * 2, p.y + ts.y + pad.y * 2), col(ImVec4(c.x, c.y, c.z, 0.16f)), ts.y);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), col(c), t);
    ImGui::Dummy(ImVec2(ts.x + pad.x * 2, ts.y + pad.y * 2));
    ImGui::PopFont();
    gap(0.35f);
}

// One guide picture, as wide as there is room (and not too tall). Click it to see it big.
void guidePicture(UiModel& m, int pic) {
    if (pic < 0 || pic >= kPicCount) return;
    const GuidePic& g = m.guidePics[pic];
    if (!g.texture || g.width <= 0) return;
    float w = std::min(ImGui::GetContentRegionAvail().x, float(g.width));
    float h = w * float(g.height) / float(g.width);
    const float maxH = em() * 30.f;
    if (h > maxH) { h = maxH; w = h * float(g.width) / float(g.height); }
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::PushID(pic);
    if (ImGui::InvisibleButton("##pic", ImVec2(w, h))) m.bigPicture = pic;
    const bool hot = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = em() * 0.4f;
    dl->AddImageRounded(ImTextureRef(ImTextureID(g.texture)), a, ImVec2(a.x + w, a.y + h), ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32_WHITE, r);
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(hot ? kSignal : kLine), r, 0, hot ? 2.f : 1.f);
    if (hot) ImGui::SetTooltip("%s", T("Click to see it bigger"));
    gap(0.5f);
}

// The picture, as big as the window allows. Click anywhere or press Esc to close.
void bigPictureWindow(UiModel& m) {
    if (m.bigPicture < 0 || m.bigPicture >= kPicCount) { m.bigPicture = -1; return; }
    const GuidePic& g = m.guidePics[m.bigPicture];
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 200));
    const float s = std::min({vp->Size.x * 0.94f / float(g.width), vp->Size.y * 0.9f / float(g.height), 1.0f});
    const ImVec2 size(float(g.width) * s, float(g.height) * s);
    const ImVec2 a(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, vp->Pos.y + (vp->Size.y - size.y) * 0.5f);
    dl->AddImageRounded(ImTextureRef(ImTextureID(g.texture)), a, ImVec2(a.x + size.x, a.y + size.y), ImVec2(0, 0), ImVec2(1, 1),
                        IM_COL32_WHITE, em() * 0.4f);
    const char* hint = T("Click anywhere to close");
    const ImVec2 hs = ImGui::CalcTextSize(hint);
    dl->AddText(ImVec2(vp->Pos.x + (vp->Size.x - hs.x) * 0.5f, a.y + size.y + em() * 0.4f), col(kMuted), hint);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape)) m.bigPicture = -1;
}

// Where to go and what to click, for one service: the page, numbered steps, the pictures.
void keyGuide(UiModel& m, UiActions& act, int which, bool inSetup) {
    const KeyGuideText& g = kKeyGuides[which];
    // The website, and buttons that open it.
    ImGui::TextColored(kMuted, "%s", T("Website:"));
    ImGui::SameLine();
    ImGui::TextUnformatted(g.pageShown);
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    if (g.extraUrl) {
        const float half = (ImGui::GetContentRegionAvail().x - sp) * 0.5f;
        if (button(g.extraLabel, false, half)) act.openUrl = g.extraUrl;
        ImGui::SameLine();
        if (button("Open the keys page", true, half)) act.openUrl = g.page;
    } else {
        if (button("Open the page", true)) act.openUrl = g.page;
    }
    note("Then follow these steps. Click a picture to see it bigger.");
    gap(0.3f);
    int n = 1;
    for (const char* s : g.steps) {
        if (!s) continue;
        setupStepLine(n, s);
        bool shown = false;
        for (int i = 0; i < 5; ++i)  // the picture for what was just said
            if (g.pics[i] >= 0 && g.picAfter[i] == n) {
                if (!shown) gap(0.1f);
                guidePicture(m, g.pics[i]);
                shown = true;
            }
        ++n;
    }
    setupStepLine(n, inSetup ? "Paste the key in the box below." : "Paste the key in its box under Accounts.");
    gap(0.3f);
}

// The key box with "Check key" and what the check found, for DeepL, Gemini, Claude or OpenAI.
void keyBox(UiModel& m, UiActions& act, int which, std::string& key, const char* id) {
    fieldLabel(which == kGuideDeepL    ? "DeepL API key"
               : which == kGuideClaude ? "Claude API key"
               : which == kGuideOpenAI ? "OpenAI API key"
                                       : "Gemini API key");
    const float checkW = std::max(em() * 7.f, ImGui::CalcTextSize(T("Check key")).x + em() * 1.6f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - checkW - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::InputTextWithHint(id, T("Paste it here"), &key, m.showKey ? 0 : ImGuiInputTextFlags_Password))
        m.keyCheck[which] = kCheckNone;  // a different key: not checked yet
    ImGui::SameLine();
    const bool checking = m.keyCheck[which] == kChecking;
    ImGui::BeginDisabled(checking || key.find_first_not_of(" \t\r\n") == std::string::npos);
    if (button(checking ? "Checking" : "Check key", true, checkW)) act.checkKey = which;
    ImGui::EndDisabled();
    if (checking) note(std::string(T("Checking")) + dots(), kBusy);
    else if (m.keyCheck[which] == kCheckGood) note(T(m.keyCheckMsg[which]), kVoices[2]);
    else if (m.keyCheck[which] == kCheckBad) note(T(m.keyCheckMsg[which]), kDanger);
}

// Settings: the same guide in a window, from "How do I get this?".
void guideWindow(UiModel& m, UiActions& act) {
    if (m.guideOpen < 0 || m.guideOpen >= kGuideCount) return;
    if (!ImGui::IsPopupOpen("##keyguide")) ImGui::OpenPopup("##keyguide");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(em() * 38.f, vp->Size.x * 0.92f), vp->Size.y * 0.86f));
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.3f, em() * 1.1f));
    if (ImGui::BeginPopupModal("##keyguide", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        char title[160];
        std::snprintf(title, sizeof(title), TF("Get your %s key"), kKeyGuides[m.guideOpen].name);
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.4f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        gap(0.4f);
        const bool pictureOpen = m.bigPicture >= 0;
        if (pictureOpen) ImGui::BeginDisabled();
        ImGui::BeginChild("guidebody", ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() - em() * 0.6f), 0,
                          ImGuiWindowFlags_NoBackground);
        keyGuide(m, act, m.guideOpen, false);
        ImGui::EndChild();
        const bool close = button("Close", true, em() * 7.f) || (ImGui::IsKeyPressed(ImGuiKey_Escape) && !pictureOpen);
        if (pictureOpen) ImGui::EndDisabled();
        if (close) {
            m.guideOpen = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// A quiet link under a key field.
void guideLink(UiModel& m, int which) {
    ImGui::PushID(which);
    ImGui::PushStyleColor(ImGuiCol_Text, kSignal);
    const char* t = T("How do I get this?");
    if (ImGui::Selectable(t, false, 0, ImGui::CalcTextSize(t))) m.guideOpen = which;
    ImGui::PopStyleColor();
    ImGui::PopID();
}

// CIMA Box -> CABLE Input == CABLE Output -> your game's mic, as four boxes.
void cableDiagram() {
    const float w = ImGui::GetContentRegionAvail().x, h = em() * 3.4f, arrow = em() * 1.4f;
    const float bw = (w - arrow * 3) / 4.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const char* top[] = {"CIMA Box", "CABLE Input", "CABLE Output", "Your mic in the game"};
    const char* sub[] = {"makes the voice", "the cable's start", "the cable's end", "Discord, VRChat..."};
    for (int i = 0; i < 4; ++i) {
        const float x = p.x + i * (bw + arrow);
        const bool cable = i == 1 || i == 2;
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + bw, p.y + h), col(cable ? ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.14f) : kField), em() * 0.4f);
        dl->AddRect(ImVec2(x, p.y), ImVec2(x + bw, p.y + h), col(cable ? kSignal : kLine), em() * 0.4f);
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 0.86f);
        const char* t = i == 3 ? T(top[i]) : top[i];  // the others are names
        ImVec2 ts = ImGui::CalcTextSize(t, nullptr, false, bw - em() * 0.4f);
        dl->AddText(nullptr, 0.f, ImVec2(x + (bw - std::min(ts.x, bw - em() * 0.4f)) * 0.5f, p.y + em() * 0.45f), col(kText), t, nullptr, bw - em() * 0.4f);
        ImGui::PopFont();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.78f);
        const char* s = T(sub[i]);
        ts = ImGui::CalcTextSize(s, nullptr, false, bw - em() * 0.4f);
        dl->AddText(nullptr, 0.f, ImVec2(x + (bw - std::min(ts.x, bw - em() * 0.4f)) * 0.5f, p.y + h - ts.y - em() * 0.35f), col(kMuted), s, nullptr, bw - em() * 0.4f);
        ImGui::PopFont();
        if (i < 3) {  // the arrow (a double line for the cable itself)
            const float ax = x + bw + em() * 0.25f, bx = x + bw + arrow - em() * 0.25f, ay = p.y + h * 0.5f;
            if (i == 1) {
                dl->AddLine(ImVec2(ax, ay - 3), ImVec2(bx, ay - 3), col(kSignal), 2.f);
                dl->AddLine(ImVec2(ax, ay + 3), ImVec2(bx, ay + 3), col(kSignal), 2.f);
            } else {
                dl->AddLine(ImVec2(ax, ay), ImVec2(bx - 2, ay), col(kMuted), 2.f);
                dl->AddTriangleFilled(ImVec2(bx, ay), ImVec2(bx - em() * 0.4f, ay - em() * 0.3f), ImVec2(bx - em() * 0.4f, ay + em() * 0.3f), col(kMuted));
            }
        }
    }
    ImGui::Dummy(ImVec2(w, h));
    gap(0.4f);
}

bool hasCable(const UiModel& m, std::string* name) {
    for (const std::string& d : m.outputDevices)
        if (d.find("CABLE Input") != std::string::npos || d.find("VB-Audio") != std::string::npos) {
            if (name) *name = d;
            return true;
        }
    return false;
}

void setupLanguage(UiModel& m) {
    setupTitle("Choose your language", "You can change it any time in Settings.");
    const std::vector<UiLanguage>& langs = uiLanguages();
    const float gapX = em() * 0.7f, w = std::floor((ImGui::GetContentRegionAvail().x - gapX) * 0.5f), h = em() * 2.7f;
    for (size_t i = 0; i < langs.size(); ++i) {
        const bool sel = m.cfg.uiLanguage == langs[i].code;
        if (i % 2) ImGui::SameLine(0, gapX);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(int(i));
        if (ImGui::InvisibleButton("##lang", ImVec2(w, h))) {
            m.cfg.uiLanguage = langs[i].code;
            setUiLanguage(langs[i].code);
        }
        const bool hot = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = em() * 0.5f;
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(sel ? kSignal : hot ? kFieldHover : kPanel), r);
        if (!sel) dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(hot ? kFieldActive : kLine), r);
        ImGui::PushFont(sel ? gSemibold : nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
        const ImVec2 ts = ImGui::CalcTextSize(langs[i].name.c_str());
        dl->AddText(ImVec2(a.x + em() * 1.0f, a.y + (h - ts.y) * 0.5f), col(sel ? kOnSignal : kText), langs[i].name.c_str());
        ImGui::PopFont();
        if (i % 2 == 0 && i + 1 == langs.size()) ImGui::NewLine();
        if (i % 2 == 1 || i + 1 == langs.size()) gap(0.15f);
    }
}

void setupWelcome() {
    setupTitle("Welcome to CIMA Box", "Let's get you set up. It takes about two minutes, and you can change everything later.");
    setupStepLine(1, "Connect ElevenLabs, where the voices come from, plus DeepL and an AI (Gemini, Claude or OpenAI) for great translations.");
    setupStepLine(2, "Choose what you want to do: talk, type, or read what others say.");
    setupStepLine(3, "Choose where the voice goes, then play a test.");
    gap(0.4f);
    note("Every step shows you exactly where to click, with pictures.");
}

void setupKey(UiModel& m, UiActions& act) {
    chip("Required", kSignal);
    setupTitle("Connect ElevenLabs", "Every voice comes from ElevenLabs. CIMA Box needs an API key: a password made just for apps. "
                                     "The free plan is enough to try everything.");
    keyGuide(m, act, kGuideEleven, true);
    fieldLabel("ElevenLabs API key");
    const float checkW = std::max(em() * 7.f, ImGui::CalcTextSize(T("Check key")).x + em() * 1.6f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - checkW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##setupkey", T("Paste it here"), &m.cfg.apiKey, m.showKey ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    ImGui::BeginDisabled(m.loading || m.cfg.apiKey.empty());
    if (button(m.loading ? "Checking" : "Check key", true, checkW)) act.loadCatalog = true;
    ImGui::EndDisabled();
    toggleRow("Show keys", &m.showKey);
    if (m.loading) {
        note(std::string(T("Checking")) + dots(), kBusy);
    } else if (!m.voices.empty()) {
        char b[160];
        std::snprintf(b, sizeof(b), TF("Connected. %d voices found."), int(m.voices.size()));
        note(b, kVoices[2]);
    } else if (!m.catalogMsg.empty()) {
        note(T(m.catalogMsg), kDanger);
    }
    gap(0.3f);
    note("Keys stay on this PC.");
}

void setupDeepL(UiModel& m, UiActions& act) {
    chip("Highly recommended", kBusy);
    setupTitle("Add DeepL", "Fast, reliable translations, and the backup CIMA Box uses whenever the AI is slow or busy. "
                            "The free plan is plenty for chatting.");
    note("Without it, the backup is a free translator with a small daily limit and rougher wording.", kText);
    gap(0.4f);
    keyGuide(m, act, kGuideDeepL, true);
    keyBox(m, act, kGuideDeepL, m.cfg.deeplKey, "##setupdeepl");
    toggleRow("Show keys", &m.showKey);
}

void setupAi(UiModel& m, UiActions& act) {
    chip("Highly recommended", kBusy);
    setupTitle("Make translations sound real", "Gemini, Claude or OpenAI translate the way people actually talk. You only need one. "
                                                "No key? A model on this PC works too, with limits.");
    ImGui::TextColored(kText, "%s", T("Without one, you'd miss:"));
    for (const char* miss : {"Dialects, like Mexican Spanish, Kansai Japanese or Kuwaiti Arabic",
                             "Slang and casual speech, instead of textbook wording",
                             "Matching how people talk to you",
                             "Words that change with who's speaking and who you're talking to"}) {
        ImGui::TextColored(kSignal, "  -");
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(kMuted, "%s", T(miss));
        ImGui::PopTextWrapPos();
    }
    gap(0.7f);
    if (setupCard("##gemini", kIconSpark, "Google Gemini", "Free to start, with a daily limit. Needs a Google account (18 or older).",
                  m.setupAi == kGuideGemini, false))
        m.setupAi = kGuideGemini;
    if (setupCard("##claude", kIconBubbles, "Claude", "The most natural. Pay as you go: a few cents for an evening of chatting, from credits you buy first.",
                  m.setupAi == kGuideClaude, false))
        m.setupAi = kGuideClaude;
    if (setupCard("##openai", kIconBolt, "OpenAI", "Cheap and quick. Try it free; $5 of credit lifts the limits.",
                  m.setupAi == kGuideOpenAI, false))
        m.setupAi = kGuideOpenAI;
    if (setupCard("##local", kIconSliders, "On this PC, no key", "Free and private, but it uses your PC's own power and isn't as good as the three above.",
                  m.setupAi == kSetupLocal, false))
        m.setupAi = kSetupLocal;
    gap(0.3f);
    if (m.setupAi == kSetupLocal) {  // pick and download a model right here, with this PC rated against each
        localModels(m.cfg);
        return;
    }
    keyGuide(m, act, m.setupAi, true);
    if (m.setupAi == kGuideClaude) keyBox(m, act, kGuideClaude, m.cfg.claudeKey, "##setupclaude");
    else if (m.setupAi == kGuideOpenAI) keyBox(m, act, kGuideOpenAI, m.cfg.openaiKey, "##setupopenai");
    else keyBox(m, act, kGuideGemini, m.cfg.geminiKey, "##setupgemini");
    toggleRow("Show keys", &m.showKey);
}

void setupUseStep(UiModel& m) {
    setupTitle("What do you want to do?", "Pick one or more. Every page stays available either way.");
    struct Opt { int bit, icon; const char* title; const char* desc; };
    static const Opt kOpts[] = {
        {kUseVoice, kIconMic, "Change my voice", "Talk into your mic and others hear a different voice, or your words in another language."},
        {kUseText, kIconKeys, "Type and be heard", "Type instead of talking. A voice says it for you, translated if you like."},
        {kUseRead, kIconBubbles, "Read what others say", "Live captions of what people say in a game or call, translated into your language."},
    };
    for (const Opt& o : kOpts) {
        ImGui::PushID(o.bit);
        if (setupCard("##use", o.icon, o.title, o.desc, (m.setupUse & o.bit) != 0, true)) m.setupUse ^= o.bit;
        ImGui::PopID();
    }
}

void setupVoice(UiModel& m, UiActions& act) {
    setupTitle("Pick a voice", "Any voice in your ElevenLabs library, including clones of your own voice.");
    fieldLabel("Voice");
    voiceCombo(m);
    if (m.voices.empty()) note("No voices yet: go back and check your ElevenLabs key.", kDanger);
    if (m.setupUse & kUseVoice) {
        gap(0.6f);
        fieldLabel("Your microphone");
        if (deviceCombo("##setupmic", m.inputDevices, m.cfg.inDevice)) act.reopenInput = true;
        meter(m.levelDb, m.meterDb, m.cfg.thresholdDb, m.recording, false);
        note("Say something: the bar should move.");
    }
}

void setupRouteStep(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    setupTitle("Where should the voice go?", "Pick what fits. You can change it later under Output on My voice.");
    struct Opt { int route, icon; const char* title; const char* desc; };
    static const Opt kOpts[] = {
        {kRouteMe, kIconHeadphones, "Just me", "Play it on my headphones or speakers. Good for trying it out."},
        {kRouteApps, kIconGame, "Into a game, Discord or a call", "Other people hear it as if it were your microphone."},
        {kRouteOwn, kIconSliders, "I have my own setup", "A virtual cable or mixer I already use. I'll pick the output myself."},
    };
    for (const Opt& o : kOpts) {
        ImGui::PushID(o.route);
        if (setupCard("##route", o.icon, o.title, o.desc, m.setupRoute == o.route, false) && m.setupRoute != o.route) {
            m.setupRoute = o.route;
            std::string cable;
            if (o.route == kRouteMe) { c.outDevice.clear(); c.monitor = false; act.reopenOutput = act.reopenMonitor = true; }
            if (o.route == kRouteApps && hasCable(m, &cable)) {
                c.outDevice = cable;
                c.monitor = true;
                c.monDevice.clear();
                act.reopenOutput = act.reopenMonitor = true;
            }
        }
        ImGui::PopID();
    }
    if (m.setupRoute == kRouteNone) return;
    gap(0.4f);
    std::string cable;
    if (m.setupRoute == kRouteApps && !hasCable(m, &cable)) {
        heading("You need a free virtual microphone", 1.1f);
        note("Discord and games only listen to microphones. VB-CABLE adds a pretend microphone to your PC: CIMA Box plays "
             "the voice into it, and the game hears it as if you were talking.", kText);
        gap(0.3f);
        cableDiagram();
        note("Think of it as a cable from CIMA Box into your game's microphone jack, except it's inside your PC.");
        gap(0.4f);
        setupStepLine(1, "Click Get VB-CABLE below and download the ZIP file from their page.");
        setupStepLine(2, "Open the ZIP and click Extract all.");
        setupStepLine(3, "In the extracted folder, right-click VBCABLE_Setup_x64 and choose Run as administrator.");
        setupStepLine(4, "Click Install Driver, then restart your PC.");
        setupStepLine(5, "Open CIMA Box again (it continues here) and press Look again. It finds the cable and picks it for you.");
        gap(0.2f);
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (button("Get VB-CABLE (free)", true, half)) act.openUrl = "https://vb-audio.com/Cable/";
        ImGui::SameLine();
        if (button("Look again", false, half)) act.rescanDevices = true;
        note("Can't install it right now? Pick Just me for now; you can switch any time.");
    } else if (m.setupRoute == kRouteApps) {
        note("VB-CABLE is installed and picked.", kVoices[2]);
        gap(0.2f);
        cableDiagram();
        heading("Last step: tell your game to use it", 1.05f);
        setupStepLine(1, "In Discord: Settings > Voice & Video > Input Device, choose CABLE Output.");
        setupStepLine(2, "In a game: open its audio or voice settings and set the microphone to CABLE Output.");
        setupStepLine(3, "Leave CIMA Box running while you play.");
        if (toggleRow("Hear it yourself too", &c.monitor)) act.reopenMonitor = true;
    } else {
        fieldLabel(m.setupRoute == kRouteMe ? "Play it on" : "Send the voice to");
        if (deviceCombo("##setupout", m.outputDevices, c.outDevice)) act.reopenOutput = true;
        if (m.setupRoute == kRouteOwn) {
            note("Pick the input end of your cable or mixer, then choose its output end as the microphone in your game or app.");
            if (toggleRow("Hear it yourself too", &c.monitor)) act.reopenMonitor = true;
        }
    }
    gap(0.6f);
    ImGui::BeginDisabled(c.voiceId.empty() || m.busy);
    if (button(m.busy ? "Playing" : "Play a test", false, em() * 9.f)) act.testSound = T("Hi! This is how my new voice sounds.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!m.lastError.empty()) ImGui::TextColored(kDanger, "%s", T(m.lastError));
    else ImGui::TextColored(kMuted, "%s", T(m.setupRoute == kRouteApps && !hasCable(m, nullptr) ? "It plays on your speakers for now."
                                          : m.setupRoute == kRouteApps && !c.monitor ? "Turn on Hear it yourself too to hear the test."
                                                                                     : "You should hear the voice."));
}

void setupRead(UiModel& m, UiActions& act) {
    ListenSettings& l = m.cfg.listen;
    setupTitle("Read what others say", "CIMA Box listens to a game or app and shows what people say, translated.");
    fieldLabel("Listen to");
    static const char* const kSources[] = {"One program", "Everything I hear", "A microphone"};
    segmented("##setupsource", kSources, 3, &l.source);
    if (l.source == kSrcApp) {
        appCombo(m, act);
        note("Only that program is captioned, like your game or Discord.");
    } else if (l.source == kSrcSpeakers) {
        deviceCombo("##setupspk", m.outputDevices, l.speakers);
        note("Everything playing on these speakers or headphones.");
    } else {
        deviceCombo("##setupin", m.inputDevices, l.input);
    }
    gap(0.5f);
    fieldLabel("Translate into");
    languageCombo("##setupto", l.toLang, false);
    note("Uses the free translator to start. A DeepL, Gemini, Claude or OpenAI key in Settings gives better translations.");
}

// A page name in amber and what to do there.
void setupPageRow(const char* page, const char* what) {
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::TextColored(kSignal, "%s", T(page));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted(T(what));
    ImGui::PopTextWrapPos();
    gap(0.6f);
}

void setupDone(UiModel& m) {
    setupTitle("You're all set", "Here's where everything is.");
    if (m.setupUse & kUseVoice) setupPageRow("My voice", "Flip the switch at the top right, then talk.");
    if (m.setupUse & kUseText) setupPageRow("Text", "Type at the bottom and press Enter. What others say shows up there too.");
    if (m.setupUse & kUseRead) setupPageRow("Listen", "Flip the switch at the top right, and captions appear as people talk.");
    gap(0.3f);
    note("Settings (bottom left) has your keys, hotkeys and the app's language, and can run this setup again.");
}

void finishSetup(UiModel& m) {
    m.cfg.setupDone = true;
    m.setupStep = -1;
    // Translate with the best service that has a key.
    auto has = [](const std::string& k) { return k.find_first_not_of(" \t\r\n") != std::string::npos; };
    bool localStarted = false;  // a model on this PC, downloaded or on its way
    for (const LocalModelState& ls : m.local.models) localStarted = localStarted || ls.stage != kModelNone;
    if (m.setupAi == kSetupLocal && localStarted) m.cfg.translateService = kLocal;
    else if (m.setupAi == kGuideClaude && has(m.cfg.claudeKey)) m.cfg.translateService = kClaude;
    else if (m.setupAi == kGuideGemini && has(m.cfg.geminiKey)) m.cfg.translateService = kGemini;
    else if (m.setupAi == kGuideOpenAI && has(m.cfg.openaiKey)) m.cfg.translateService = kOpenAI;
    else if (has(m.cfg.claudeKey)) m.cfg.translateService = kClaude;
    else if (has(m.cfg.geminiKey)) m.cfg.translateService = kGemini;
    else if (has(m.cfg.openaiKey)) m.cfg.translateService = kOpenAI;
    else if (has(m.cfg.deeplKey)) m.cfg.translateService = kDeepL;
    m.cfg.page = kPageHome;  // everything at a glance, with what's worth turning on
}

void setupView(UiModel& m, UiActions& act) {
    const ImVec2 full = ImGui::GetContentRegionAvail(), origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int step = std::max(0, std::min(m.setupStep, int(kStepCount) - 1));

    // Top: the logo and name, and Skip on the right.
    const float topH = em() * 2.4f;
    float x = origin.x;
    if (m.logoTexture != 0 && m.logoSize > 0) {
        const float side = std::floor(topH * 0.8f);
        dl->AddImageRounded(ImTextureRef(ImTextureID(m.logoTexture)), ImVec2(x, origin.y + (topH - side) * 0.5f),
                            ImVec2(x + side, origin.y + (topH + side) * 0.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, side * 0.22f);
        x += side + em() * 0.5f;
    }
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.2f);
    dl->AddText(ImVec2(x, origin.y + (topH - ImGui::GetFontSize()) * 0.5f), col(kText), "CIMA Box");
    ImGui::PopFont();
    if (step > kStepLanguage) {
        const char* skip = T("Skip setup");
        const ImVec2 ss = ImGui::CalcTextSize(skip);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + full.x - ss.x, origin.y + (topH - ss.y) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        if (ImGui::Selectable(skip, false, 0, ss)) finishSetup(m);
        ImGui::PopStyleColor();
        if (m.setupStep < 0) return;
    }

    // The column in the middle.
    const float colW = std::min(full.x, em() * 34.f), colX = origin.x + std::floor((full.x - colW) * 0.5f);
    const float navH = ImGui::GetFrameHeight() + em() * 1.2f;
    float y = origin.y + topH + em() * 1.4f;

    // Progress: one short bar per screen this person will see.
    int shownCount = 0, shownIndex = 0;
    for (int s = 0; s < kStepCount; ++s)
        if (setupShows(m, s)) {
            if (s == step) shownIndex = shownCount;
            ++shownCount;
        }
    const float segGap = em() * 0.35f, segW = (colW - segGap * (shownCount - 1)) / shownCount, segH = std::max(3.f, em() * 0.22f);
    for (int i = 0; i < shownCount; ++i) {
        const float sx = colX + i * (segW + segGap);
        dl->AddRectFilled(ImVec2(sx, y), ImVec2(sx + segW, y + segH), col(i <= shownIndex ? kSignal : kLine), segH * 0.5f);
    }
    y += segH + em() * 1.6f;

    ImGui::SetCursorScreenPos(ImVec2(colX, y));
    ImGui::BeginChild("setupbody", ImVec2(colW, origin.y + full.y - navH - y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    switch (step) {
    case kStepLanguage: setupLanguage(m); break;
    case kStepWelcome: setupWelcome(); break;
    case kStepKey: setupKey(m, act); break;
    case kStepDeepL: setupDeepL(m, act); break;
    case kStepAi: setupAi(m, act); break;
    case kStepUse: setupUseStep(m); break;
    case kStepVoice: setupVoice(m, act); break;
    case kStepRoute: setupRouteStep(m, act); break;
    case kStepRead: setupRead(m, act); break;
    default: setupDone(m); break;
    }
    ImGui::EndChild();

    // Back and Next at the bottom of the column.
    const float bw = em() * 8.5f, by = origin.y + full.y - ImGui::GetFrameHeight() - em() * 0.4f;
    dl->AddLine(ImVec2(colX, by - em() * 0.7f), ImVec2(colX + colW, by - em() * 0.7f), col(kLine));
    if (step > kStepLanguage) {
        ImGui::SetCursorScreenPos(ImVec2(colX, by));
        if (button("Back", false, bw)) m.setupStep = setupNext(m, step, -1);
    }
    const char* next = step == kStepDone ? "Start" : step == kStepLanguage ? "Continue" : "Next";
    bool canGo = true;
    if (step == kStepKey && m.cfg.apiKey.empty()) next = "Skip for now";
    if (step == kStepDeepL && m.cfg.deeplKey.empty()) next = "Skip for now";
    bool localStarted = false;
    for (const LocalModelState& ls : m.local.models) localStarted = localStarted || ls.stage != kModelNone;
    if (step == kStepAi && m.setupAi == kSetupLocal) { if (!localStarted) next = "Skip for now"; }
    else if (step == kStepAi && (m.setupAi == kGuideClaude   ? m.cfg.claudeKey
                            : m.setupAi == kGuideOpenAI ? m.cfg.openaiKey
                                                        : m.cfg.geminiKey).empty())
        next = "Skip for now";
    if (step == kStepUse) canGo = m.setupUse != 0;
    if (step == kStepRoute) canGo = m.setupRoute != kRouteNone;
    ImGui::SetCursorScreenPos(ImVec2(colX + colW - bw, by));
    ImGui::BeginDisabled(!canGo);
    if (button(next, true, bw)) {
        if (step == kStepKey && !m.cfg.apiKey.empty() && m.voices.empty() && !m.loading) act.loadCatalog = true;
        if (step == kStepDone) finishSetup(m);
        else m.setupStep = setupNext(m, step, 1);
    }
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------- Features page

// One feature: its name, one line about it and a switch. Its settings show only while it's on, so
// people who never use a feature never see its options. Returns whether to draw the settings;
// call featureCardEnd() either way.
bool featureCard(const char* id, const char* title, const char* desc, bool* on) {
    const bool glow = jumpAnchor(title);
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 0.85f, em() * 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(kField.x, kField.y, kField.z, (on && *on) ? 0.6f : 0.32f));
    ImGui::PushStyleColor(ImGuiCol_Border, glow       ? kSignal
                                           : (on && *on) ? ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.38f) : ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##card", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    const float sh = std::floor(em() * 1.2f), sw = sh * 1.8f;
    const float textW = ImGui::GetContentRegionAvail().x - (on ? sw + em() * 0.6f : 0.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.0f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
    ImGui::TextUnformatted(T(title));
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (desc && *desc) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.9f);
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
        ImGui::TextUnformatted(T(desc));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    if (on) {
        const ImVec2 after = ImGui::GetCursorScreenPos();
        const ImVec2 sp(p.x + ImGui::GetContentRegionAvail().x - sw, p.y + em() * 0.1f);
        ImGui::SetCursorScreenPos(sp);
        if (ImGui::InvisibleButton("##switch", ImVec2(sw, sh))) *on = !*on;
        drawSwitch(sp, sh, *on, ImGui::IsItemHovered(), ImGui::GetID("##cardanim"));
        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy(ImVec2(0, 0));
    }
    const bool open = !on || *on;
    if (open && on) gap(0.25f);
    return open;
}
void featureCardEnd() {
    ImGui::EndChild();
    ImGui::PopID();
    gap(0.6f);
}

// Small toggleable chips for picking several languages ("Languages I speak").
void languageChips(const char* id, std::vector<std::string>& codes) {
    ImGui::PushID(id);
    const float maxX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < codes.size(); ++i) {
        const Language* l = findLanguage(codes[i]);
        if (!l) continue;
        const std::string label = std::string(l->name) + "  x##c" + std::to_string(i);
        const float w = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < maxX) ImGui::SameLine();
        if (ImGui::SmallButton(label.c_str())) {
            codes.erase(codes.begin() + long(i));
            break;
        }
    }
    std::string add;
    languageCombo("##add", add, false);
    if (!add.empty() && std::find(codes.begin(), codes.end(), add) == codes.end()) codes.push_back(add);
    ImGui::PopID();
}

// A list of short texts, each with a remove button.
bool removableRow(const std::string& text, int i) {
    ImGui::PushID(i);
    const float xw = ImGui::GetFrameHeight();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - xw - em() * 0.4f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - xw);
    const bool gone = ImGui::Button("x", ImVec2(xw, 0));
    ImGui::PopID();
    return gone;
}

void featuresTalking(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    FeatureSettings& f = c.fx;

    bool preview = f.previewMs > 0;
    if (featureCard("cancel", "Cancel before it's spoken", "A moment to see what the voice is about to say, and stop it if it misheard you.", &preview)) {
        if (preview && f.previewMs == 0) f.previewMs = 1200;
        float sec = f.previewMs / 1000.f;
        if (sliderF("Wait before speaking", &sec, 0.5f, 3.f, "%.1f s")) f.previewMs = int(sec * 1000.f + 0.5f);
        fieldLabel("Cancel key");
        keyBinder(m, 10, f.cancelKey);
        note("Or click Cancel on the bar at the bottom. Adds this wait to every phrase.");
    }
    if (!preview) f.previewMs = 0;
    featureCardEnd();

    if (featureCard("answer", "Answer in their language", "Translate to switches to the language people speak to you.", &f.answerTheirs)) {
        note("It only switches after they say two things in a row in that language, so one short word can't flip it. "
             "Pick a language yourself any time; it switches again when someone speaks another language.");
        if (!m.autoTargetNote.empty()) note(m.autoTargetNote, kSignal);
    }
    featureCardEnd();

    if (featureCard("langs", "Languages", "Short phrases are easy to mistake for another language. List the ones you expect.", nullptr)) {
        fieldLabel("Languages I speak");
        languageChips("mine", f.myLanguages);
        fieldLabel("Languages around me");
        languageChips("around", f.aroundLanguages);
        note("\"Detect automatically\" then sticks to these. Leave a list empty to allow every language.");
        if (!m.heardLanguages.empty()) {
            fieldLabel("Heard this session");
            for (size_t i = 0; i < m.heardLanguages.size() && i < 6; ++i) {
                const Language* l = findLanguage(m.heardLanguages[i]);
                if (!l) continue;
                if (i > 0) ImGui::SameLine();
                ImGui::PushID(int(i));
                if (ImGui::SmallButton(l->name)) c.targetLang = l->code;
                ImGui::PopID();
            }
            note("Click one to translate into it.");
        }
    }
    featureCardEnd();

    bool swear = f.voiceSwear != kSwearKeep;
    if (featureCard("swear", "Clean up my swearing", "For streams or new people: the voice tones down or bleeps your swear words.", &swear)) {
        if (f.voiceSwear == kSwearKeep) f.voiceSwear = kSwearSoften;
        int mode = f.voiceSwear - 1;
        static const char* const kModes[] = {"Tone it down", "Say \"beep\""};
        if (segmented("##swearmode", kModes, 2, &mode)) f.voiceSwear = mode + 1;
        note(f.voiceSwear == kSwearSoften ? "Milder words instead (with an AI service; otherwise they're left out)."
                                          : "Each swear word becomes \"beep\".");
    }
    if (!swear) f.voiceSwear = kSwearKeep;
    featureCardEnd();

    if (featureCard("deeplwords", "Words for DeepL",
                    "Words the AI tends to soften or refuse, like strong swearing or slang. These skip the AI and DeepL translates them as they are.",
                    &f.deeplWordsOn)) {
        for (size_t i = 0; i < f.deeplWords.size(); ++i)
            if (removableRow(f.deeplWords[i], int(i))) { f.deeplWords.erase(f.deeplWords.begin() + long(i)); break; }
        const float addW = std::max(em() * 4.f, ImGui::CalcTextSize(T("Add")).x + em() * 1.6f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - ImGui::GetStyle().ItemSpacing.x);
        const bool enter = ImGui::InputTextWithHint("##newdeeplword", T("Add a word or phrase"), &m.deeplWordDraft,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (button("Add##deeplword", false, addW) || enter) {
            std::string item;
            auto take = [&] {
                while (!item.empty() && item.back() == ' ') item.pop_back();
                while (!item.empty() && item.front() == ' ') item.erase(item.begin());
                if (!item.empty() && std::find(f.deeplWords.begin(), f.deeplWords.end(), item) == f.deeplWords.end()) f.deeplWords.push_back(item);
                item.clear();
            };
            for (char ch : m.deeplWordDraft) { if (ch == ',') take(); else item += ch; }
            take();
            m.deeplWordDraft.clear();
        }
        note("Add several at once with commas. The AI translates the rest of your line and leaves a gap where each word goes; "
             "DeepL fills it in. Any letter case matches, and only whole words. On its own, DeepL can't always pick the right form for the sentence.");
        if (!isAiService(c.translateService)) note("Your translation service is already DeepL or Free, so every word goes there anyway.", kMuted);
        else if (c.deeplKey.empty()) note("No DeepL key: these words go to the Free service instead. Add a DeepL key in Settings > Accounts for better results.", kBusy);
    }
    featureCardEnd();

    if (featureCard("best", "Best translations", "Bigger AI models and more time. Better dialects and slang, a bit slower and pricier.", &f.best)) {
        note("Uses claude-sonnet-5-5, gemini-3.6-flash or gpt-6.2-sol, and allows twice the time limit.");
    }
    featureCardEnd();

    if (featureCard("try", "Try a line", "See how each style says something, with your own keys.", nullptr)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##trytext", T("Type something to say"), &m.tryText);
        ImGui::BeginDisabled(m.tryBusy || m.tryText.find_first_not_of(" ") == std::string::npos || !isAiService(c.translateService));
        if (button(m.tryBusy ? "Translating" : "Try it", true)) act.tryLine = m.tryText;
        ImGui::EndDisabled();
        if (!isAiService(c.translateService)) note("Needs Claude, Gemini or OpenAI as the translation service.");
        static const char* const kNames[] = {"Polite", "Casual", "Very casual", "My own"};
        for (int i = 0; i < 4; ++i) {
            if (m.tryResults[i].empty()) continue;
            ImGui::TextColored(kSignal, "%s", T(kNames[i]));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(m.tryResults[i].c_str());
            ImGui::PopTextWrapPos();
        }
    }
    featureCardEnd();

    if (featureCard("keys", "More hotkeys", nullptr, nullptr)) {
        fieldLabel("Hold for my real voice");
        keyBinder(m, 11, f.realVoiceKey);
        note("While held, your own mic goes out instead of the new voice.");
        fieldLabel("Stop speaking");
        keyBinder(m, 12, f.stopKey);
        note("Stops the voice right away and drops anything waiting.");
    }
    featureCardEnd();
}

void featuresHearing(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    FeatureSettings& f = c.fx;
    ScreenSettings& sc = c.screen;

    bool screen = sc.on;
    if (featureCard("screen", "Screen translation", "Translate text in a game or app: chat boxes, signs, menus. Take a snapshot, or let it watch a chat box.", &screen)) {
        if (screen && !sc.agreed) {  // the notice comes first
            m.screenNotice = true;
            screen = false;
        }
        if (sc.on) {
            // The three things needed for a snapshot come first; the rest are choices.
            fieldLabel("Window");
            appCombo(m, act, sc.app, "##screenapp");
            if (sc.toLang.empty()) sc.toLang = c.listen.toLang;
            fieldLabel("Translate into");
            languageCombo("##screento", sc.toLang, false);
            gap(0.2f);
            ImGui::BeginDisabled(m.shot.busy || sc.app.empty());
            if (button(m.shot.busy ? "Reading" : "Take a snapshot", true)) act.screenSnapshot = true;
            ImGui::EndDisabled();
            if (!m.screenStatus.empty()) note(T(m.screenStatus), kBusy);
            else note("Then drag over the text you want in the picture and press Translate. It shows up with the captions and in the conversation.");

            subheading("Who reads the text");
            static const char* const kReaders[] = {"Windows (free)", "AI"};
            segmented("##reader", kReaders, 2, &sc.reader);
            note(sc.reader == kReadWindows ? "Reads on your PC, free. Knows fewer languages (see the notice)."
                                           : "Reads any language and translates in one step, but pictures use many more tokens.");
            if (sc.reader == kReadAi && !isOnlineAi(c.translateService))
                note("AI reading needs Claude, Gemini or OpenAI as the translation service.", kDanger);
            if (sc.reader == kReadWindows) {
                fieldLabel("Text in the picture");
                languageCombo("##readlang", sc.readLang, true);
                if (m.readerKnown && m.readerLanguages.empty()) {
                    note("Windows' text reader isn't available on this PC. Use AI reading instead.", kDanger);
                } else if (sc.readLang.empty()) {
                    note("Pick the language of the text. Left on automatic, it only reads the languages Windows itself is set to.");
                } else if (m.readerKnown) {
                    bool can = false;  // Windows names them "ja", "en-US", "zh-Hans-CN"...
                    for (const std::string& tag : m.readerLanguages)
                        if (tag == sc.readLang || tag.rfind(sc.readLang + "-", 0) == 0) can = true;
                    if (!can) {
                        note("Windows can't read this language yet. Add it in Windows Settings > Time & language > Language (with its text recognition), or use AI reading.", kDanger);
                        if (button("Open Windows language settings")) act.openUrl = "ms-settings:regionlanguage";
                    }
                }
            }

            subheading("Watch a chat box");
            toggleRow("Watch for new lines", &sc.watch);
            if (sc.watch) {
                float sec = sc.watchSeconds;
                if (sliderF("Look every", &sec, 1.f, 15.f, "%.0f s")) sc.watchSeconds = sec;
                const bool whole = sc.area[0] <= 0.f && sc.area[1] <= 0.f && sc.area[2] >= 1.f && sc.area[3] >= 1.f;
                note(whole ? "Reads the whole window. Take a snapshot and crop it to the chat box for better results."
                           : "Reads the area you picked in the preview. Only new lines are translated.");
                if (sc.reader == kReadAi) note("Watching with AI reading sends a picture every time it looks. That adds up fast.", kBusy);
            }

            subheading("While you play");
            fieldLabel("Snapshot key");
            keyBinder(m, 13, sc.key);
            note("The key, and Translate screen in the SteamVR menu, translate right away: you're in the game, so there's no preview to click.");
            toggleRow("Translate without the preview", &sc.skipPreview);
            toggleRow("Also show on the VR panel", &sc.toVr);
            note("Don't capture anything private.", kBusy);
        }
    }
    if (!sc.agreed) screen = false;
    sc.on = screen;
    if (!sc.on) sc.watch = false;
    featureCardEnd();

    if (featureCard("ear", "Hear them in my ear", "A voice reads the translations of what others say, in your headphones.", &f.hearThem)) {
        fieldLabel("Voice");
        std::string preview = T("The voice I use");
        for (const Voice& v : m.voices)
            if (v.id == f.earVoice) preview = v.name;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##earvoice", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable(T("The voice I use"), f.earVoice.empty())) f.earVoice.clear();
            for (const Voice& v : m.voices) {
                ImGui::PushID(v.id.c_str());
                if (ImGui::Selectable(v.name.c_str(), v.id == f.earVoice)) f.earVoice = v.id;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        fieldLabel("Play it on");
        deviceCombo("##eardevice", m.outputDevices, f.earDevice);
        sliderF("Volume", &f.earVolume, 0.f, 2.f, "%.0f%%", 100.f);
        note("Needs Listen on. Uses ElevenLabs credits for every caption.");
    }
    featureCardEnd();

    if (featureCard("live", "Captions while they talk", "Words appear while someone is still speaking, then the translation replaces them.", &f.liveCaptions)) {
        note("Transcribes the unfinished sentence every second or two, so it uses more ElevenLabs credits.");
    }
    featureCardEnd();

    if (featureCard("captions", "Captions", nullptr, nullptr)) {
        toggleRow("Hide swearing (f***)", &f.maskCaptions);
        toggleRow("Sound labels ([laughs], [music])", &f.soundLabels);
        toggleRow("Skip music and noise", &f.ignoreNoise);
        toggleRow("Big, high-contrast captions", &f.highContrast);
        toggleRow("Same voice, same color", &f.voiceIds);
        if (f.voiceIds) note("Recognizes voices across captions (a best guess). Right-click a line to mute that voice.");
        toggleRow("Guess gender from their words", &f.genderFromWords);
        if (f.genderFromWords) note("\"Estoy cansada\" says more than a voice, which a voice changer can fake.");
        if (!m.mutedVoices.empty()) {
            char b[120];
            std::snprintf(b, sizeof(b), TF("%d muted voices"), int(m.mutedVoices.size()));
            note(b);
            if (button("Unmute everyone")) m.mutedVoices.clear();
        }
    }
    featureCardEnd();

    if (featureCard("overlay", "Caption window over the game", "A small see-through window of captions on top of your game (borderless windowed mode, not fullscreen).", nullptr)) {
        toggleRow("Clicks go through it", &f.clickThrough);
        if (f.clickThrough && f.overlayKey == 0) note("Set a key below first: it's how you get the window back.", kBusy);
        sliderF("See-through", &f.overlayOpacity, 0.3f, 1.f, "%.0f%%", 100.f);
        fieldLabel("Key to switch click-through");
        keyBinder(m, 14, f.overlayKey);
        if (button("Open the caption window", true)) act.openCaptionWindow = true;
    }
    featureCardEnd();
}

void featuresMore(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    FeatureSettings& f = c.fx;

    if (featureCard("phrases", "Quick phrases", "Buttons on the Text page for things you say a lot.", nullptr)) {
        for (size_t i = 0; i < f.quickPhrases.size(); ++i)
            if (removableRow(f.quickPhrases[i], int(i))) { f.quickPhrases.erase(f.quickPhrases.begin() + long(i)); break; }
        const float addW = std::max(em() * 4.f, ImGui::CalcTextSize(T("Add")).x + em() * 1.6f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - ImGui::GetStyle().ItemSpacing.x);
        const bool enter = ImGui::InputTextWithHint("##newphrase", T("Add a phrase"), &m.phraseDraft, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((button("Add##phrase", false, addW) || enter) && m.phraseDraft.find_first_not_of(" ") != std::string::npos) {
            f.quickPhrases.push_back(m.phraseDraft);
            m.phraseDraft.clear();
        }
        const std::vector<std::string> common = commonLines(f, 3);
        if (!common.empty()) {
            fieldLabel("You type these a lot");
            for (size_t i = 0; i < common.size(); ++i) {
                ImGui::PushID(int(i) + 100);
                if (ImGui::SmallButton((std::string("+ ") + common[i]).c_str())) f.quickPhrases.push_back(common[i]);
                ImGui::PopID();
            }
        }
    }
    featureCardEnd();

    if (featureCard("words", "My word list", "Names and words it keeps getting wrong. Written your way, and never translated.", nullptr)) {
        for (size_t i = 0; i < f.words.size(); ++i) {
            const WordFix& w = f.words[i];
            const std::string row = w.heard.empty() ? w.write : w.heard + "  >  " + w.write;
            if (removableRow(row, int(i) + 1000)) { f.words.erase(f.words.begin() + long(i)); break; }
        }
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::SetNextItemWidth(half);
        ImGui::InputTextWithHint("##wordheard", T("It writes (optional)"), &m.wordHeard);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(half);
        ImGui::InputTextWithHint("##wordwrite", T("Write it as"), &m.wordWrite);
        if (button("Add##word") && m.wordWrite.find_first_not_of(" ") != std::string::npos) {
            f.words.push_back({m.wordHeard, m.wordWrite});
            m.wordHeard.clear();
            m.wordWrite.clear();
        }
        note("For example: it writes \"see my box\", write it as \"CIMA Box\". Or just a name, to keep it as it is.");
    }
    featureCardEnd();

    if (featureCard("learn", "Learning mode", "On the Text page: how to read each line, replay it, and save lines to study.", &f.learning)) {
        note("How to read it (romaji, pinyin, romanization) needs an AI service. Replay plays in your headphones.");
        if (!f.savedLines.empty()) {
            fieldLabel("Saved lines");
            for (size_t i = 0; i < f.savedLines.size(); ++i)
                if (removableRow(f.savedLines[i], int(i) + 2000)) { f.savedLines.erase(f.savedLines.begin() + long(i)); break; }
            if (button("Copy them all")) {
                std::string all;
                for (const std::string& s : f.savedLines) all += s + "\n";
                ImGui::SetClipboardText(all.c_str());
            }
        }
    }
    featureCardEnd();

    if (featureCard("history", "Keep a history", "Saves the conversation to a text file for each day, on this PC only.", &f.keepHistory)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##historyq", T("Search the history"), &m.historyQuery, ImGuiInputTextFlags_EnterReturnsTrue))
            act.historySearch = m.historyQuery;
        for (const std::string& r : m.historyResults) {
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(r.c_str());
            ImGui::PopTextWrapPos();
        }
        if (button("Open the history folder")) act.openHistory = true;
    }
    featureCardEnd();

    if (featureCard("profiles", "Profiles", "Save your setup and switch in one click: \"Japanese lobby\", \"Streaming\"...", nullptr)) {
        for (const auto& kv : f.profiles) {
            ImGui::PushID(kv.first.c_str());
            const float bw = em() * 4.2f;
            ImGui::TextUnformatted(kv.first.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - bw * 2 - ImGui::GetStyle().ItemSpacing.x);
            if (button("Load", true, bw)) act.loadProfile = kv.first;
            ImGui::SameLine();
            if (button("Delete", false, bw)) act.deleteProfile = kv.first;
            ImGui::PopID();
        }
        const float saveW = em() * 5.f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - saveW - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputTextWithHint("##profilename", T("Name"), &m.profileName);
        ImGui::SameLine();
        ImGui::BeginDisabled(m.profileName.find_first_not_of(" ") == std::string::npos);
        if (button("Save##profile", false, saveW)) { act.saveProfile = m.profileName; m.profileName.clear(); }
        ImGui::EndDisabled();
        subheading("Share a setup");
        bool copy = false, paste = false;
        buttonPair("Copy my setup", "Paste a setup", copy, paste);
        act.copySetupCode = copy;
        act.pasteSetupCode = paste;
        note(m.setupCodeMsg.empty() ? "A code to paste in Discord. Never includes your keys." : m.setupCodeMsg);
    }
    featureCardEnd();
}

void featuresPage(UiModel& m, UiActions& act) {
    const float gapW = em() * 1.6f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float colW = std::floor((avail.x - gapW * 2) / 3.f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    beginPanel("fxtalk", ImVec2(colW, avail.y), "Talking");
    featuresTalking(m, act);
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + colW + gapW, origin.y));
    beginPanel("fxhear", ImVec2(colW, avail.y), "Hearing and reading");
    featuresHearing(m, act);
    ImGui::EndChild();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (colW + gapW) * 2, origin.y));
    beginPanel("fxmore", ImVec2(avail.x - (colW + gapW) * 2, avail.y), "Handy");
    featuresMore(m, act);
    ImGui::EndChild();
}


// ---------------------------------------------------------------- screen translation windows

// The notice before screen translation is first switched on. The box must be ticked.
void screenNoticeWindow(UiModel& m) {
    if (!m.screenNotice) return;
    if (!ImGui::IsPopupOpen("##screennotice")) ImGui::OpenPopup("##screennotice");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(em() * 40.f, vp->Size.x * 0.94f), vp->Size.y * 0.9f));
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.4f, em() * 1.1f));
    if (ImGui::BeginPopupModal("##screennotice", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::BeginChild("noticebody", ImVec2(0, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() * 2.6f), 0,
                          ImGuiWindowFlags_NoBackground);
        heading("Before you use screen translation", 1.45f);
        gap(0.4f);
        ImGui::PushStyleColor(ImGuiCol_Text, kDanger);
        heading("Never capture private or sensitive information", 1.12f);
        ImGui::PopStyleColor();
        auto para = [](const char* t, const ImVec4& c = kText) {
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(T(t));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        };
        auto bullet = [](const char* t) {
            ImGui::TextColored(kDanger, "  -");
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(T(t));
            ImGui::PopTextWrapPos();
        };
        para("Screen translation takes a picture of the app you choose. Anything visible in that app can end up in the picture, including things you didn't mean to capture.");
        gap(0.2f);
        para("Do NOT use it on anything showing:");
        bullet("Passwords, keys, codes or login screens");
        bullet("Bank, card or payment details");
        bullet("Private messages, DMs or emails");
        bullet("Your real name, address, phone number or other personal details");
        bullet("Anyone else's personal or private information");
        gap(0.2f);
        para("If you use AI reading, the picture is sent to that AI service (Claude, Gemini or OpenAI) and handled under their privacy rules, not CIMA Box's. Once it's sent, CIMA Box can't take it back. If you wouldn't post it publicly, don't capture it.", kBusy);
        subheading("It can use extra tokens");
        bullet("Windows reader (the default): reads the text on your PC. Free; only the translation uses tokens, like normal.");
        bullet("AI reading: the picture itself is sent to the AI, and pictures cost much more than text. One capture can cost as much as dozens of spoken lines.");
        bullet("Watch a chat box with AI reading keeps sending pictures as long as it's on. It can use up a free plan's daily limit quickly and adds to your spending. Your spending limit still applies.");
        subheading("The Windows reader only knows some languages");
        para("It can read: Arabic, Bosnian, Bulgarian, Chinese (Simplified and Traditional), Croatian, Czech, Danish, Dutch, English, Finnish, French, German, Greek, Hungarian, Italian, Japanese, Korean, Norwegian, Polish, Portuguese, Romanian, Russian, Serbian, Slovak, Slovenian, Spanish, Swedish and Turkish.");
        para("It can't read Hindi, Thai, Vietnamese, Indonesian, Hebrew, Ukrainian, Persian or Tagalog, among others. For those, use AI reading. Each language also has to be installed in Windows first (Settings > Time & language > Language).", kMuted);
        subheading("Good to know");
        bullet("It only captures the app you picked, only when you press the button or key (or while Watch is on).");
        bullet("Pictures are never saved or written to the log.");
        bullet("It never touches the game's files or memory. For competitive games, check their rules about screen capture.");
        bullet("Respect other people's privacy: their messages are theirs.");
        ImGui::EndChild();
        gap(0.2f);
        ImGui::Checkbox(T("I understand. I won't capture private or sensitive information."), &m.screenNoticeTicked);
        bool ok = false, cancel = false;
        const float bw = em() * 9.f;
        ImGui::BeginDisabled(!m.screenNoticeTicked);
        ok = button("I understand", true, bw);
        ImGui::EndDisabled();
        ImGui::SameLine();
        cancel = button("Cancel##notice", false, bw);
        if (ok) {
            m.cfg.screen.agreed = true;
            m.cfg.screen.on = true;
        }
        if (ok || cancel) {
            m.screenNotice = false;
            m.screenNoticeTicked = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// Once, after the setup (or after updating from a version that didn't have it): CIMA Box runs on
// the person's own accounts, so what it uses is theirs to watch. Friendly, short, and one button.
void usageNoticeWindow(UiModel& m, bool inSetup) {
    const bool due = m.cfg.usageNotice < kUsageNotice && !inSetup;
    if (!due && !m.showUsageNotice) return;
    if (!ImGui::IsPopupOpen("##usagenotice")) ImGui::OpenPopup("##usagenotice");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(em() * 36.f, vp->Size.x * 0.92f), 0.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(vp->Size.x * 0.92f, vp->Size.y * 0.92f));
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.6f, em() * 1.3f));
    if (ImGui::BeginPopupModal("##usagenotice", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        auto para = [](const char* t, const ImVec4& c = kText) {
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(T(t));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        };
        auto bullet = [](const char* t) {
            ImGui::TextColored(kSignal, "  -");
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(T(t));
            ImGui::PopTextWrapPos();
        };
        heading("One thing before you start", 1.45f);
        gap(0.5f);
        para("CIMA Box is free, and it runs on your own accounts: ElevenLabs, and DeepL, Claude, Gemini or OpenAI if you add them. Whatever it uses is charged by those services to your keys.");
        gap(0.4f);
        para("So please keep an eye on it:");
        bullet("Voices, captions and translations use credits or tokens for as long as they're switched on. Switch off what you aren't using.");
        bullet("Some settings use a lot more: Listen left running, Captions while they talk, Hear them in my ear, Best translations and AI screen reading.");
        bullet("Set a spending limit in Settings > Accounts, and look at what's left at the bottom left now and then.");
        bullet("If you share a key with friends, what they use counts on your account too.");
        gap(0.4f);
        para("The maker of CIMA Box isn't responsible for charges, used-up credits or tokens, or anything else that comes from using it. You choose the settings, so you use it at your own risk.", kMuted);
        gap(0.7f);
        const bool ok = button("Got it", true, em() * 8.f);
        ImGui::SameLine();
        const bool limit = button("Set a spending limit", false, std::max(em() * 11.f, ImGui::CalcTextSize(T("Set a spending limit")).x + em() * 1.6f));
        if (ok || limit) {
            m.cfg.usageNotice = kUsageNotice;
            m.showUsageNotice = false;
            if (limit) findJumpTo(m, "Spending limit");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// The picture that was taken: drag on it to pick the part to read, then translate.
void screenPreviewWindow(UiModel& m, UiActions& act) {
    ScreenShot& s = m.shot;
    if (!s.show) return;
    if (!ImGui::IsPopupOpen("##screenpreview")) ImGui::OpenPopup("##screenpreview");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.92f, vp->Size.y * 0.9f));
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.2f, em() * 1.0f));
    if (ImGui::BeginPopupModal("##screenpreview", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        heading("Screen translation", 1.3f);
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%s", s.app.c_str());
        note("Drag on the picture to pick the part to read, like the chat box. Nothing is sent until you press Translate.");
        const float sideW = std::max(em() * 18.f, ImGui::GetContentRegionAvail().x * 0.34f);
        const float footH = ImGui::GetFrameHeight() + em() * 0.6f;
        const ImVec2 room(ImGui::GetContentRegionAvail().x - sideW - em(), ImGui::GetContentRegionAvail().y - footH);
        // ---- the picture, with the chosen part outlined
        float w = room.x, h = s.width > 0 ? w * float(s.height) / float(s.width) : 0.f;
        if (h > room.y) { h = room.y; w = s.height > 0 ? h * float(s.width) / float(s.height) : 0.f; }
        const ImVec2 top = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##pic", ImVec2(std::max(1.f, room.x), std::max(1.f, room.y)));
        const ImVec2 a(top.x + (room.x - w) * 0.5f, top.y + (room.y - h) * 0.5f);  // centred in its room
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (s.texture) dl->AddImage(ImTextureRef(ImTextureID(s.texture)), a, ImVec2(a.x + w, a.y + h));
        static ImVec2 dragFrom;
        if (ImGui::IsItemActivated()) dragFrom = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActive() && w > 0 && h > 0) {
            const ImVec2 b = ImGui::GetIO().MousePos;
            auto nx = [&](float x) { return clamp01((x - a.x) / w); };
            auto ny = [&](float y) { return clamp01((y - a.y) / h); };
            const float x0 = std::min(nx(dragFrom.x), nx(b.x)), x1 = std::max(nx(dragFrom.x), nx(b.x));
            const float y0 = std::min(ny(dragFrom.y), ny(b.y)), y1 = std::max(ny(dragFrom.y), ny(b.y));
            if (x1 - x0 > 0.02f && y1 - y0 > 0.02f) { s.crop[0] = x0; s.crop[1] = y0; s.crop[2] = x1; s.crop[3] = y1; }
        }
        const ImVec2 c0(a.x + s.crop[0] * w, a.y + s.crop[1] * h), c1(a.x + s.crop[2] * w, a.y + s.crop[3] * h);
        const ImU32 shade = IM_COL32(0, 0, 0, 140);  // dim what won't be read
        dl->AddRectFilled(a, ImVec2(a.x + w, c0.y), shade);
        dl->AddRectFilled(ImVec2(a.x, c1.y), ImVec2(a.x + w, a.y + h), shade);
        dl->AddRectFilled(ImVec2(a.x, c0.y), ImVec2(c0.x, c1.y), shade);
        dl->AddRectFilled(ImVec2(c1.x, c0.y), ImVec2(a.x + w, c1.y), shade);
        dl->AddRect(c0, c1, col(kSignal), 0, 0, 2.f);
        // ---- what was found
        ImGui::SetCursorScreenPos(ImVec2(top.x + room.x + em(), top.y));
        ImGui::BeginChild("found", ImVec2(sideW, room.y), ImGuiChildFlags_Borders);
        if (s.busy) ImGui::TextColored(kBusy, TF("Reading%s"), dots().c_str());
        else if (s.lines.empty()) note(m.screenStatus.empty() ? "The text it finds and its translation show up here." : m.screenStatus);
        for (const ScreenLine& l : s.lines) {
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(l.translated.c_str());
            if (l.translated != l.text) ImGui::TextColored(kMuted, "%s", l.text.c_str());
            ImGui::PopTextWrapPos();
            gap(0.3f);
        }
        if (!s.lines.empty() && !s.busy && button("Copy them all")) {
            std::string all;
            for (const ScreenLine& l : s.lines) all += l.translated + "\n";
            ImGui::SetClipboardText(all.c_str());
        }
        if (m.cfg.screen.reader == kReadAi && s.width > 0) {  // what the chosen part costs, not the whole picture
            const int pw = std::min(1280, int(float(s.width) * (s.crop[2] - s.crop[0])));
            const int ph = std::min(1280, int(float(s.height) * (s.crop[3] - s.crop[1])));
            char b[160];
            std::snprintf(b, sizeof(b), TF("AI reading: about %d tokens for this picture."), pictureTokens(m.cfg.translateService, pw, ph));
            note(b, kBusy);
        }
        ImGui::EndChild();
        // ---- buttons
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + room.y + em() * 0.5f));
        const float bw = em() * 8.5f;
        ImGui::BeginDisabled(s.busy);
        if (button("Translate", true, bw)) act.screenTranslate = true;
        ImGui::SameLine();
        if (button("Retake", false, bw)) act.screenSnapshot = true;
        ImGui::SameLine();
        if (button("Whole window", false, bw)) { s.crop[0] = s.crop[1] = 0.f; s.crop[2] = s.crop[3] = 1.f; }
        ImGui::SameLine();
        if (button("Watch this area", false, std::max(bw, ImGui::CalcTextSize(T("Watch this area")).x + em() * 1.6f))) act.screenUseArea = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", T("Keeps looking at this part and translates new lines as they appear."));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (button("Close##preview", false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            act.screenClose = true;
            s.show = false;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::GetContentRegionAvail().x - ImGui::GetItemRectSize().x > ImGui::CalcTextSize(T("Don't capture private info.")).x + em() * 2.f) ImGui::SameLine();
        ImGui::TextColored(kDanger, "%s", T("Don't capture private info."));
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}


}  // namespace

// ---------------------------------------------------------------- public

// ---------------------------------------------------------------- the look (Settings > Look)

void styleColors() {
    ImVec4* c = ImGui::GetStyle().Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = kPanel;
    c[ImGuiCol_PopupBg] = kPopup;
    c[ImGuiCol_Border] = kLine;
    c[ImGuiCol_FrameBg] = kField;
    c[ImGuiCol_FrameBgHovered] = kFieldHover;
    c[ImGuiCol_FrameBgActive] = kFieldActive;
    c[ImGuiCol_Button] = kField;
    c[ImGuiCol_ButtonHovered] = kFieldHover;
    c[ImGuiCol_ButtonActive] = kFieldActive;
    c[ImGuiCol_Header] = kFieldHover;
    c[ImGuiCol_HeaderHovered] = kFieldHover;
    c[ImGuiCol_HeaderActive] = kFieldActive;
    c[ImGuiCol_SliderGrab] = kSignal;
    c[ImGuiCol_SliderGrabActive] = kSignalHover;
    c[ImGuiCol_CheckMark] = kSignal;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = kLine;
    c[ImGuiCol_ScrollbarGrabHovered] = kFieldActive;
    c[ImGuiCol_ScrollbarGrabActive] = kMuted;
    c[ImGuiCol_Separator] = kLine;
    c[ImGuiCol_TextSelectedBg] = ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.32f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.03f, 0.04f, 0.06f, 0.62f);
    c[ImGuiCol_NavCursor] = kSignal;

    c[ImGuiCol_ChildBg] = ImVec4(kPanel.x, kPanel.y, kPanel.z, gPanelAlpha);
}

ImVec4 rgbOf(uint32_t v) { return rgb(v); }
float luminance(const ImVec4& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
ImVec4 toward(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.f); }

// Recolours the whole app from the four colours someone picked. The original look keeps its exact
// hand-tuned shades; anything else gets its in-between shades worked out from the four.
void applyLook(const LookSettings& l, bool picture) {
    static LookSettings last;
    static bool lastPicture = false, first = true;
    if (!first && std::memcmp(&last.accent, &l.accent, sizeof(uint32_t) * 4) == 0 && last.textScale == l.textScale &&
        lastPicture == picture && last.pictureShow == l.pictureShow)
        return;
    first = false;
    last.accent = l.accent; last.background = l.background; last.panel = l.panel; last.text = l.text;
    last.textScale = l.textScale; last.pictureShow = l.pictureShow; lastPicture = picture;
    LookSettings d;
    const bool original = l.accent == d.accent && l.background == d.background && l.panel == d.panel && l.text == d.text;
    if (original) {
        kBg = rgb(0x14171E); kPanel = rgb(0x1C212B); kField = rgb(0x272D3A); kFieldHover = rgb(0x303747);
        kFieldActive = rgb(0x394154); kLine = rgb(0x2B3240); kText = rgb(0xDDE2EB); kMuted = rgb(0x8D98AD);
        kSignal = rgb(0xE8B05E); kSignalHover = rgb(0xF0C07A); kSignalDown = rgb(0xCF9A48); kOnSignal = rgb(0x1E1707);
        kPopup = rgb(0x222834);
    } else {
        kBg = rgbOf(l.background);
        kPanel = rgbOf(l.panel);
        kText = rgbOf(l.text);
        kField = toward(kPanel, kText, 0.07f);
        kFieldHover = toward(kPanel, kText, 0.11f);
        kFieldActive = toward(kPanel, kText, 0.16f);
        kLine = toward(kPanel, kText, 0.08f);
        kMuted = toward(kPanel, kText, 0.62f);
        kPopup = toward(kPanel, kText, 0.04f);
        kSignal = rgbOf(l.accent);
        const bool dark = luminance(kBg) < 0.5f;
        kSignalHover = toward(kSignal, dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), 0.18f);
        kSignalDown = toward(kSignal, ImVec4(0, 0, 0, 1), 0.12f);
        kOnSignal = luminance(kSignal) > 0.45f ? toward(kSignal, ImVec4(0, 0, 0, 1), 0.88f) : ImVec4(1, 1, 1, 1);
    }
    kLabel = kMuted;
    kVoices[0] = kSignal;
    gPanelAlpha = picture ? std::max(0.55f, 1.f - l.pictureShow * 0.6f) : 1.f;
    styleColors();
    // ImGui 1.92 takes a new base size at the start of the next frame (the same way its own style editor does).
    ImGui::GetStyle().FontSizeBase = ImGui::GetStyle()._NextFrameFontSizeBase = 16.f * std::min(1.3f, std::max(0.85f, l.textScale));
}

void uiSetup(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.WindowRounding = 0;
    s.ChildRounding = 12;
    s.FrameRounding = 7;
    s.PopupRounding = 10;
    s.GrabRounding = 6;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.GrabMinSize = 14;
    s.ScrollbarSize = 8;
    s.DisabledAlpha = 0.4f;

    styleColors();

    s.ScaleAllSizes(scale);
    s.FontSizeBase = 16.0f;  // Settings > Look > Text size scales this (applyLook)
    s.FontScaleDpi = scale;
    loadFonts();
}

void findJump(UiModel& m, const char* label) { findJumpTo(m, label); }

void lookPreset(LookSettings& l, int preset) {
    struct P { uint32_t accent, background, panel, text; };
    static const P kPresets[kLookCount] = {
        {0xE8B05E, 0x14171E, 0x1C212B, 0xDDE2EB},  // Default: the original amber on slate
        {0x7AA2F7, 0x0B0D13, 0x141822, 0xDCE3F2},  // Midnight: blue on near-black
        {0x7FD6A4, 0x0F1512, 0x17201B, 0xDCE8E0},  // Forest: green on dark green-grey
        {0xE59AD0, 0x17121A, 0x221A26, 0xEDE0EA},  // Rose: pink on plum
    };
    if (preset < 0 || preset >= kLookCount) return;
    l.preset = preset;
    l.accent = kPresets[preset].accent;
    l.background = kPresets[preset].background;
    l.panel = kPresets[preset].panel;
    l.text = kPresets[preset].text;
}

#include "ui3.inl"

UiActions uiDraw(UiModel& m) {

    UiActions act;
    gUi = &m;
    gAct = &act;
    const bool picture = m.pictureTexture != 0 && m.pictureW > 0 && m.pictureH > 0;
    applyLook(m.cfg.look, picture);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (picture) {
        // The picture fills the window (cropped, never stretched), under a wash of the background
        // colour so text on top stays readable.
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        const ImVec2 a = vp->Pos, b(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
        const float sx = vp->Size.x / m.pictureW, sy = vp->Size.y / m.pictureH, sc = std::max(sx, sy);
        const float u = (vp->Size.x / sc) / m.pictureW, v = (vp->Size.y / sc) / m.pictureH;
        const ImVec2 uv0((1.f - u) * 0.5f, (1.f - v) * 0.5f), uv1(uv0.x + u, uv0.y + v);
        bg->AddImage(ImTextureRef(ImTextureID(m.pictureTexture)), a, b, uv0, uv1);
        bg->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(kBg.x, kBg.y, kBg.z, 1.f - m.cfg.look.pictureShow)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    }
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const bool framed = !m.captionsOnly && !m.mini && m.setupStep < 0;  // sidebar and pages
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, framed ? ImVec2(0, 0) : ImVec2(em() * 1.1f, em() * 0.8f));
    ImGui::Begin("root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    if (picture) ImGui::PopStyleColor();  // only the root window is see-through; popups keep their colour

    const bool pictureOpen = m.bigPicture >= 0;  // a picture shown big: nothing underneath reacts to clicks
    if (pictureOpen) ImGui::BeginDisabled();
    if (m.captionsOnly) {
        captionsOnlyView(m, act);
    } else if (m.mini) {
        miniView(m, act);
    } else if (m.setupStep >= 0) {
        setupView(m, act);
    } else {
        appFrame(m, act, picture);
    }
    if (m.checkOpen && !m.captionsOnly) checkWindow(m, act);
    noticeToast(m);

    findWindow(m);
    usageNoticeWindow(m, m.setupStep >= 0 || m.captionsOnly);
    changesWindow(m);
    if (pictureOpen) ImGui::EndDisabled();
    guideWindow(m, act);
    screenNoticeWindow(m);
    screenPreviewWindow(m, act);
    bigPictureWindow(m);
    ImGui::End();
    return act;
}
