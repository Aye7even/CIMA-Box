#include "ui.h"

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
const ImVec4 kBg = rgb(0x161A23);          // window
const ImVec4 kPanel = rgb(0x1F2530);       // the three stages and the status strip
const ImVec4 kField = rgb(0x2A3140);       // inputs, buttons
const ImVec4 kFieldHover = rgb(0x333B4D);
const ImVec4 kFieldActive = rgb(0x3D465B);
const ImVec4 kLine = rgb(0x343C4E);
const ImVec4 kText = rgb(0xE9EDF5);
const ImVec4 kMuted = rgb(0x93A0B8);
const ImVec4 kSignal = rgb(0xF2B04A);      // amber = live signal: meter, switches, the selected option
const ImVec4 kSignalHover = rgb(0xF7C069);
const ImVec4 kSignalDown = rgb(0xD99A38);
const ImVec4 kOnSignal = rgb(0x1B1405);    // text on amber
const ImVec4 kBusy = rgb(0x8AB4FF);
const ImVec4 kDanger = rgb(0xFF7B7B);
const ImVec4 kVoices[] = {kSignal, kBusy, rgb(0x7FD6A4), rgb(0xE59AD0)};  // speakers inside one caption

ImFont* gSemibold = nullptr;

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

void fieldLabel(const char* text) {
    gap(0.1f);
    ImGui::TextColored(kMuted, "%s", shown(text).c_str());
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
    ImGui::PushID(label);
    const float h = ImGui::GetFrameHeight(), w = ImGui::GetContentRegionAvail().x;
    const float sh = std::floor(em() * 1.2f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
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
    ImGui::TextColored(kMuted, "%s", shown(label).c_str());
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
    sliderHeader(label, buf);
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
    sliderHeader(label, buf);
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

void voiceCombo(UiModel& m) {
    Settings& c = m.cfg;
    std::string current = c.voiceId.empty() ? T("Pick a voice") : c.voiceId;
    for (const Voice& v : m.voices)
        if (v.id == c.voiceId) current = v.name;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##voice", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", T("Search voices"), &m.voiceFilter);
        for (const Voice& v : m.voices) {
            if (!m.voiceFilter.empty() && !containsNoCase(v.name, m.voiceFilter)) continue;
            ImGui::PushID(v.id.c_str());
            if (ImGui::Selectable(v.name.c_str(), v.id == c.voiceId)) c.voiceId = v.id;
            if (!v.category.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(kMuted, "%s", v.category.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
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

// Shared by both pages: which service turns text into the other language.
void translationService(Settings& c, bool forCaptions) {
    fieldLabel("Translation service");
    static const char* const kServices[] = {"Free", "DeepL", "Claude", "Gemini"};
    segmented("##service", kServices, 4, &c.translateService);
    const bool ai = c.translateService == kClaude || c.translateService == kGemini;
    // The key can be pasted right here the first time; after that it lives in Settings.
    std::string* key = c.translateService == kDeepL ? &c.deeplKey : c.translateService == kClaude ? &c.claudeKey
                     : c.translateService == kGemini ? &c.geminiKey : nullptr;
    if (key && key->empty()) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##servicekey", c.translateService == kDeepL ? T("Paste your DeepL API key")
                                                 : c.translateService == kClaude ? T("Paste your Claude API key (console.anthropic.com)")
                                                                                 : T("Paste your Gemini API key (aistudio.google.com)"),
                                 key, ImGuiInputTextFlags_Password);
    }
    if (c.translateService == kMyMemory)
        note(forCaptions ? "MyMemory, no sign-up. Its 5,000 characters a day last only minutes of steady captions."
                         : "MyMemory, no sign-up. About 5,000 characters a day, and rougher wording.");
    else if (c.translateService == kDeepL)
        note("Accurate, textbook wording. A free DeepL API key covers 500,000 characters a month.");
    else
        note(forCaptions ? "An AI that understands slang, so captions read the way people meant them."
                         : "An AI that says it the way a native speaker would, slang included. If it takes longer than the "
                           "time limit, that phrase uses DeepL (or Free) instead.");
    if (!ai) return;

    if (ai && c.deeplKey.empty())
        note("Tip: add a DeepL key in Settings too. It steps in when the AI is slow.");
    if (!forCaptions) {
        fieldLabel("How you talk");
        static const char* const kLevels[] = {"Polite", "Casual", "Very casual"};
        segmented("##level", kLevels, 3, &c.speechLevel);
        note(c.speechLevel == kLevelPolite ? "Polite but natural, as with someone you just met."
             : c.speechLevel == kLevelCasual ? "Like talking with friends: casual speech level, everyday slang."
                                             : "Close friends gaming together: lots of slang, swearing kept.");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##persona", T("Sound like... (optional)"), &c.persona);
        if (c.persona.empty()) note("For example: a guy in his 20s from Seoul. Region and age change the slang.");
        toggleRow("Match how they talk to me", &c.mirror);
        note(c.mirror ? "Uses the last few lines from the Listen captions, so you answer at their level and pick up "
                        "their words. Needs Listen on."
                      : "Translates each phrase on its own.");
    }
    float seconds = c.aiTimeoutMs / 1000.f;
    if (sliderF("Time limit", &seconds, 0.8f, 4.f, "%.1f s")) c.aiTimeoutMs = int(seconds * 1000.f + 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    const bool open = ImGui::TreeNodeEx(T(forCaptions ? "AI model##listenmodel" : "AI model##voicemodel"),
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor(2);
    if (open) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (c.translateService == kClaude) ImGui::InputText("##claudemodel", &c.claudeModel);
        else ImGui::InputText("##geminimodel", &c.geminiModel);
        note(c.translateService == kClaude ? "claude-haiku-5-5 is the fastest. claude-sonnet-5-5 words things a bit better, but is slower."
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
    const bool ai = c.translateService == kClaude || c.translateService == kGemini;
    if (!ai && (custom || cur))
        note("Dialects need Claude or Gemini. DeepL and Free use the standard language (DeepL knows a few regional "
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
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();
    heading(title, 1.15f);
    gap(0.05f);
}

// A smaller heading inside a panel, with a little room above it.
void subheading(const char* text) {
    gap(0.5f);
    heading(text, 1.0f);
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
        sliderI("Pause that ends a phrase", &c.holdMs, 150, 1500, "%d ms");
        note("Shorter replies sooner, but splits sentences at small pauses.");
    } else {
        fieldLabel("Key");
        keyBinder(m, 0, c.pttKey);
        note(kGlobalHotkey ? "Works while another window has focus. Recording runs a moment past release so the last word isn't cut."
                           : "Works while this window has focus. Recording runs a moment past release so the last word isn't cut.");
    }
    sliderI("Longest phrase", &c.maxSeconds, 3, 30, "%d s");
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
            note("Add your ElevenLabs API key in Settings (the gear at the top right) to see your voices.");
        } else {
            ImGui::BeginDisabled(m.loading);
            if (button(m.loading ? "Loading voices" : "Load voices and models", true)) act.loadCatalog = true;
            ImGui::EndDisabled();
        }
    }
    if (c.engine == kTranslate) {
        toggleRow("A voice for each language", &c.voicePerLanguage);
        if (c.voicePerLanguage) note("Picking another language brings back the voice you last used with it.");
    }

    fieldLabel("Model");
    modelCombo(m.models, c.engine == kVoiceChanger, c.engine == kVoiceChanger ? c.stsModel : c.ttsModel);

    if (c.engine != kVoiceChanger) {
        fieldLabel("I speak");
        languageCombo("##spoken", c.spokenLang, true);
    }
    if (c.engine == kTranslate) {
        fieldLabel("Translate to");
        languageCombo("##target", c.targetLang, false, c.ttsModel);
        dialectPicker(c);
        translationService(c, false);
        if (c.translateService == kClaude || c.translateService == kGemini) {
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
                     "sentence was said (excited, flat, whispered...). Adds no delay.");
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
    sliderF("Volume##outvol", &c.outVolume, 0.f, 2.f, "%.0f%%", 100.f);

    gap(0.1f);
    if (toggleRow("Hear it yourself too", &c.monitor)) act.reopenMonitor = true;
    if (c.monitor) {
        if (deviceCombo("##mon", m.outputDevices, c.monDevice)) act.reopenMonitor = true;
        sliderF("Volume##monvol", &c.monVolume, 0.f, 2.f, "%.0f%%", 100.f);
    }

    gap(0.4f);
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

    gap(0.4f);
    toggleRow("Show in VRChat chatbox", &c.chatbox);
    if (c.chatbox) {
        static const char* const kShow[] = {"What I said", "Spoken as", "Both"};
        segmented("##chatboxshow", kShow, 3, &c.chatboxShow);
        note(c.engine == kVoiceChanger ? "Changer mode doesn't write anything down, so this also transcribes each phrase "
                                         "(at the same time as the voice, so no added delay, but it is billed)."
                                       : "Needs OSC turned on in VRChat. At most one message every 1.6 seconds.");
    }
    gap(0.1f);
    if (button("Rescan devices")) act.rescanDevices = true;
}

// ---------------------------------------------------------------- header and status strip

void header(UiModel& m) {
    const float h = em() * 2.3f, w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float x = p.x;
    if (m.logoTexture != 0 && m.logoSize > 0) {
        const float side = std::floor(std::min(m.logoSize, h * 0.92f));
        const ImVec2 a(x, std::floor(p.y + (h - side) * 0.5f));
        dl->AddImageRounded(ImTextureRef(ImTextureID(m.logoTexture)), a, ImVec2(a.x + side, a.y + side), ImVec2(0, 0),
                            ImVec2(1, 1), IM_COL32_WHITE, side * 0.22f);
        x += side + em() * 0.6f;
    }
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.45f);
    const float nameY = p.y + (h - ImGui::GetFontSize()) * 0.5f, nameSize = ImGui::GetFontSize();
    dl->AddText(ImVec2(x, nameY), col(kText), "CIMA Box");
    x += ImGui::CalcTextSize("CIMA Box").x + em() * 0.45f;
    ImGui::PopFont();
    // Version, small and on the same baseline as the name.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.95f);
    dl->AddText(ImVec2(x, nameY + nameSize - ImGui::GetFontSize() - em() * 0.12f), col(kMuted), CIMA_VERSION);
    x += ImGui::CalcTextSize(CIMA_VERSION).x + em() * 1.1f;
    ImGui::PopFont();

    // The two halves of the app: changing your own voice, and reading what others say.
    ImGui::SetCursorScreenPos(ImVec2(x, std::floor(p.y + (h - ImGui::GetFrameHeight()) * 0.5f)));
    static const char* const kPages[] = {"My voice", "Listen", "VR"};
    float tabW = 0.f;  // as wide as the longest name needs, in whatever language
    for (const char* pg : kPages) tabW = std::max(tabW, ImGui::CalcTextSize(shown(pg).c_str()).x);
    const ImVec2 tabsAt = ImGui::GetCursorScreenPos();
    const float tabsW = (tabW + em() * 1.8f) * 3.f + em() * 0.4f;
    segmented("##page", kPages, 3, &m.cfg.page, tabsW);

    // A new version: a small amber button next to the tabs, to Settings where it installs.
    if (m.update.stage == kUpdateAvailable || m.update.stage == kUpdateDownloading || m.update.stage == kUpdateReady) {
        char label[160];
        std::snprintf(label, sizeof(label), TF("Update to %s"), m.update.latest.c_str());
        ImGui::SetCursorScreenPos(ImVec2(tabsAt.x + tabsW + em() * 0.9f, tabsAt.y));
        if (button((std::string(label) + "##updatepill").c_str(), true, ImGui::CalcTextSize(label).x + em() * 1.6f))
            m.cfg.page = kPageSettings;
    }

    // Settings: a gear at the far right.
    const float gs = std::floor(h * 0.62f);
    const ImVec2 gp(p.x + w - gs, p.y + (h - gs) * 0.5f);
    ImGui::SetCursorScreenPos(gp);
    const bool onSettings = m.cfg.page == kPageSettings;
    static int lastPage = kPageVoice;
    if (!onSettings) lastPage = m.cfg.page;
    if (ImGui::InvisibleButton("##settings", ImVec2(gs, gs))) m.cfg.page = onSettings ? lastPage : kPageSettings;
    {
        const bool hot = ImGui::IsItemHovered();
        if (hot) ImGui::SetTooltip("%s", T(onSettings ? "Back" : "Settings"));
        const ImU32 gc = col(onSettings ? kSignal : hot ? kText : kMuted);
        const ImVec2 c(gp.x + gs * 0.5f, gp.y + gs * 0.5f);
        const float ro = gs * 0.5f, ri = gs * 0.36f;
        for (int i = 0; i < 8; ++i) {  // teeth
            const float a = float(i) * 3.14159265f / 4.f, ca = std::cos(a), sa = std::sin(a), tw = gs * 0.11f;
            const ImVec2 d(ca, sa), n(-sa, ca);
            auto at = [&](float r, float t) { return ImVec2(c.x + d.x * r + n.x * t, c.y + d.y * r + n.y * t); };
            dl->AddQuadFilled(at(ri * 0.8f, -tw), at(ro, -tw * 0.8f), at(ro, tw * 0.8f), at(ri * 0.8f, tw), gc);
        }
        dl->AddCircleFilled(c, ri, gc, 32);
        dl->AddCircleFilled(c, gs * 0.15f, col(kBg), 24);
    }
    if (onSettings) {
        const char* title = T("Settings");
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.1f);
        const ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(gp.x - em() * 0.8f - ts.x, p.y + (h - ts.y) * 0.5f), col(kText), title);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        ImGui::Dummy(ImVec2(w, em() * 0.35f));
        return;
    }

    // State and on/off switch of the half that is showing.
    const bool listen = m.cfg.page == kPageListen, vrPageOn = m.cfg.page == kPageVR;
    bool& on = vrPageOn ? m.cfg.vr.on : listen ? m.cfg.listen.on : m.cfg.enabled;
    const char* state = "Off";
    ImVec4 color = kMuted;
    if (vrPageOn) {
        if (on && m.vr.visible) { state = "Showing"; color = kSignal; }
        else if (on && m.vr.connected) { state = "Connected"; color = kText; }
        else if (on) { state = "Waiting for SteamVR"; color = kBusy; }
    } else if (on && listen) {
        if (!m.listen.error.empty()) { state = "Problem"; color = kDanger; }
        else if (m.listen.hearing) { state = "Hearing speech"; color = kSignal; }
        else if (m.listen.busy) { state = "Translating"; color = kBusy; }
        else { state = "Listening"; color = kText; }
    } else if (!listen && !vrPageOn && !on && m.passing) {
        state = "Your mic";
        color = kText;
    } else if (on) {
        if (m.recording) { state = "Recording"; color = kSignal; }
        else if (m.busy) { state = "Converting"; color = kBusy; }
        else { state = "Listening"; color = kText; }
    }
    state = T(state);
    const float sh = std::floor(em() * 1.5f), sw = sh * 1.8f;
    const ImVec2 sp(gp.x - em() * 1.1f - sw, p.y + (h - sh) * 0.5f);
    ImGui::SetCursorScreenPos(sp);
    if (ImGui::InvisibleButton("##master", ImVec2(sw, sh))) on = !on;
    drawSwitch(sp, sh, on, ImGui::IsItemHovered(), ImGui::GetID(vrPageOn ? "##vranim" : listen ? "##listenanim" : "##masteranim"));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", T(vrPageOn ? (on ? "Stop showing captions in VR" : "Show captions in VR")
                                  : listen ? (on ? "Stop captioning" : "Start captioning")
                                           : (on ? "Turn the voice changer off" : "Turn the voice changer on")));

    const ImVec2 ts = ImGui::CalcTextSize(state);
    const float tx = sp.x - em() * 0.9f - ts.x, ty = p.y + (h - ts.y) * 0.5f;
    dl->AddText(ImVec2(tx, ty), col(color), state);
    dl->AddCircleFilled(ImVec2(tx - em() * 0.7f, p.y + h * 0.5f), em() * 0.26f, col(color), 16);

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, em() * 0.35f));
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
    add(T("transcribe"), m.sttMs);
    add(m.translateVia.empty() ? T("translate") : (std::string(T("translate")) + " (" + m.translateVia + ")").c_str(), m.translateMs);
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

void statusStrip(UiModel& m, UiActions& act, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.05f, em() * 0.7f));
    ImGui::BeginChild("status", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    const float buttonW = em() * 8.5f;
    const float leftW = ImGui::GetContentRegionAvail().x - buttonW - em();
    if (m.firstRun) {  // nothing works without the ElevenLabs key: say how to start
        ImGui::BeginChild("lines", ImVec2(leftW, 0), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
        ImGui::TextUnformatted(T("Welcome to CIMA Box. Three steps to start:"));
        ImGui::PopFont();
        ImGui::TextColored(kMuted, "%s", T("1. Add your ElevenLabs API key in Settings (the gear, top right)."));
        ImGui::TextColored(kMuted, "%s", T("2. Pick your microphone under Input.   3. Pick where the voice goes under Output."));
        ImGui::EndChild();
        ImGui::SameLine(0, em());
        ImGui::BeginGroup();
        gap(0.15f);
        if (button("Open settings", true, buttonW)) m.cfg.page = kPageSettings;
        ImGui::EndGroup();
        ImGui::EndChild();
        return;
    }
    ImGui::BeginChild("lines", ImVec2(leftW, 0), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    if (!m.lastError.empty()) statusLine("Problem", T(m.lastError), kDanger);
    else if (!m.heardText.empty()) statusLine("You said", m.heardText, kText);
    else ImGui::TextColored(kMuted, "%s", T(m.cfg.engine == kVoiceChanger ? "Changer mode sends your voice straight through, without text."
                                                                           : "What you say will show up here."));
    if (m.cfg.engine != kVoiceChanger && !m.sentText.empty() && m.lastError.empty()) statusLine("Spoken as", m.sentText, kSignal);
    if (m.cfg.engine != kVoiceChanger && m.cfg.emotion && !m.delivery.empty()) ImGui::TextColored(kMuted, "%s", m.delivery.c_str());
    ImGui::TextColored(kMuted, "%s", timingLine(m).c_str());
    ImGui::EndChild();

    ImGui::SameLine(0, em());
    ImGui::BeginGroup();
    gap(0.15f);
    if (button("Stop speaking", false, buttonW)) act.stopSpeaking = true;
    if (m.queued > 0) ImGui::TextColored(kMuted, TF("%d waiting"), m.queued);
    ImGui::EndGroup();
    ImGui::EndChild();
}

void chevron(ImDrawList* dl, ImVec2 c, float s, ImU32 color) {
    dl->AddTriangleFilled(ImVec2(c.x - s * 0.45f, c.y - s), ImVec2(c.x - s * 0.45f, c.y + s), ImVec2(c.x + s * 0.55f, c.y), color);
}

// ---------------------------------------------------------------- Listen: captions for other people

std::string dots() { return std::string(size_t(ImGui::GetTime() * 3.0) % 4, '.'); }

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

void appCombo(UiModel& m, UiActions& act) {
    ListenSettings& l = m.cfg.listen;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##app", l.app.empty() ? T("Pick an application") : l.app.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) act.refreshApps = true;  // the list arrives on the next frame
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##appname", T("Or type its file name, like VRChat.exe"), &l.app);
        for (const AppInfo& a : m.apps) {
            ImGui::PushID(int(a.pid));
            if (ImGui::Selectable(a.exe.c_str(), a.exe == l.app)) l.app = a.exe;
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "%s", a.title.c_str());
            ImGui::PopID();
        }
        if (m.apps.empty())
            ImGui::TextColored(kMuted, "%s", T(kAppCaptureSupported ? "No open programs found." : "Not available on this system."));
        ImGui::EndCombo();
    }
}

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
    heading("Translation", 1.15f);
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

void captionLine(const CaptionLine& line, float fontSize, bool showOriginal) {
    const bool tagged = line.speaker >= 0;
    const ImVec4 color = tagged ? kVoices[line.speaker % 4] : kText;
    const float indent = em() * 0.9f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (tagged) ImGui::Indent(indent);
    ImGui::BeginGroup();
    if (tagged) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
        ImGui::TextColored(color, TF("Voice %d"), line.speaker + 1);
        ImGui::PopFont();
    }
    ImGui::PushFont(nullptr, fontSize);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(line.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (showOriginal && !line.original.empty()) {
        ImGui::PushFont(nullptr, std::max(ImGui::GetStyle().FontSizeBase * 0.9f, fontSize * 0.72f));
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(line.original.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    if (tagged) {
        ImGui::Unindent(indent);
        const float bar = std::max(2.f, std::floor(em() * 0.2f));
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start.x, start.y + 2), ImVec2(start.x + bar, ImGui::GetItemRectMax().y - 2),
                                                  col(color), bar * 0.5f);
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
    for (const Caption& c : m.captions) {
        ImGui::PushID(int(c.id & 0x7FFFFFFF));
        ImGui::PushFont(nullptr, base * 0.85f);
        ImGui::TextColored(kMuted, "%s", c.pending ? T("Just heard") : c.language.c_str());
        const float timeW = ImGui::CalcTextSize(c.time.c_str()).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - timeW));
        ImGui::TextColored(kMuted, "%s", c.time.c_str());
        ImGui::PopFont();
        if (c.pending) ImGui::TextColored(kBusy, TF("Translating%s"), dots().c_str());
        for (const CaptionLine& line : c.lines) captionLine(line, base * scale, l.showOriginal);
        gap(0.55f);
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
    heading("Captions", 1.15f);
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
    heading("Show", 1.15f);
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
    subheading("About");
    char b[320];
    std::snprintf(b, sizeof(b), TF("CIMA Box %s"), CIMA_VERSION);
    ImGui::TextUnformatted(b);
    if (button("Version history")) m.showChanges = 2;
    note("Settings save by themselves, here:");
    note(m.settingsPath);
    if (button("Open settings folder")) act.openSettingsFolder = true;
    note("That file holds your API keys: don't share it.");
    if (!m.settingsMovedFrom.empty()) {
        note("Your settings were copied here from the program's folder. The old cimabox.json there isn't used "
             "anymore; you can delete it.", kBusy);
        note(m.settingsMovedFrom, kBusy);
    }
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + colW + gapW, origin.y));
    beginPanel("setkeys", ImVec2(colW, avail.y), "Accounts");
    apiKeyField("ElevenLabs API key", "##apikey", c.apiKey, m.showKey,
                "Needed for everything. elevenlabs.io: your profile, then API keys.");
    ImGui::BeginDisabled(m.loading || c.apiKey.empty());
    if (button(m.loading ? "Loading voices" : "Load voices and models", true)) act.loadCatalog = true;
    ImGui::EndDisabled();
    if (!m.catalogMsg.empty()) note(T(m.catalogMsg));
    apiKeyField("Claude API key", "##claudekey", c.claudeKey, m.showKey,
                "For natural translation with slang. console.anthropic.com: API keys.");
    apiKeyField("Gemini API key", "##geminikey", c.geminiKey, m.showKey,
                "The other AI choice. aistudio.google.com: Get API key.");
    apiKeyField("DeepL API key", "##deeplkey", c.deeplKey, m.showKey,
                "Accurate textbook translation, and the backup when the AI is slow. deepl.com: API plan.");
    gap(0.2f);
    toggleRow("Show keys", &m.showKey);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + (colW + gapW) * 2, origin.y));
    beginPanel("setkeys2", ImVec2(avail.x - (colW + gapW) * 2, avail.y), "Hotkeys");
    fieldLabel("Push to talk");
    keyBinder(m, 0, c.pttKey);
    note("Used when Input is set to push to talk.");
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
    note("Used by Claude and Gemini when translating, for languages whose words change with gender.");
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
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- My voice: the three stages

void voicePage(UiModel& m, UiActions& act) {
    // The three panels follow the audio: microphone -> voice -> output.
    const bool fourLines = m.cfg.engine != kVoiceChanger && m.cfg.emotion;
    const float gapW = em() * 1.6f, stripH = em() * (fourLines ? 7.0f : 5.3f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bodyH = std::max(em() * 8.f, avail.y - stripH - ImGui::GetStyle().ItemSpacing.y);
    const float colW = std::floor((avail.x - gapW * 2) / 3.f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    beginPanel("input", ImVec2(colW, bodyH), "Input");
    inputPanel(m, act);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + colW + gapW, origin.y));
    beginPanel("voice", ImVec2(colW, bodyH), "Voice");
    voicePanel(m, act);
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(origin.x + (colW + gapW) * 2, origin.y));
    beginPanel("output", ImVec2(colW, bodyH), "Output");
    outputPanel(m, act);
    ImGui::EndChild();

    // Arrows between the stages light up while sound is moving through them.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = origin.y + bodyH * 0.5f, s = em() * 0.42f;
    chevron(dl, ImVec2(origin.x + colW + gapW * 0.5f, cy), s, col(m.recording ? kSignal : kLine));
    chevron(dl, ImVec2(origin.x + colW * 2 + gapW * 1.5f, cy), s, col(m.busy ? kSignal : kLine));

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + bodyH + ImGui::GetStyle().ItemSpacing.y));
    statusStrip(m, act, stripH);
}

}  // namespace

// ---------------------------------------------------------------- public

void uiSetup(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing = ImVec2(10, 7);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.WindowRounding = 0;
    s.ChildRounding = 10;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.GrabRounding = 6;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.GrabMinSize = 14;
    s.ScrollbarSize = 10;
    s.DisabledAlpha = 0.4f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = kPanel;
    c[ImGuiCol_PopupBg] = rgb(0x242B38);
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
    c[ImGuiCol_TextSelectedBg] = rgb(0xF2B04A, 0.35f);
    c[ImGuiCol_NavCursor] = kSignal;

    s.ScaleAllSizes(scale);
    s.FontSizeBase = 16.0f;
    s.FontScaleDpi = scale;
    loadFonts();
}

UiActions uiDraw(UiModel& m) {
    UiActions act;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.1f, em() * 0.8f));
    ImGui::Begin("root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    if (m.captionsOnly) {
        captionsOnlyView(m, act);
    } else {
        header(m);
        if (m.cfg.page == kPageListen) listenPage(m, act);
        else if (m.cfg.page == kPageVR) vrPage(m, act);
        else if (m.cfg.page == kPageSettings) settingsPage(m, act);
        else voicePage(m, act);
    }

    changesWindow(m);
    ImGui::End();
    return act;
}
