// CIMA Box 3.0: the sidebar, each page's title bar, the Home page, My voice's layout and the mini
// window. Included into ui.cpp's anonymous namespace (it uses its widgets and colours).

// ---------------------------------------------------------------- icons
// Simple line icons drawn with the draw list, in a box of side `s` centred on `c`.

enum IconKind {
    kIcHome, kIcMic, kIcText, kIcHeadphones, kIcVR, kIcGrid, kIcSliders, kIcMini, kIcWave, kIcFace, kIcChat,
    kIcSpeaker, kIcScreen, kIcCaptions, kIcBookmark, kIcSwap, kIcClock, kIcBook, kIcStar, kIcBeep, kIcCancel, kIcWords,
    kIcCheck, kIcBars
};

void drawIcon(ImDrawList* dl, int kind, ImVec2 c, float s, ImU32 color) {
    const float t = std::max(1.3f, s * 0.085f);
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 0.5f) * s, c.y + (y - 0.5f) * s); };
    auto L = [&](float x1, float y1, float x2, float y2) { dl->AddLine(P(x1, y1), P(x2, y2), color, t); };
    auto R = [&](float x1, float y1, float x2, float y2, float r) { dl->AddRect(P(x1, y1), P(x2, y2), color, r * s, 0, t); };
    const float pi = 3.14159265f;
    switch (kind) {
    case kIcHome:
        L(0.12f, 0.5f, 0.5f, 0.16f); L(0.5f, 0.16f, 0.88f, 0.5f);
        L(0.24f, 0.42f, 0.24f, 0.86f); L(0.76f, 0.42f, 0.76f, 0.86f); L(0.24f, 0.86f, 0.76f, 0.86f);
        break;
    case kIcMic:
        R(0.38f, 0.1f, 0.62f, 0.58f, 0.12f);
        dl->PathArcTo(P(0.5f, 0.46f), 0.27f * s, 0.f, pi, 16); dl->PathStroke(color, 0, t);
        L(0.5f, 0.73f, 0.5f, 0.9f);
        break;
    case kIcText:
        L(0.15f, 0.25f, 0.85f, 0.25f); L(0.15f, 0.5f, 0.65f, 0.5f); L(0.15f, 0.75f, 0.5f, 0.75f);
        break;
    case kIcHeadphones:
        dl->PathArcTo(P(0.5f, 0.58f), 0.36f * s, pi, 2 * pi, 18); dl->PathStroke(color, 0, t);
        R(0.12f, 0.56f, 0.28f, 0.86f, 0.06f); R(0.72f, 0.56f, 0.88f, 0.86f, 0.06f);
        break;
    case kIcVR:
        R(0.08f, 0.3f, 0.92f, 0.72f, 0.12f);
        L(0.42f, 0.72f, 0.5f, 0.6f); L(0.5f, 0.6f, 0.58f, 0.72f);
        break;
    case kIcGrid:
        R(0.14f, 0.14f, 0.44f, 0.44f, 0.06f); R(0.56f, 0.14f, 0.86f, 0.44f, 0.06f);
        R(0.14f, 0.56f, 0.44f, 0.86f, 0.06f); R(0.56f, 0.56f, 0.86f, 0.86f, 0.06f);
        break;
    case kIcSliders:
        L(0.12f, 0.3f, 0.88f, 0.3f); L(0.12f, 0.7f, 0.88f, 0.7f);
        dl->AddCircleFilled(P(0.66f, 0.3f), 0.1f * s, color, 12); dl->AddCircleFilled(P(0.34f, 0.7f), 0.1f * s, color, 12);
        break;
    case kIcMini:
        R(0.1f, 0.18f, 0.9f, 0.82f, 0.08f);
        dl->AddRectFilled(P(0.52f, 0.52f), P(0.82f, 0.74f), color, 0.04f * s);
        break;
    case kIcWave:
        L(0.08f, 0.5f, 0.24f, 0.5f); L(0.24f, 0.5f, 0.36f, 0.18f); L(0.36f, 0.18f, 0.52f, 0.84f);
        L(0.52f, 0.84f, 0.64f, 0.5f); L(0.64f, 0.5f, 0.92f, 0.5f);
        break;
    case kIcFace:
        dl->AddCircle(P(0.5f, 0.5f), 0.4f * s, color, 24, t);
        dl->PathArcTo(P(0.5f, 0.5f), 0.22f * s, 0.2f * pi, 0.8f * pi, 12); dl->PathStroke(color, 0, t);
        dl->AddCircleFilled(P(0.36f, 0.4f), 0.05f * s, color, 8); dl->AddCircleFilled(P(0.64f, 0.4f), 0.05f * s, color, 8);
        break;
    case kIcChat:
        R(0.12f, 0.16f, 0.88f, 0.66f, 0.1f);
        L(0.3f, 0.66f, 0.22f, 0.86f); L(0.22f, 0.86f, 0.48f, 0.66f);
        break;
    case kIcSpeaker:
        L(0.14f, 0.38f, 0.3f, 0.38f); L(0.3f, 0.38f, 0.5f, 0.2f); L(0.5f, 0.2f, 0.5f, 0.8f);
        L(0.5f, 0.8f, 0.3f, 0.62f); L(0.3f, 0.62f, 0.14f, 0.62f); L(0.14f, 0.62f, 0.14f, 0.38f);
        dl->PathArcTo(P(0.5f, 0.5f), 0.2f * s, -0.3f * pi, 0.3f * pi, 10); dl->PathStroke(color, 0, t);
        dl->PathArcTo(P(0.5f, 0.5f), 0.36f * s, -0.3f * pi, 0.3f * pi, 12); dl->PathStroke(color, 0, t);
        break;
    case kIcScreen:
        R(0.1f, 0.16f, 0.9f, 0.66f, 0.06f);
        L(0.5f, 0.66f, 0.5f, 0.82f); L(0.32f, 0.84f, 0.68f, 0.84f);
        break;
    case kIcCaptions:
        R(0.1f, 0.2f, 0.9f, 0.8f, 0.1f);
        L(0.24f, 0.46f, 0.44f, 0.46f); L(0.54f, 0.46f, 0.76f, 0.46f); L(0.24f, 0.62f, 0.76f, 0.62f);
        break;
    case kIcBookmark:
        L(0.26f, 0.12f, 0.74f, 0.12f); L(0.74f, 0.12f, 0.74f, 0.88f); L(0.74f, 0.88f, 0.5f, 0.68f);
        L(0.5f, 0.68f, 0.26f, 0.88f); L(0.26f, 0.88f, 0.26f, 0.12f);
        break;
    case kIcSwap:
        L(0.14f, 0.36f, 0.84f, 0.36f); L(0.84f, 0.36f, 0.68f, 0.2f);
        L(0.86f, 0.64f, 0.16f, 0.64f); L(0.16f, 0.64f, 0.32f, 0.8f);
        break;
    case kIcClock:
        dl->AddCircle(P(0.5f, 0.5f), 0.4f * s, color, 24, t);
        L(0.5f, 0.5f, 0.5f, 0.26f); L(0.5f, 0.5f, 0.68f, 0.6f);
        break;
    case kIcBook:
        R(0.12f, 0.18f, 0.5f, 0.84f, 0.04f); R(0.5f, 0.18f, 0.88f, 0.84f, 0.04f);
        break;
    case kIcStar: {
        ImVec2 pts[10];
        for (int i = 0; i < 10; ++i) {
            const float a = -pi / 2 + i * pi / 5, r = (i % 2 == 0 ? 0.42f : 0.18f) * s;
            pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + 0.04f * s + std::sin(a) * r);
        }
        dl->AddPolyline(pts, 10, color, ImDrawFlags_Closed, t);
        break;
    }
    case kIcBeep:
        L(0.5f, 0.14f, 0.5f, 0.86f); L(0.18f, 0.32f, 0.82f, 0.68f); L(0.18f, 0.68f, 0.82f, 0.32f);
        break;
    case kIcCancel:
        dl->AddCircle(P(0.5f, 0.5f), 0.4f * s, color, 24, t);
        L(0.36f, 0.36f, 0.64f, 0.64f); L(0.64f, 0.36f, 0.36f, 0.64f);
        break;
    case kIcCheck:
        dl->AddCircle(P(0.5f, 0.5f), 0.4f * s, color, 24, t);
        L(0.32f, 0.52f, 0.45f, 0.65f); L(0.45f, 0.65f, 0.7f, 0.37f);
        break;
    case kIcWords:
        R(0.1f, 0.24f, 0.9f, 0.76f, 0.12f);
        L(0.26f, 0.5f, 0.42f, 0.5f); L(0.5f, 0.5f, 0.74f, 0.5f);
        break;
    case kIcBars:  // a small bar chart
        L(0.22f, 0.86f, 0.22f, 0.5f); L(0.5f, 0.86f, 0.5f, 0.18f); L(0.78f, 0.86f, 0.78f, 0.62f);
        L(0.1f, 0.86f, 0.9f, 0.86f);
        break;
    }
}

// ---------------------------------------------------------------- what's on

// The state of the half a page belongs to, for its title bar and the sidebar's dots.
struct PageState { const char* text; ImVec4 color; };
PageState voiceState(const UiModel& m) {
    if (!m.cfg.enabled) return {m.passing ? "Your mic" : "Off", m.passing ? kText : kMuted};
    if (m.recording) return {"Recording", kSignal};
    if (m.busy) return {"Converting", kBusy};
    if (m.holding) return {"Waiting for more", kBusy};
    if (m.passing) return {"Your mic", kText};
    return {"Listening", kText};
}
PageState listenState(const UiModel& m) {
    if (!m.cfg.listen.on) return {"Off", kMuted};
    if (!m.listen.error.empty()) return {"Problem", kDanger};
    if (m.listen.hearing) return {"Hearing speech", kSignal};
    if (m.listen.busy) return {"Translating", kBusy};
    return {"Listening", kText};
}
PageState vrState(const UiModel& m) {
    if (!m.cfg.vr.on) return {"Off", kMuted};
    if (m.vr.visible) return {"Showing", kSignal};
    if (m.vr.connected) return {"Connected", kText};
    return {"Waiting for SteamVR", kBusy};
}

// ---------------------------------------------------------------- sidebar

struct NavItem { int page; const char* label; const char* sub; int icon; };
const NavItem kNav[] = {
    {kPageHome, "Home", "At a glance", kIcHome},
    {kPageVoice, "My voice", "Voice, language, dialect", kIcMic},
    {kPageText, "Text", "Type, the voice says it", kIcText},
    {kPageListen, "Listen", "Captions of others", kIcHeadphones},
    {kPageVR, "VR", "Captions in your headset", kIcVR},
    {kPageFeatures, "Extra features", "Optional add-ons", kIcGrid},
    {kPageUsage, "Usage", "Credits and costs", kIcBars},
};

// One sidebar row: icon, name, a line saying what's there, and a dot when that part is on.
bool navRow(const char* id, int icon, const char* label, const char* sub, bool current, const ImVec4* dot) {
    const float w = ImGui::GetContentRegionAvail().x, h = em() * (sub ? 2.85f : 2.2f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (current || hot) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(current ? kPanel : ImVec4(kPanel.x, kPanel.y, kPanel.z, 0.55f)), em() * 0.55f);
    if (current) dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.25f), ImVec2(p.x + em() * 0.18f, p.y + h * 0.75f), col(kSignal), em() * 0.1f);
    const ImVec4 fg = current ? kText : hot ? kText : kMuted;
    drawIcon(dl, icon, ImVec2(p.x + em() * 1.25f, p.y + (sub ? em() * 1.05f : h * 0.5f)), em() * 1.15f, col(current ? kSignal : fg));
    const float tx = p.x + em() * 2.3f, right = p.x + w - (dot ? em() * 1.3f : em() * 0.5f);
    dl->PushClipRect(ImVec2(tx, p.y), ImVec2(right, p.y + h), true);
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
    dl->AddText(ImVec2(tx, p.y + (sub ? em() * 0.45f : (h - em()) * 0.5f)), col(fg), T(label));
    ImGui::PopFont();
    if (sub) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
        dl->AddText(ImVec2(tx, p.y + em() * 1.7f), col(kMuted), T(sub));
        ImGui::PopFont();
    }
    dl->PopClipRect();
    if (dot) dl->AddCircleFilled(ImVec2(p.x + w - em() * 0.75f, p.y + (sub ? em() * 1.05f : h * 0.5f)), em() * 0.24f, col(*dot), 14);
    if (hot && sub) ImGui::SetTooltip("%s", T(sub));
    gap(0.1f);
    return pressed;
}

void sidebar(UiModel& m, UiActions& act) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Logo, name, version.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float side = std::floor(em() * 2.3f);
        float x = p.x + em() * 0.3f;
        if (m.logoTexture != 0 && m.logoSize > 0) {
            dl->AddImageRounded(ImTextureRef(ImTextureID(m.logoTexture)), ImVec2(x, p.y), ImVec2(x + side, p.y + side), ImVec2(0, 0),
                                ImVec2(1, 1), IM_COL32_WHITE, side * 0.25f);
            x += side + em() * 0.6f;
        }
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.15f);
        dl->AddText(ImVec2(x, p.y + side * 0.5f - ImGui::GetFontSize() * 0.92f), col(kText), "CIMA Box");
        ImGui::PopFont();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
        dl->AddText(ImVec2(x, p.y + side * 0.5f + em() * 0.12f), col(kMuted), CIMA_VERSION);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, side + em() * 1.0f));
    }

    for (const NavItem& n : kNav) {
        ImVec4 dot;
        bool hasDot = false;
        if (n.page == kPageVoice && m.cfg.enabled) { dot = voiceState(m).color; hasDot = true; }
        if (n.page == kPageListen && m.cfg.listen.on) { dot = listenState(m).color; hasDot = true; }
        if (n.page == kPageVR && m.cfg.vr.on) { dot = vrState(m).color; hasDot = true; }
        if (hasDot && dot.x == kMuted.x && dot.y == kMuted.y) hasDot = false;
        ImGui::PushID(n.page);
        if (navRow("##nav", n.icon, n.label, n.sub, m.cfg.page == n.page, hasDot ? &dot : nullptr)) m.cfg.page = n.page;
        ImGui::PopID();
    }

    // The bottom: what needs attention, the mini window, what's left to spend, Settings.
    float bottomH = ImGui::GetFrameHeightWithSpacing() * 1.2f + em() * 2.6f;  // mini window + Settings
    bottomH += ImGui::GetFrameHeight() * 1.15f + em() * 0.35f + ImGui::GetStyle().ItemSpacing.y;  // Join our Discord
    if (!m.usage.empty()) bottomH += em() * (1.4f + 1.25f * float(m.usage.size()));
    // The version you went back from isn't offered here again (Settings still shows it); a newer one is.
    const bool update = (m.update.stage == kUpdateAvailable && m.update.latest != m.cfg.skipUpdate) ||
                        m.update.stage == kUpdateDownloading || m.update.stage == kUpdateReady;
    if (update) bottomH += ImGui::GetFrameHeightWithSpacing();
    if (!m.capturing.empty()) bottomH += ImGui::GetFrameHeightWithSpacing();
    const float y = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - bottomH;
    if (y > ImGui::GetCursorScreenPos().y) ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, y));

    if (update) {
        char label[160];
        std::snprintf(label, sizeof(label), TF("Update to %s"), m.update.latest.c_str());
        if (button((std::string(label) + "##updatepill").c_str(), true)) m.cfg.page = kPageSettings;
    }
    if (!m.capturing.empty()) {  // screen translation is watching a window: never running unnoticed
        char label[200];
        std::snprintf(label, sizeof(label), TF("Capturing: %s"), m.capturing.c_str());
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
        if (ImGui::InvisibleButton("##capturepill", ImVec2(w, h))) m.cfg.page = kPageFeatures;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("Screen translation is watching this window. Click to change it."));
        dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(ImVec4(kDanger.x, kDanger.y, kDanger.z, 0.6f)), ImGui::GetStyle().FrameRounding);
        dl->AddCircleFilled(ImVec2(a.x + em() * 0.7f, a.y + h * 0.5f), em() * 0.24f, col(kDanger), 16);
        dl->PushClipRect(a, ImVec2(a.x + w - em() * 0.4f, a.y + h), true);
        dl->AddText(ImVec2(a.x + em() * 1.2f, a.y + (h - em()) * 0.5f), col(kText), label);
        dl->PopClipRect();
        gap(0.25f);
    }
    // The Discord server: one click from any page.
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() * 1.15f;
        if (ImGui::InvisibleButton("##discord", ImVec2(w, h))) act.openUrl = kDiscordUrl;
        const bool hot = ImGui::IsItemHovered();
        if (hot) ImGui::SetTooltip("%s", T("Questions, tips and news about updates, with other people who use CIMA Box. Opens in your browser."));
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(ImVec4(kSignal.x, kSignal.y, kSignal.z, hot ? 0.22f : 0.12f)), ImGui::GetStyle().FrameRounding);
        dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(ImVec4(kSignal.x, kSignal.y, kSignal.z, hot ? 0.9f : 0.5f)), ImGui::GetStyle().FrameRounding, 0, 1.3f);
        drawIcon(dl, kIcChat, ImVec2(a.x + em() * 1.1f, a.y + h * 0.5f), em() * 1.0f, col(kSignal));
        dl->PushClipRect(a, ImVec2(a.x + w - em() * 0.4f, a.y + h), true);
        dl->AddText(ImVec2(a.x + em() * 2.1f, a.y + (h - em()) * 0.5f), col(kText), T("Join our Discord"));
        dl->PopClipRect();
        gap(0.35f);
    }
    // The mini window: My voice at a glance while you play.
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight() * 1.15f;
        if (ImGui::InvisibleButton("##mini", ImVec2(w, h))) act.setMini = 1;
        const bool hot = ImGui::IsItemHovered();
        if (hot) ImGui::SetTooltip("%s", T("A small window that stays on top of your game: what you said, what they heard, and the switch."));
        dl->AddRect(a, ImVec2(a.x + w, a.y + h), col(hot ? kMuted : kLine), ImGui::GetStyle().FrameRounding, 0, 1.3f);
        drawIcon(dl, kIcMini, ImVec2(a.x + em() * 1.1f, a.y + h * 0.5f), em() * 1.0f, col(hot ? kSignal : kMuted));
        dl->AddText(ImVec2(a.x + em() * 2.1f, a.y + (h - em()) * 0.5f), col(hot ? kText : kMuted), T("Mini window"));
        gap(0.35f);
    }
    if (!m.usage.empty()) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = em() * (0.9f + 1.25f * float(m.usage.size()));
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(kPanel), em() * 0.55f);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.86f);
        for (size_t i = 0; i < m.usage.size(); ++i) {
            const UsageItem& u = m.usage[i];
            const float ly = a.y + em() * 0.45f + em() * 1.25f * float(i);
            ImGui::SetCursorScreenPos(ImVec2(a.x, ly - em() * 0.1f));
            ImGui::PushID(int(i));
            if (ImGui::InvisibleButton("##usage", ImVec2(w, em() * 1.2f))) m.cfg.page = kPageUsage;  // the whole picture
            const bool hot = ImGui::IsItemHovered();
            ImGui::PopID();
            const ImVec4 vc = u.level == kUsageOut ? kDanger : u.level == kUsageLow ? kSignal : u.level == kUsageUnknown ? kMuted : kText;
            dl->AddText(ImVec2(a.x + em() * 0.7f, ly), col(kMuted), u.name.c_str());
            const ImVec2 vs = ImGui::CalcTextSize(u.text.c_str());
            dl->AddText(ImVec2(std::max(a.x + em() * 0.7f + ImGui::CalcTextSize(u.name.c_str()).x + em() * 0.5f, a.x + w - em() * 0.7f - vs.x), ly), col(vc), u.text.c_str());
            if (hot && !u.detail.empty()) {
                ImGui::PopFont();
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(em() * 24.f);
                ImGui::TextUnformatted(u.detail.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.86f);
            }
        }
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h));
        gap(0.35f);
    }
    if (navRow("##navsettings", kIcSliders, "Settings", nullptr, m.cfg.page == kPageSettings, nullptr)) m.cfg.page = kPageSettings;
}

// ---------------------------------------------------------------- each page's title bar

const char* pageTitle(int page) {
    switch (page) {
    case kPageHome: return "Home";
    case kPageVoice: return "My voice";
    case kPageText: return "Text";
    case kPageListen: return "Listen";
    case kPageVR: return "VR";
    case kPageFeatures: return "Extra features";
    case kPageUsage: return "Usage";
    default: return "Settings";
    }
}

void pageHeader(UiModel& m, UiActions& act) {
    const float h = em() * 2.4f, w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.5f);
    const char* title = T(pageTitle(m.cfg.page));
    const float titleW = ImGui::CalcTextSize(title).x;
    dl->AddText(ImVec2(p.x, p.y + (h - ImGui::GetFontSize()) * 0.5f), col(kText), title);
    ImGui::PopFont();
    float leftEnd = p.x + titleW + em() * 1.2f;

    // Right to left: the switch and the state of the half this page belongs to.
    float right = p.x + w;
    if (m.cfg.page != kPageSettings) {
        const bool listen = m.cfg.page == kPageListen || m.cfg.page == kPageText, vrPage = m.cfg.page == kPageVR;
        bool& on = vrPage ? m.cfg.vr.on : listen ? m.cfg.listen.on : m.cfg.enabled;
        const PageState st = vrPage ? vrState(m) : listen ? listenState(m) : voiceState(m);
        // One button holds the state and the switch, and all of it can be clicked: a small switch on
        // its own in the corner was easy to miss, and a page left off looked like a page that was on.
        const float ph = std::floor(em() * 2.3f), sh = std::floor(em() * 1.6f), sw = sh * 1.8f, q = (ph - sh) * 0.5f;
        const char* state = T(st.text);
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.05f);
        const ImVec2 ts = ImGui::CalcTextSize(state);
        const float lead = em() * 0.85f, dotW = em() * 0.95f, between = em() * 0.8f;
        const float full = lead + dotW + ts.x + between + sw + q;
        const bool wide = right - full > leftEnd + em() * 8.f;  // too narrow for the word: the switch alone
        const float pw = wide ? full : sw + q * 2.f;
        const ImVec2 a(right - pw, p.y + (h - ph) * 0.5f), b(a.x + pw, a.y + ph);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##master", ImVec2(pw, ph))) on = !on;
        const bool hot = ImGui::IsItemHovered();
        const ImVec4 edge = on ? kSignal : kMuted;
        dl->AddRectFilled(a, b, col(on ? ImVec4(kSignal.x, kSignal.y, kSignal.z, hot ? 0.24f : 0.15f) : hot ? kFieldHover : kPanel), ph * 0.5f);
        dl->AddRect(a, b, col(ImVec4(edge.x, edge.y, edge.z, on ? (hot ? 0.9f : 0.6f) : (hot ? 0.8f : 0.45f))), ph * 0.5f, 0, 1.5f);
        if (wide) {
            dl->AddCircleFilled(ImVec2(a.x + lead + em() * 0.28f, a.y + ph * 0.5f), em() * 0.28f, col(st.color), 16);
            dl->AddText(ImVec2(a.x + lead + dotW, a.y + (ph - ts.y) * 0.5f), col(on ? st.color : kText), state);
        }
        ImGui::PopFont();
        drawSwitch(ImVec2(b.x - q - sw, a.y + q), sh, on, hot, ImGui::GetID(vrPage ? "##vranim" : listen ? "##listenanim" : "##masteranim"));
        if (hot)
            ImGui::SetTooltip("%s", T(vrPage ? (on ? "Stop showing captions in VR" : "Show captions in VR")
                                      : listen ? (on ? "Stop captioning" : "Start captioning")
                                               : (on ? "Turn the voice changer off" : "Turn the voice changer on")));
        right = a.x - em() * 0.8f;
    }

    // A service you use reports problems on its status page: say so, so it isn't mistaken for the app.
    if (const ServiceStatus* bad = troubleInUse(m)) {
        char label[160];
        std::snprintf(label, sizeof(label), TF("%s: problems"), bad->name.c_str());
        if (ImGui::CalcTextSize(label).x + em() * 2.f > right - leftEnd - em() * 12.f) std::snprintf(label, sizeof(label), "%s", bad->name.c_str());
        const ImVec2 ls = ImGui::CalcTextSize(label);
        const float pw = ls.x + em() * 2.0f, ph = ImGui::GetFrameHeight();
        const ImVec2 a(right - pw, p.y + (h - ph) * 0.5f);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##servicepill", ImVec2(pw, ph))) act.openUrl = bad->page;
        const bool hot = ImGui::IsItemHovered();
        const ImVec4 c = healthColor(bad->health);
        dl->AddRect(a, ImVec2(a.x + pw, a.y + ph), col(ImVec4(c.x, c.y, c.z, hot ? 0.9f : 0.5f)), ImGui::GetStyle().FrameRounding);
        dl->AddCircleFilled(ImVec2(a.x + em() * 0.7f, a.y + ph * 0.5f), em() * 0.24f, col(c), 16);
        dl->AddText(ImVec2(a.x + em() * 1.2f, a.y + (ph - ls.y) * 0.5f), col(hot ? kText : c), label);
        if (hot) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(em() * 26.f);
            ImGui::TextUnformatted(T(bad->detail));
            ImGui::TextColored(kMuted, "%s", T("This is on their side, not in CIMA Box. Click to open their status page."));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        right = a.x - em() * 0.8f;
    }

    // Listen is on: what it's listening to, from any page ("Listening to VRChat"). Lights up while
    // someone talks; click to open the Listen page.
    if (m.cfg.listen.on) {
        const ListenSettings& ls = m.cfg.listen;
        std::string what;
        if (ls.source == kSrcApp) {
            what = ls.app;
            if (what.size() > 4 && (what.compare(what.size() - 4, 4, ".exe") == 0 || what.compare(what.size() - 4, 4, ".EXE") == 0))
                what.resize(what.size() - 4);
            if (!what.empty() && what[0] >= 'a' && what[0] <= 'z') what[0] = char(what[0] - 32);
        } else if (ls.source == kSrcSpeakers) {
            what = ls.speakers.empty() ? std::string(T("your speakers")) : ls.speakers;
        } else {
            what = ls.input.empty() ? std::string(T("your microphone")) : ls.input;
        }
        const bool problem = !m.listen.error.empty();
        const bool waiting = !problem && m.listen.status.rfind("Waiting", 0) == 0;
        const bool hearing = !problem && !waiting && m.listen.hearing;
        const char* fmt = problem ? TF("Listen has a problem") : waiting ? TF("Waiting for %s") : TF("Listening to %s");
        const float room = right - leftEnd - em() * 12.f;  // leave the search box some room
        char label[200];
        std::string shown = what;
        for (;;) {  // shorten a long device name until it fits
            std::snprintf(label, sizeof(label), fmt, shown.c_str());
            if (ImGui::CalcTextSize(label).x + em() * 2.6f <= std::min(room, em() * 20.f) || shown.size() <= 6) break;
            size_t cut = shown.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(shown[cut]) & 0xC0) == 0x80) --cut;  // whole letters only
            shown = shown.substr(0, cut);
            while (!shown.empty() && shown.back() == ' ') shown.pop_back();
            if (shown.size() > 3 && shown.compare(shown.size() - 3, 3, "\xE2\x80\xA6") == 0) shown.resize(shown.size() - 3);
            shown += "\xE2\x80\xA6";  // …
        }
        const ImVec2 lsz = ImGui::CalcTextSize(label);
        const float pw = lsz.x + em() * 2.6f, ph = ImGui::GetFrameHeight();
        if (pw <= room) {
            const ImVec2 a(right - pw, p.y + (h - ph) * 0.5f);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##listenpill", ImVec2(pw, ph))) m.cfg.page = kPageListen;
            const bool hot = ImGui::IsItemHovered();
            const ImVec4 c = problem ? kDanger : waiting ? kBusy : hearing ? kSignal : kText;
            const float r = ImGui::GetStyle().FrameRounding;
            // While someone talks the pill glows softly, pulsing with time.
            const float pulse = hearing ? 0.10f + 0.06f * std::sin(float(ImGui::GetTime()) * 5.f) : 0.05f;
            dl->AddRectFilled(a, ImVec2(a.x + pw, a.y + ph), col(ImVec4(c.x, c.y, c.z, hot ? pulse + 0.06f : pulse)), r);
            dl->AddRect(a, ImVec2(a.x + pw, a.y + ph), col(ImVec4(c.x, c.y, c.z, hot ? 0.8f : 0.35f)), r);
            drawIcon(dl, kIcHeadphones, ImVec2(a.x + em() * 0.95f, a.y + ph * 0.5f), em() * 0.95f, col(c));
            dl->AddText(ImVec2(a.x + em() * 1.75f, a.y + (ph - lsz.y) * 0.5f), col(hot ? kText : ImVec4(c.x, c.y, c.z, 1.f)), label);
            if (hot) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(em() * 26.f);
                if (problem) ImGui::TextColored(kDanger, "%s", T(m.listen.error));
                else if (shown != what) ImGui::TextUnformatted(what.c_str());
                ImGui::TextColored(kMuted, "%s", T("Captions are on. Click to open the Listen page."));
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            right = a.x - em() * 0.8f;
        }
    }

    // Find a setting or feature: looks like a search box, opens the search (Ctrl+F too).
    {
        const float fw = std::min(em() * 17.f, right - leftEnd);
        if (fw > em() * 2.4f) {
            const float fh = ImGui::GetFrameHeight();
            const ImVec2 a(right - fw, p.y + (h - fh) * 0.5f);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##find", ImVec2(fw, fh))) m.findOpen = true;
            const bool hot = ImGui::IsItemHovered();
            dl->AddRectFilled(a, ImVec2(a.x + fw, a.y + fh), col(hot ? kFieldHover : kPanel), ImGui::GetStyle().FrameRounding);
            const ImVec2 c(a.x + em() * 0.85f, a.y + fh * 0.47f);
            const ImU32 ic = col(hot ? kText : kMuted);
            dl->AddCircle(c, em() * 0.32f, ic, 16, 1.6f);
            dl->AddLine(ImVec2(c.x + em() * 0.23f, c.y + em() * 0.23f), ImVec2(c.x + em() * 0.5f, c.y + em() * 0.5f), ic, 1.8f);
            if (fw > em() * 8.f) {
                const char* hint = T("Find a setting or feature");
                const char* keys = "Ctrl F";
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
                const ImVec2 ks = ImGui::CalcTextSize(keys);
                const ImVec2 ka(a.x + fw - ks.x - em() * 0.9f, a.y + (fh - ks.y) * 0.5f);
                dl->AddRect(ImVec2(ka.x - em() * 0.3f, ka.y - em() * 0.08f), ImVec2(ka.x + ks.x + em() * 0.3f, ka.y + ks.y + em() * 0.08f), col(kLine), em() * 0.25f);
                dl->AddText(ka, col(kMuted), keys);
                ImGui::PopFont();
                dl->PushClipRect(a, ImVec2(ka.x - em() * 0.6f, a.y + fh), true);
                dl->AddText(ImVec2(a.x + em() * 1.6f, a.y + (fh - em()) * 0.5f), ic, hint);
                dl->PopClipRect();
            }
            if (hot) ImGui::SetTooltip("%s", T("Find a setting (Ctrl+F)"));
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, em() * 0.5f));
}

// ---------------------------------------------------------------- notices and fixes

// A notice for a couple of seconds at the bottom of the window (any view: the full window, the
// mini window, the caption window over the game), so a hotkey pressed in a game shows it worked.
void noticeToast(const UiModel& m) {
    static int seen = 0;
    static double at = -100.0;
    if (m.noticeSerial != seen) { seen = m.noticeSerial; at = ImGui::GetTime(); }
    const double age = ImGui::GetTime() - at;
    if (m.notice.empty() || age > 2.6) return;
    const float alpha = age < 0.15 ? float(age / 0.15) : age > 2.1 ? float((2.6 - age) / 0.5) : 1.f;
    char text[256];
    std::snprintf(text, sizeof(text), TF(m.notice.c_str()), T(m.noticeArg.c_str()));
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ws = ImGui::GetMainViewport()->Size, wp = ImGui::GetMainViewport()->Pos;
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, ws.x - em() * 4.f);
    const ImVec2 pad(em() * 1.1f, em() * 0.6f);
    // The mini window has its controls at the bottom: the notice goes near the top there.
    const float y = m.mini ? wp.y + em() * 2.6f : wp.y + ws.y - ts.y - pad.y * 2 - em() * 1.2f;
    const ImVec2 a(wp.x + (ws.x - ts.x) * 0.5f - pad.x, y);
    const ImVec2 b(a.x + ts.x + pad.x * 2, a.y + ts.y + pad.y * 2);
    dl->AddRectFilled(a, b, col(ImVec4(kPanel.x, kPanel.y, kPanel.z, 0.97f * alpha)), em() * 0.6f);
    dl->AddRect(a, b, col(ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.8f * alpha)), em() * 0.6f, 0, 1.4f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + pad.x, a.y + pad.y), col(ImVec4(kText.x, kText.y, kText.z, alpha)),
                text, nullptr, ws.x - em() * 4.f);
    ImGui::PopFont();
}

// What fixes a problem: the setting to go to, or looking for devices again. Never a payment page.
struct ErrorFix { const char* label; const char* jumpTo; bool rescan; };
ErrorFix errorFix(const std::string& e) {
    auto has = [&e](const char* s) { return e.find(s) != std::string::npos; };
    if (has("microphone")) return {"Pick a microphone", "Microphone", true};
    if (has("output device")) return {"Pick where it goes", "Send the new voice to", true};
    if (has("API key") || has("key was rejected")) return {"Open Accounts", "Accounts", false};
    if (has("translation service") || has("Switch to DeepL") || has("DeepL key")) return {"Change the service", "Translation service", false};
    if (has("Pick a voice")) return {"Pick a voice", "Voice", false};
    if (has("spoken language")) return {"Pick your language", "I speak", false};
    return {nullptr, nullptr, false};
}
void errorFixButtons(UiModel& m, UiActions& act, const std::string& e) {
    const ErrorFix f = errorFix(e);
    if (!f.label) return;
    auto w = [](const char* l) { return ImGui::CalcTextSize(T(l)).x + em() * 2.4f; };
    if (button(f.label, true, w(f.label))) findJumpTo(m, f.jumpTo);
    if (f.rescan) {
        ImGui::SameLine(0, em() * 0.5f);
        if (button("Look for devices again", false, w("Look for devices again"))) act.rescanDevices = true;
    }
}

// ---------------------------------------------------------------- Check my setup

void checkWindow(UiModel& m, UiActions& act) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(em() * 38.f, vp->Size.x - em() * 2.f), 0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, vp->Size.y - em() * 2.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPopup);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kLine);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.4f, em() * 1.2f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, em() * 0.8f);
    bool open = true;
    char title[96];
    std::snprintf(title, sizeof(title), "%s###checksetup", T("Check my setup"));
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(0.f);
        note("Each step says what to do. It takes about a minute.");
        if (!m.checkPrompt.empty()) {  // what to do now, in a box that's hard to miss
            gap(0.3f);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->ChannelsSplit(2);
            ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
            ImGui::SetCursorScreenPos(ImVec2(a.x + em() * 0.8f, a.y + em() * 0.55f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - em() * 1.6f);
            ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.05f);
            ImGui::TextUnformatted(T(m.checkPrompt));
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            const float h = ImGui::GetItemRectMax().y - a.y + em() * 0.55f;
            ImGui::GetWindowDrawList()->ChannelsSetCurrent(0);
            ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col(ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.16f)), em() * 0.5f);
            ImGui::GetWindowDrawList()->AddRect(a, ImVec2(a.x + w, a.y + h), col(kSignal), em() * 0.5f, 0, 1.4f);
            ImGui::GetWindowDrawList()->ChannelsMerge();
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h));
            ImGui::Dummy(ImVec2(w, 0));
        }
        gap(0.4f);
        const ImVec4 kGood = kVoices[2];
        for (size_t i = 0; i < m.check.size(); ++i) {
            const CheckItem& c = m.check[i];
            ImGui::PushID(int(i));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 dot(p.x + em() * 0.6f, p.y + em() * 0.65f);
            const float r = em() * 0.5f;
            switch (c.state) {
            case kCheckNow: {
                const float pulse = 0.55f + 0.45f * std::sin(float(ImGui::GetTime()) * 5.f);
                dl->AddCircleFilled(dot, r, col(ImVec4(kSignal.x, kSignal.y, kSignal.z, pulse)), 20);
                break;
            }
            case kCheckOk:
                dl->AddCircleFilled(dot, r, col(kGood), 20);
                dl->AddLine(ImVec2(dot.x - r * 0.45f, dot.y), ImVec2(dot.x - r * 0.1f, dot.y + r * 0.38f), col(kBg), 2.f);
                dl->AddLine(ImVec2(dot.x - r * 0.1f, dot.y + r * 0.38f), ImVec2(dot.x + r * 0.5f, dot.y - r * 0.38f), col(kBg), 2.f);
                break;
            case kCheckProblem:
                dl->AddCircleFilled(dot, r, col(kDanger), 20);
                dl->AddLine(ImVec2(dot.x, dot.y - r * 0.5f), ImVec2(dot.x, dot.y + r * 0.12f), col(kBg), 2.2f);
                dl->AddCircleFilled(ImVec2(dot.x, dot.y + r * 0.45f), 1.4f, col(kBg));
                break;
            case kCheckNote:
                dl->AddCircle(dot, r, col(kMuted), 20, 1.5f);
                dl->AddLine(ImVec2(dot.x - r * 0.4f, dot.y), ImVec2(dot.x + r * 0.4f, dot.y), col(kMuted), 1.8f);
                break;
            default:
                dl->AddCircle(dot, r, col(kLine), 20, 1.5f);
                break;
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x + em() * 1.7f, p.y));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(0.f);
            ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
            ImGui::TextColored(c.state == kCheckWaiting ? kMuted : kText, "%s", T(c.title));
            ImGui::PopFont();
            if (!c.detail.empty()) {
                char b[1200];
                std::snprintf(b, sizeof(b), TF(c.detail.c_str()), c.arg1.c_str(), c.arg2.c_str());
                ImGui::TextColored(c.state == kCheckProblem ? kDanger : kMuted, "%s", b);
            }
            if (!c.fix.empty()) {
                if (button(c.fix.c_str(), true, ImGui::CalcTextSize(T(c.fix)).x + em() * 2.4f)) {
                    if (c.fixJump == "sound") act.openSoundPanel = true;
                    else {
                        m.checkOpen = false;
                        if (m.mini) act.setMini = 0;
                        findJumpTo(m, c.fixJump.c_str());
                    }
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::PopID();
            gap(0.45f);
        }
        gap(0.2f);
        if (m.checkRunning) {
            if (button("Stop the check", false, em() * 9.f)) act.stopCheck = true;
        } else {
            int problems = 0;
            for (const CheckItem& c : m.check) problems += c.state == kCheckProblem;
            if (!m.check.empty() && m.check.back().state != kCheckWaiting) {
                ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
                ImGui::TextColored(problems ? kDanger : kGood, "%s", T(problems ? "Fix the red ones, then run it again." : "Everything looks good."));
                ImGui::PopFont();
                gap(0.2f);
            }
            if (button("Run it again", true, em() * 9.f)) act.startCheck = true;
            ImGui::SameLine(0, em() * 0.6f);
            if (button("Close", false, em() * 7.f)) { m.checkOpen = false; act.stopCheck = true; }
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    if (!open) { m.checkOpen = false; act.stopCheck = true; }
}

// ---------------------------------------------------------------- Home

// A rounded card that grows to fit what's in it.
bool beginCard(const char* id, float width = 0.f) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.3f, em() * 1.1f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, em() * 0.9f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    const bool open = ImGui::BeginChild(id, ImVec2(width, 0),
                                        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return open;
}
void endCard() { ImGui::EndChild(); }

// The mic level as bars, newest on the right, kept for the last couple of seconds.
void levelBars(const UiModel& m, ImVec2 a, ImVec2 size, int count) {
    static float hist[64] = {};
    static double next = 0;
    if (ImGui::GetTime() >= next) {
        next = ImGui::GetTime() + 0.05;
        std::memmove(hist, hist + 1, sizeof(float) * 63);
        hist[63] = m.cfg.enabled ? clamp01((m.levelDb + 60.f) / 55.f) : 0.f;
    }
    count = std::max(8, std::min(64, count));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float slot = size.x / float(count), bw = std::max(2.f, slot * 0.55f);
    for (int i = 0; i < count; ++i) {
        const float v = hist[64 - count + i];
        const float bh = std::max(bw, v * size.y);
        const float x = a.x + slot * float(i) + (slot - bw) * 0.5f, cy = a.y + size.y * 0.5f;
        const bool loud = v > clamp01((m.cfg.thresholdDb + 60.f) / 55.f);
        dl->AddRectFilled(ImVec2(x, cy - bh * 0.5f), ImVec2(x + bw, cy + bh * 0.5f), col(loud && m.cfg.enabled ? kSignal : kFieldActive), bw * 0.5f);
    }
}

// Text with clickable words in it ("You speak [English], they hear [Japanese]..."), wrapped to
// the width. `format` has %1, %2, %3 where the chips go, so translations can put them anywhere.
// Returns the chip that was clicked (1..3), or 0.
int sentenceWithChips(const char* format, const std::vector<std::string>& chips, float scale) {
    ImGui::PushID(format);  // each sentence's words have their own IDs (two sentences can share a window)
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float lineH = ImGui::GetFontSize() * 1.75f, padX = em() * 0.3f;
    const char* s = T(format);
    const bool rtl = bidi::hasRtl(s, s + std::strlen(s));  // Arabic: lay the pieces out from the right

    // Split into words, spaces and chips, then place them one after another, wrapping as needed.
    struct Piece { std::string text; int chip; float w; };  // chip: -1 = word, -2 = space, else index
    std::vector<Piece> pieces;
    std::string word;
    auto flush = [&]() { if (!word.empty()) pieces.push_back({word, -1, ImGui::CalcTextSize(word.c_str()).x}); word.clear(); };
    for (const char* q = s; *q; ++q) {
        if (*q == '%' && q[1] >= '1' && q[1] <= '9') {
            flush();
            const size_t i = size_t(q[1] - '1');
            ++q;
            if (i >= chips.size()) continue;
            ImGui::PushFont(gSemibold, ImGui::GetFontSize());
            pieces.push_back({chips[i], int(i), ImGui::CalcTextSize(chips[i].c_str()).x + padX * 2});
            ImGui::PopFont();
        } else if (*q == ' ') {
            flush();
            pieces.push_back({" ", -2, ImGui::CalcTextSize(" ").x});
        } else {
            word += *q;
        }
    }
    flush();

    int clicked = 0;
    float used = 0.f, y = start.y;
    for (const Piece& pc : pieces) {
        if (pc.chip == -2 && used == 0.f) continue;  // no space at the start of a line
        if (used + pc.w > avail && used > 0.f) { used = 0.f; y += lineH; if (pc.chip == -2) continue; }
        const float x = rtl ? start.x + avail - used - pc.w : start.x + used;
        used += pc.w;
        if (pc.chip == -2) continue;
        if (pc.chip == -1) {
            dl->AddText(ImVec2(x, y + (lineH - ImGui::GetFontSize()) * 0.5f), col(kText), pc.text.c_str());
            continue;
        }
        const ImVec2 a(x, y + lineH * 0.08f), b(x + pc.w, y + lineH * 0.92f);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(pc.chip);
        if (ImGui::InvisibleButton("##chip", ImVec2(pc.w, b.y - a.y))) clicked = pc.chip + 1;
        const bool hot = ImGui::IsItemHovered();
        ImGui::PopID();
        dl->AddRectFilled(a, b, col(hot ? kFieldHover : kField), em() * 0.3f, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(a.x, b.y - 2.f), b, col(kSignal));
        ImGui::PushFont(gSemibold, ImGui::GetFontSize());
        dl->AddText(ImVec2(a.x + padX, y + (lineH - ImGui::GetFontSize()) * 0.5f), col(kText), pc.text.c_str());
        ImGui::PopFont();
    }
    ImGui::PopFont();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(start.x, y + lineH));
    ImGui::Dummy(ImVec2(0, 0));
    return clicked;
}

std::string languageName(const std::string& code, const char* none) {
    if (code.empty()) return T(none);
    const Language* l = findLanguage(code);
    return l ? T(l->name) : code;
}
std::string voiceName(const UiModel& m) {
    for (const Voice& v : m.voices)
        if (v.id == m.cfg.voiceId) return v.name;
    return m.cfg.voiceId.empty() ? T("a voice you pick") : m.cfg.voiceId;
}
std::string deviceName(const std::string& d) {
    if (d.empty()) return T("the default speakers");
    const size_t paren = d.find(" (");
    return paren != std::string::npos && paren > 3 ? d.substr(0, paren) : d;
}

// The quick toggles: shortcuts to the extras people use most. Each knows how to show and switch
// itself, and where its full settings are.
struct Tile {
    const char* id;
    int icon;
    const char* name;
    const char* what;
    const char* settingsLabel;  // the setting "Find a setting" jumps to for its full settings
};
const Tile kTiles[] = {
    {"live", kIcWave, "Live transcription", "Voice starts sooner after you talk", "Live transcription"},
    {"emotion", kIcFace, "Carry my emotion", "Laughs, whispers and excitement come through", "Carry my emotion"},
    {"chatbox", kIcChat, "VRChat chatbox", "Your words above your head", "Show in VRChat chatbox"},
    {"listen", kIcHeadphones, "Captions of others", "Read what people say, translated", "Listen to"},
    {"ear", kIcSpeaker, "Hear them in my ear", "Others, translated, in your headphones", "Hear them in my ear"},
    {"screen", kIcScreen, "Screen translation", "Read game text in your language", "Screen translation"},
    {"overlay", kIcCaptions, "Captions over the game", "Subtitles on top of your game", "Caption window over the game"},
    {"meaning", kIcCheck, "What my line means", "Checks your line. Uses extra characters", "Show what my translated line means"},
    {"answer", kIcSwap, "Answer in their language", "Follows who you talk to", "Answer in their language"},
    {"cancel", kIcCancel, "Cancel before it's spoken", "A moment to stop a misheard line", "Cancel before it's spoken"},
    {"swear", kIcBeep, "Clean up my swearing", "Soften or beep your swear words", "Clean up my swearing"},
    {"vr", kIcVR, "Captions in VR", "Captions inside your headset", "Where"},
    {"best", kIcStar, "Best translations", "Bigger AI models, a bit slower", "Best translations"},
    {"history", kIcClock, "Keep a history", "Saves the conversation each day", "Keep a history"},
    {"learn", kIcBook, "Learning mode", "See how to read what's said", "Learning mode"},
    {"deeplwords", kIcWords, "Words for DeepL", "Words the AI softens, kept as said", "Words for DeepL"},
    {"phrases", kIcBookmark, "Quick phrases", "One click for lines you say a lot", "Quick phrases"},
    {"check", kIcCheck, "Check my setup", "A one-minute test of your mic, cable and keys", "Check my setup"},
    {"words", kIcWords, "My word list", "Names written your way, never translated", "My word list"},
    {"share", kIcChat, "Share a setup", "Copy your setup for a friend, without your keys", "Profiles"},
};
const char* const kDefaultTiles[] = {"live", "emotion", "chatbox", "listen", "ear", "screen", "overlay", "answer", "meaning"};

// -1 = not an on/off feature (it opens something), else 0 / 1.
int tileOn(const UiModel& m, const std::string& id) {
    const Settings& c = m.cfg;
    if (id == "live") return c.liveStt;
    if (id == "emotion") return c.emotion;
    if (id == "chatbox") return c.chatbox;
    if (id == "listen") return c.listen.on;
    if (id == "ear") return c.fx.hearThem;
    if (id == "screen") return c.screen.on;
    if (id == "meaning") return c.showMeaning;
    if (id == "answer") return c.fx.answerTheirs;
    if (id == "cancel") return c.fx.previewMs > 0;
    if (id == "swear") return c.fx.voiceSwear != kSwearKeep;
    if (id == "vr") return c.vr.on;
    if (id == "best") return c.fx.best;
    if (id == "history") return c.fx.keepHistory;
    if (id == "learn") return c.fx.learning;
    if (id == "deeplwords") return c.fx.deeplWordsOn;
    return -1;
}

// Clicking a tile: switches it, or (when it needs a choice first) opens its settings.
void tileClick(UiModel& m, UiActions& act, const Tile& t) {
    Settings& c = m.cfg;
    const std::string id = t.id;
    if (id == "live") c.liveStt = !c.liveStt;
    else if (id == "emotion") c.emotion = !c.emotion;
    else if (id == "chatbox") c.chatbox = !c.chatbox;
    else if (id == "listen") c.listen.on = !c.listen.on;
    else if (id == "ear") {
        c.fx.hearThem = !c.fx.hearThem;
        if (c.fx.hearThem) c.listen.on = true;  // it reads out the captions, so they need to be on
    }
    else if (id == "screen") {
        if (!c.screen.agreed || c.screen.on) { if (c.screen.on) c.screen.on = false; else findJumpTo(m, t.settingsLabel); }
        else c.screen.on = true;
    }
    else if (id == "meaning") c.showMeaning = !c.showMeaning;
    else if (id == "answer") c.fx.answerTheirs = !c.fx.answerTheirs;
    else if (id == "cancel") c.fx.previewMs = c.fx.previewMs > 0 ? 0 : 1200;
    else if (id == "swear") c.fx.voiceSwear = c.fx.voiceSwear == kSwearKeep ? kSwearSoften : kSwearKeep;
    else if (id == "vr") c.vr.on = !c.vr.on;
    else if (id == "best") c.fx.best = !c.fx.best;
    else if (id == "history") c.fx.keepHistory = !c.fx.keepHistory;
    else if (id == "learn") c.fx.learning = !c.fx.learning;
    else if (id == "deeplwords") { if (c.fx.deeplWords.empty() && !c.fx.deeplWordsOn) findJumpTo(m, t.settingsLabel); else c.fx.deeplWordsOn = !c.fx.deeplWordsOn; }
    else if (id == "overlay") act.openCaptionWindow = true;
    else if (id == "phrases") m.cfg.page = kPageText;
    else if (id == "check") act.startCheck = true;
    else if (id == "words" || id == "share") findJumpTo(m, t.settingsLabel);
}

std::vector<const Tile*> homeTiles(const UiModel& m) {
    std::vector<const Tile*> out;
    auto add = [&](const std::string& id) {
        for (const Tile& t : kTiles)
            if (id == t.id && std::find(out.begin(), out.end(), &t) == out.end()) out.push_back(&t);
    };
    if (m.cfg.homeTiles.empty()) for (const char* id : kDefaultTiles) add(id);
    else for (const std::string& id : m.cfg.homeTiles) add(id);
    return out;
}

void tileGrid(UiModel& m, UiActions& act) {
    const std::vector<const Tile*> tiles = homeTiles(m);
    const float w = ImGui::GetContentRegionAvail().x, gapW = em() * 0.7f;
    const int cols = std::max(2, std::min(4, int((w + gapW) / (em() * 12.5f + gapW))));
    const float tw = std::floor((w - gapW * float(cols - 1)) / float(cols)), th = em() * 6.4f, armW = em() * 1.9f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    for (size_t i = 0; i < tiles.size(); ++i) {
        const Tile& t = *tiles[i];
        const int r = int(i) / cols, cIdx = int(i) % cols;
        const ImVec2 a(origin.x + float(cIdx) * (tw + gapW), origin.y + float(r) * (th + gapW)), b(a.x + tw, a.y + th);
        const int on = tileOn(m, t.id);
        ImGui::PushID(t.id);
        ImGui::SetCursorScreenPos(a);
        const bool pressed = ImGui::InvisibleButton("##tile", ImVec2(tw - armW, th));
        const bool hot = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(ImVec2(b.x - armW, a.y));
        const bool more = ImGui::InvisibleButton("##arm", ImVec2(armW, th));
        const bool armHot = ImGui::IsItemHovered();
        if (armHot) ImGui::SetTooltip("%s", T("Its full settings"));
        ImGui::PopID();
        dl->AddRectFilled(a, b, col(hot || armHot ? kFieldHover : kPanel), em() * 0.8f);
        if (on == 1) dl->AddRect(a, b, col(kSignal), em() * 0.8f, 0, 1.6f);
        dl->AddLine(ImVec2(b.x - armW, a.y + em() * 0.7f), ImVec2(b.x - armW, b.y - em() * 0.7f), col(kLine), 1.f);
        const ImVec2 cc(b.x - armW * 0.5f, a.y + th * 0.5f);
        const float cs = em() * 0.28f;
        dl->AddLine(ImVec2(cc.x - cs * 0.4f, cc.y - cs), ImVec2(cc.x + cs * 0.4f, cc.y), col(armHot ? kSignal : kMuted), 1.8f);
        dl->AddLine(ImVec2(cc.x + cs * 0.4f, cc.y), ImVec2(cc.x - cs * 0.4f, cc.y + cs), col(armHot ? kSignal : kMuted), 1.8f);
        // Icon in a soft square, the state on the right, then the name and what it does.
        const float pad = em() * 0.8f, isz = em() * 1.9f;
        const ImVec2 ia(a.x + pad, a.y + pad);
        dl->AddRectFilled(ia, ImVec2(ia.x + isz, ia.y + isz), col(on == 1 ? ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.18f) : kField), em() * 0.45f);
        drawIcon(dl, t.icon, ImVec2(ia.x + isz * 0.5f, ia.y + isz * 0.5f), isz * 0.58f, col(on == 1 ? kSignal : kMuted));
        const char* state = on == 1 ? T("On") : on == 0 ? T("Off") : t.icon == kIcCaptions ? T("Open") : t.icon == kIcCheck ? T("Run") : T("Edit");
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 0.82f);
        const ImVec2 ss = ImGui::CalcTextSize(state);
        dl->AddText(ImVec2(b.x - armW - pad * 0.7f - ss.x, ia.y + (isz - ss.y) * 0.5f), col(on == 1 ? kSignal : kMuted), state);
        ImGui::PopFont();
        dl->PushClipRect(a, ImVec2(b.x - armW - em() * 0.3f, b.y), true);
        {
            const float room = tw - armW - pad * 1.4f, base = ImGui::GetStyle().FontSizeBase;
            ImGui::PushFont(gSemibold, base);
            const float nw = ImGui::CalcTextSize(T(t.name)).x;
            ImGui::PopFont();
            ImGui::PushFont(gSemibold, nw > room ? base * std::max(0.72f, room / nw) : base);
            dl->AddText(ImVec2(a.x + pad, ia.y + isz + em() * 0.35f), col(kText), T(t.name));
            ImGui::PopFont();
        }
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + pad, ia.y + isz + em() * 1.55f), col(kMuted), T(t.what), nullptr,
                    tw - armW - pad * 1.5f);
        ImGui::PopFont();
        dl->PopClipRect();
        if (hot) ImGui::SetTooltip("%s", T(t.what));
        if (pressed) tileClick(m, act, t);
        if (more) findJumpTo(m, t.settingsLabel);
    }
    const int rows = (int(tiles.size()) + cols - 1) / cols;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + float(rows) * (th + gapW) - gapW));
    ImGui::Dummy(ImVec2(w, 0));
}

// Pick which quick toggles Home shows.
void editTilesPopup(UiModel& m) {
    if (!ImGui::BeginPopup("##edittiles")) return;
    ImGui::PushTextWrapPos(em() * 22.f);
    heading("Quick toggles", 1.05f);
    note("Pick the shortcuts Home shows (up to 12). Everything is still on its own page.");
    ImGui::PopTextWrapPos();
    gap(0.3f);
    std::vector<std::string> ids;
    for (const Tile* t : homeTiles(m)) ids.push_back(t->id);
    bool changed = false;
    for (const Tile& t : kTiles) {
        bool shownNow = std::find(ids.begin(), ids.end(), t.id) != ids.end();
        ImGui::BeginDisabled(!shownNow && ids.size() >= 12);
        if (ImGui::Checkbox(T(t.name), &shownNow)) {
            changed = true;
            if (shownNow) ids.push_back(t.id);
            else ids.erase(std::find(ids.begin(), ids.end(), t.id));
        }
        ImGui::EndDisabled();
    }
    if (changed) m.cfg.homeTiles = ids;
    gap(0.3f);
    if (button("Back to the usual ones")) m.cfg.homeTiles.clear();
    ImGui::EndPopup();
}

// The extras someone hasn't tried, one at a time, so new people find what makes the app useful.
void newToYou(UiModel& m, UiActions& act) {
    static int at = 0;
    static const char* const kIdeas[] = {"screen", "words", "ear", "listen", "share", "chatbox", "live", "emotion", "overlay", "answer", "cancel", "check", "learn", "history", "phrases"};
    const int n = int(sizeof(kIdeas) / sizeof(kIdeas[0]));
    const Tile* pick = nullptr;
    for (int k = 0; k < n && !pick; ++k) {
        const char* id = kIdeas[(at + k) % n];
        if (tileOn(m, id) == 1) continue;
        if (std::strcmp(id, "phrases") == 0 && !m.cfg.fx.quickPhrases.empty()) continue;
        if (std::strcmp(id, "words") == 0 && !m.cfg.fx.words.empty()) continue;
        for (const Tile& t : kTiles)
            if (std::strcmp(t.id, id) == 0) pick = &t;
        if (pick) at = (at + k) % n;
    }
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 0.86f);
    ImGui::TextColored(kSignal, "%s", T(pick ? "New to you" : "You've tried everything here"));
    ImGui::PopFont();
    if (!pick) {
        note("Extra features has even more, and Settings has your keys and the look.");
        return;
    }
    heading(pick->name, 1.08f);
    note(pick->what);
    gap(0.4f);
    if (button(std::strcmp(pick->id, "check") == 0 ? "Run it" : "Show me", true, em() * 7.f)) {
        if (std::strcmp(pick->id, "check") == 0) act.startCheck = true;
        else findJumpTo(m, pick->settingsLabel);
    }
    ImGui::SameLine(0, em() * 0.6f);
    if (button("Another", false, em() * 6.f)) at = (at + 1) % n;
}

// What others are saying, from Listen.
void aroundYou(UiModel& m) {
    const ListenSettings& l = m.cfg.listen;
    ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::TextUnformatted(T("Around you"));
    ImGui::PopFont();
    if (!l.on) {
        ImGui::SameLine(0, em() * 0.8f);
        ImGui::TextColored(kMuted, "%s", T("Listen is off"));
        note("Turn it on to read what people around you say, in your language.");
        gap(0.3f);
        if (button("Turn on Listen", true, em() * 9.f)) m.cfg.listen.on = true;
        ImGui::SameLine(0, em() * 0.6f);
        if (button("Open Listen", false, em() * 8.f)) m.cfg.page = kPageListen;
        return;
    }
    ImGui::SameLine(0, em() * 0.8f);
    ImGui::TextColored(kMuted, "%s", listenState(m).text[0] ? T(listenState(m).text) : "");
    gap(0.2f);
    int shownCount = 0;
    for (auto it = m.captions.rbegin(); it != m.captions.rend() && shownCount < 3; ++it) {
        if (it->pending || it->lines.empty()) continue;
        for (const CaptionLine& line : it->lines) {
            if (shownCount >= 3 || line.text.empty()) continue;
            const ImVec4 who = line.voiceId >= 0 && m.cfg.fx.voiceIds ? kPeople[line.voiceId % 12] : kMuted;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + em() * 0.3f, p.y + em() * 0.62f), em() * 0.22f, col(who), 12);
            ImGui::SetCursorScreenPos(ImVec2(p.x + em() * 0.9f, p.y));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopTextWrapPos();
            ++shownCount;
        }
    }
    if (shownCount == 0) note("Captions show up here when someone speaks.");
    gap(0.3f);
    if (button("Open Listen", false, em() * 8.f)) m.cfg.page = kPageListen;
}

void homePage(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    ImGui::BeginChild("home", ImVec2(0, 0), 0, ImGuiWindowFlags_NoBackground);

    if (m.firstRun) {  // nothing works without the ElevenLabs key yet
        if (beginCard("welcome")) {
            heading("Welcome to CIMA Box", 1.3f);
            note("Add your ElevenLabs key to start. The setup walks you through it in a couple of minutes, then this page "
                 "shows everything at a glance.");
            gap(0.4f);
            if (button("Start the setup", true, em() * 10.f)) m.setupStep = 0;
        }
        endCard();
        gap(0.6f);
    }

    // ---- You right now: the mic, the mode, your setup in one sentence, what was just said
    if (beginCard("you")) {
        const float w = ImGui::GetContentRegionAvail().x;
        const bool wide = w > em() * 46.f;
        const float speedW = wide ? em() * 14.f : 0.f, leftW = wide ? w - speedW - em() * 2.f : w;
        ImGui::BeginGroup();
        ImGui::PushItemWidth(leftW);
        {
            const float modeW = std::min(em() * 18.f, leftW * 0.55f), barH = em() * 1.6f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            levelBars(m, ImVec2(p.x, p.y + (ImGui::GetFrameHeight() - barH) * 0.5f), ImVec2(leftW - modeW - em(), barH), int((leftW - modeW) / (em() * 0.55f)));
            ImGui::SetCursorScreenPos(ImVec2(p.x + leftW - modeW, p.y));
            static const char* const kEngines[] = {"Changer", "Re-speak", "Translate"};
            segmented("##homeengine", kEngines, 3, &c.engine, modeW);
        }
        gap(0.5f);
        ImGui::BeginChild("sentence", ImVec2(leftW, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        int chip = 0;
        if (c.engine == kTranslate)
            chip = sentenceWithChips("You speak %1, they hear %2 in %3.",
                                     {languageName(c.spokenLang, "any language"), languageName(c.targetLang, "Pick a language"), voiceName(m)}, 1.3f);
        else if (c.engine == kRespeak)
            chip = sentenceWithChips("You speak %1, they hear it in %2.", {languageName(c.spokenLang, "any language"), voiceName(m)}, 1.3f);
        else
            chip = sentenceWithChips("You talk, they hear you as %1.", {voiceName(m)}, 1.3f);
        const char* const popupFor[3][3] = {{"##pickvoice", nullptr, nullptr},
                                            {"##pickspoken", "##pickvoice", nullptr},
                                            {"##pickspoken", "##picktarget", "##pickvoice"}};
        if (chip > 0 && popupFor[c.engine][chip - 1]) ImGui::OpenPopup(popupFor[c.engine][chip - 1]);
        ImGui::SetNextWindowSizeConstraints(ImVec2(em() * 18.f, 0), ImVec2(em() * 26.f, FLT_MAX));
        if (ImGui::BeginPopup("##pickspoken")) {
            fieldLabel("I speak");
            languageCombo("##homespoken", c.spokenLang, true);
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(em() * 18.f, 0), ImVec2(em() * 26.f, FLT_MAX));
        if (ImGui::BeginPopup("##picktarget")) {
            fieldLabel("Translate to");
            languageCombo("##hometarget", c.targetLang, false, c.ttsModel);
            dialectPicker(c);
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(em() * 18.f, 0), ImVec2(em() * 26.f, FLT_MAX));
        if (ImGui::BeginPopup("##pickvoice")) {
            fieldLabel("Voice");
            voiceCombo(m);
            ImGui::EndPopup();
        }
        // Where it comes out.
        const std::string dev = deviceName(c.outDevice);
        if (sentenceWithChips(c.monitor ? "Comes out of %1, and your headphones." : "Comes out of %1.", {dev}, 1.0f) == 1)
            ImGui::OpenPopup("##pickout");
        ImGui::SetNextWindowSizeConstraints(ImVec2(em() * 20.f, 0), ImVec2(em() * 30.f, FLT_MAX));
        if (ImGui::BeginPopup("##pickout")) {
            fieldLabel("Send the new voice to");
            if (deviceCombo("##homeout", m.outputDevices, c.outDevice)) act.reopenOutput = true;
            note("For Discord or a game, pick a virtual cable here and choose the same cable as the microphone there.");
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        // The way into everything else.
        gap(0.35f);
        {
            const char* label = T("All voice settings");
            ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
            const ImVec2 ls = ImGui::CalcTextSize(label);
            ImGui::PopFont();
            const float bw = ls.x + em() * 3.0f, bh = ImGui::GetFrameHeight() * 1.15f;
            const ImVec2 a = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##allvoice", ImVec2(bw, bh))) c.page = kPageVoice;
            const bool hot = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(a, ImVec2(a.x + bw, a.y + bh), col(kSignal), ImGui::GetStyle().FrameRounding, 0, hot ? 2.2f : 1.5f);
            if (hot) dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + bh), col(ImVec4(kSignal.x, kSignal.y, kSignal.z, 0.08f)), ImGui::GetStyle().FrameRounding);
            drawIcon(dl, kIcSliders, ImVec2(a.x + em() * 1.1f, a.y + bh * 0.5f), em() * 1.0f, col(kSignal));
            ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase);
            dl->AddText(ImVec2(a.x + em() * 2.1f, a.y + (bh - em()) * 0.5f), col(kText), label);
            ImGui::PopFont();
            ImGui::SameLine(0, em() * 0.8f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (bh - em()) * 0.5f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(em() * 6.f, leftW - bw - em() * 0.8f));
            ImGui::TextColored(kMuted, "%s", T("Dialect, how you talk, model, translation service, mic and more"));
            ImGui::PopTextWrapPos();
        }

        // What was just said.
        gap(0.5f);
        ImGui::GetWindowDrawList()->AddLine(ImGui::GetCursorScreenPos(),
                                            ImVec2(ImGui::GetCursorScreenPos().x + leftW, ImGui::GetCursorScreenPos().y), col(kLine));
        gap(0.5f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + leftW);
        if (!m.lastError.empty()) {
            statusLine("Problem", T(m.lastError), kDanger);
            errorFixButtons(m, act, m.lastError);
        } else if (!m.pendingSay.empty()) {
            statusLine("About to say", m.pendingSay, kSignal);
        } else if (!m.heardText.empty()) {
            statusLine("You said", m.heardText, kText);
            if (c.engine != kVoiceChanger && !m.sentText.empty()) statusLine("They heard", m.sentText, kSignal);
            if (c.engine != kVoiceChanger && !m.sentText.empty() && !m.meaningText.empty())
                statusLine("Which means", m.meaningText, kMuted);
        } else {
            ImGui::TextColored(kMuted, "%s", T(c.engine == kVoiceChanger ? "Changer mode sends your voice straight through, without text."
                                                                         : "What you say will show up here."));
        }
        ImGui::PopTextWrapPos();
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        // Speed: from your last word to their ears.
        if (wide) {
            ImGui::SameLine(0, em() * 2.f);
            const ImVec2 top = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(top.x - em(), top.y), ImVec2(top.x - em(), top.y + em() * 9.f), col(kLine));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + speedW);
            ImGui::TextColored(kMuted, "%s", T("Last word to their ears"));
            if (m.totalMs > 0) {
                char b[32];
                std::snprintf(b, sizeof(b), "%.2f s", m.totalMs / 1000.0);
                ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 2.2f);
                ImGui::TextUnformatted(b);
                ImGui::PopFont();
                // Where the time went.
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float total = float(std::max(1, m.sttMs + m.translateMs + m.voiceMs)), bh = em() * 0.45f;
                float x = p.x;
                const ImVec4 parts[3] = {kBusy, kSignal, ImVec4(0.72f, 0.64f, 1.f, 1.f)};
                const int ms[3] = {m.sttMs, m.translateMs, m.voiceMs};
                for (int i = 0; i < 3; ++i) {
                    if (ms[i] <= 0) continue;
                    const float segW = std::max(3.f, speedW * float(ms[i]) / total - 2.f);
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, p.y), ImVec2(x + segW, p.y + bh), col(parts[i]), bh * 0.5f);
                    x += segW + 2.f;
                }
                ImGui::Dummy(ImVec2(speedW, bh + em() * 0.3f));
                const char* names[3] = {m.sttLive ? "Heard (live)" : "Heard", m.earlyUsed ? "Translated (early)" : "Translated", "Voice started"};
                char by[96];  // which service translated it
                if (!m.translateVia.empty()) std::snprintf(by, sizeof(by), TF("Translated by %s"), m.translateVia.c_str());
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
                for (int i = 0; i < 3; ++i) {
                    if (ms[i] <= 0) continue;
                    ImDrawList* ldl = ImGui::GetWindowDrawList();
                    const ImVec2 q = ImGui::GetCursorScreenPos();
                    const float rowH = ImGui::GetTextLineHeightWithSpacing();
                    char v[24];
                    std::snprintf(v, sizeof(v), "%.2f s", ms[i] / 1000.0);
                    const float vw = ImGui::CalcTextSize(v).x;
                    ldl->AddRectFilled(ImVec2(q.x, q.y + em() * 0.3f), ImVec2(q.x + em() * 0.6f, q.y + em() * 0.9f), col(parts[i]), em() * 0.15f);
                    ldl->PushClipRect(q, ImVec2(q.x + speedW - vw - em() * 0.4f, q.y + rowH), true);
                    ldl->AddText(ImVec2(q.x + em() * 1.0f, q.y), col(kMuted), i == 1 && !m.translateVia.empty() ? by : T(names[i]));
                    ldl->PopClipRect();
                    ldl->AddText(ImVec2(q.x + speedW - vw, q.y), col(kText), v);
                    ImGui::Dummy(ImVec2(speedW, rowH));
                }
                ImGui::PopFont();
            } else {
                note("Say something to see how quickly people hear it.");
            }
            gap(0.4f);
            if (!m.pendingSay.empty()) {
                if (button("Cancel##home", true, speedW)) act.cancelPending = true;
            } else if (m.busy || m.queued > 0) {
                if (button("Stop speaking##home", false, speedW)) act.stopSpeaking = true;
            }
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
        }
    }
    endCard();

    // ---- Quick toggles
    gap(0.8f);
    {
        const bool glow = jumpAnchor("Quick toggles");
        const float rowY = ImGui::GetCursorPosY();
        cardTitle("Quick toggles");
        if (glow) jumpGlow(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const ImGuiStyle& st = ImGui::GetStyle();
        const float b1 = ImGui::CalcTextSize(T("All features")).x + st.FramePadding.x * 2,
                    b2 = ImGui::CalcTextSize(T("Edit tiles")).x + st.FramePadding.x * 2;
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - b1 - b2 - em() * 0.6f, rowY + em() * 0.15f));
        if (ImGui::SmallButton(T("All features"))) c.page = kPageFeatures;
        ImGui::SameLine(0, em() * 0.6f);
        if (ImGui::SmallButton(T("Edit tiles"))) ImGui::OpenPopup("##edittiles");
        editTilesPopup(m);
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextColored(kMuted, "%s", T("Shortcuts for the extras you use most. Their full settings are a click away."));
        ImGui::PopTextWrapPos();
    }
    gap(0.4f);
    tileGrid(m, act);

    // ---- Around you, and something new to try
    gap(0.9f);
    {
        const float w = ImGui::GetContentRegionAvail().x, gapW = em() * 0.9f;
        const bool side = w > em() * 40.f;
        const float rightW = side ? std::max(em() * 16.f, w * 0.32f) : w;
        if (beginCard("around", side ? w - rightW - gapW : 0.f)) aroundYou(m);
        endCard();
        if (side) ImGui::SameLine(0, gapW);
        else gap(0.6f);
        if (beginCard("newtoyou", rightW)) newToYou(m, act);
        endCard();
    }
    gap(0.6f);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- My voice

void sectionCard(const char* id, const char* title, void (*body)(UiModel&, UiActions&), UiModel& m, UiActions& act) {
    if (beginCard(id)) {
        const bool glow = jumpAnchor(title);
        cardTitle(title);
        if (glow) jumpGlow(ImGui::GetItemRectMin(), ImVec2(ImGui::GetItemRectMin().x + ImGui::GetContentRegionAvail().x, ImGui::GetItemRectMax().y));
        gap(0.25f);
        body(m, act);
    }
    endCard();
}

// What was just said, and the buttons to stop it.
void lastLineCard(UiModel& m, UiActions& act) {
    cardTitle("Last line", 1.15f);
    gap(0.2f);
    ImGui::PushTextWrapPos(0.f);
    if (!m.lastError.empty()) {
        ImGui::TextColored(kDanger, "%s", T(m.lastError));
        if (const ServiceStatus* bad = troubleInUse(m)) {
            char b[400];
            std::snprintf(b, sizeof(b), TF("%s's status page reports: %s"), bad->name.c_str(), bad->detail.c_str());
            ImGui::TextColored(healthColor(bad->health), "%s", b);
        }
        errorFixButtons(m, act, m.lastError);
    } else if (!m.pendingSay.empty()) {
        ImGui::TextColored(kMuted, "%s", T("About to say"));
        ImGui::TextColored(kSignal, "%s", m.pendingSay.c_str());
    } else if (!m.heardText.empty()) {
        ImGui::TextColored(kMuted, "%s", T("You said"));
        ImGui::TextUnformatted(m.heardText.c_str());
        if (m.cfg.engine != kVoiceChanger && !m.sentText.empty()) {
            gap(0.2f);
            ImGui::TextColored(kMuted, "%s", T("They heard"));
            ImGui::TextColored(kSignal, "%s", m.sentText.c_str());
            if (!m.meaningText.empty()) {
                gap(0.2f);
                ImGui::TextColored(kMuted, "%s", T("Which means"));
                ImGui::TextColored(kMuted, "%s", m.meaningText.c_str());
            }
        }
        if (m.cfg.engine != kVoiceChanger && m.cfg.emotion && !m.delivery.empty()) note(m.delivery);
    } else {
        ImGui::TextColored(kMuted, "%s", T(m.cfg.engine == kVoiceChanger ? "Changer mode sends your voice straight through, without text."
                                                                          : "What you say will show up here."));
    }
    ImGui::PopTextWrapPos();
    gap(0.2f);
    note(timingLine(m));
    gap(0.3f);
    if (!m.pendingSay.empty()) {
        if (button("Cancel##strip", true)) act.cancelPending = true;
    } else if (button("Stop speaking")) {
        act.stopSpeaking = true;
    }
    if (m.queued > 0) ImGui::TextColored(kMuted, TF("%d waiting"), m.queued);
}

void tryLineCard(UiModel& m, UiActions& act) {
    const Settings& c = m.cfg;
    cardTitle("Try a line", 1.15f);
    note("See how each style would say something, before anyone hears it.");
    gap(0.25f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##trytextvoice", T("Type something to say"), &m.tryText);
    ImGui::BeginDisabled(m.tryBusy || m.tryText.find_first_not_of(" ") == std::string::npos || !isAiService(c.translateService) ||
                         c.engine != kTranslate);
    if (button(m.tryBusy ? "Translating" : "Try it", true)) act.tryLine = m.tryText;
    ImGui::EndDisabled();
    if (c.engine != kTranslate) note("For Translate mode.");
    else if (!isAiService(c.translateService)) note("Needs Claude, Gemini or OpenAI as the translation service.");
    static const char* const kNames[] = {"Polite", "Casual", "Very casual", "My own"};
    for (int i = 0; i < 4; ++i) {
        if (m.tryResults[i].empty()) continue;
        ImGui::TextColored(kSignal, "%s", T(kNames[i]));
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextUnformatted(m.tryResults[i].c_str());
        ImGui::PopTextWrapPos();
    }
}

void voicePage(UiModel& m, UiActions& act) {
    // Your settings in three sections, in the order the sound goes: microphone, voice and
    // language, where it goes. On the right, the last line and a way to try one.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gapW = em() * 1.2f;
    const bool side = avail.x > em() * 46.f;
    const float rightW = side ? std::min(em() * 20.f, avail.x * 0.34f) : 0.f;
    ImGui::BeginChild("voicecol", ImVec2(side ? avail.x - rightW - gapW : 0.f, 0), 0, ImGuiWindowFlags_NoBackground);
    sectionCard("mic", "Microphone", inputPanel, m, act);
    gap(0.7f);
    sectionCard("voice", "Voice and language", voicePanel, m, act);
    gap(0.7f);
    sectionCard("out", "Where it goes", outputPanel, m, act);
    if (!side) {
        gap(0.7f);
        if (beginCard("lastline")) lastLineCard(m, act);
        endCard();
    }
    gap(0.5f);
    ImGui::EndChild();
    if (!side) return;
    ImGui::SameLine(0, gapW);
    ImGui::BeginChild("voiceside", ImVec2(rightW, 0), 0, ImGuiWindowFlags_NoBackground);
    if (beginCard("lastline")) lastLineCard(m, act);
    endCard();
    gap(0.7f);
    if (beginCard("trybox")) tryLineCard(m, act);
    endCard();
    gap(0.7f);
    if (beginCard("voiceextras")) {
        cardTitle("Extras for this page", 1.15f);
        gap(0.3f);
        extrasFor(m, {"Cancel before it's spoken", "Answer in their language", "Languages", "Clean up my swearing", "Best translations", "Words for DeepL", "My word list", "More hotkeys", "Profiles"}, false);
    }
    endCard();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- the mini window
// Your own line, and under it what others are saying when Listen is on.

void miniView(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x, rowH = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x;
    if (m.logoTexture != 0 && m.logoSize > 0) {
        const float side = rowH;
        dl->AddImageRounded(ImTextureRef(ImTextureID(m.logoTexture)), ImVec2(x, p.y), ImVec2(x + side, p.y + side), ImVec2(0, 0),
                            ImVec2(1, 1), IM_COL32_WHITE, side * 0.25f);
        x += side + em() * 0.6f;
    }
    const PageState st = voiceState(m);
    dl->AddCircleFilled(ImVec2(x + em() * 0.3f, p.y + rowH * 0.5f), em() * 0.26f, col(st.color), 14);
    x += em() * 0.85f;
    const char* stText = T(st.text);
    dl->AddText(ImVec2(x, p.y + (rowH - em()) * 0.5f), col(st.color), stText);
    x += ImGui::CalcTextSize(stText).x + em() * 0.8f;
    const float backW = em() * 7.5f;
    std::string pair;
    if (c.engine == kTranslate) {
        const Language* from = findLanguage(c.spokenLang);
        const Language* to = findLanguage(c.targetLang);
        pair = std::string(from ? from->code : "auto") + " > " + (to ? to->code : "?");
        for (char& ch : pair) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    }
    const float pairW = pair.empty() ? 0.f : ImGui::CalcTextSize(pair.c_str()).x + em() * 1.0f;
    const float barsW = std::max(0.f, p.x + w - backW - pairW - em() * 1.4f - x);
    if (barsW > em() * 3.f) levelBars(m, ImVec2(x, p.y + rowH * 0.2f), ImVec2(barsW, rowH * 0.6f), int(barsW / (em() * 0.5f)));
    if (!pair.empty()) {
        const ImVec2 a(p.x + w - backW - em() * 0.6f - pairW, p.y);
        dl->AddRectFilled(a, ImVec2(a.x + pairW, a.y + rowH), col(kField), rowH * 0.5f);
        dl->AddText(ImVec2(a.x + em() * 0.5f, a.y + (rowH - em()) * 0.5f), col(kText), pair.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - backW, p.y));
    if (button("Full window", false, backW)) act.setMini = 0;
    gap(0.25f);

    // What they just heard.
    const float bottomH = ImGui::GetFrameHeightWithSpacing() + em() * 0.2f;
    if (beginCard("miniline")) {
        ImGui::PushTextWrapPos(0.f);
        if (!m.lastError.empty()) {
            ImGui::TextColored(kDanger, "%s", T(m.lastError));
            if (const ErrorFix f = errorFix(m.lastError); f.label && button(f.label, true)) {
                act.setMini = 0;
                findJumpTo(m, f.jumpTo);
            }
        } else if (!m.pendingSay.empty()) {
            ImGui::TextColored(kMuted, "%s", T("About to say"));
            ImGui::TextColored(kSignal, "%s", m.pendingSay.c_str());
        } else if (!m.heardText.empty()) {
            if (c.engine != kVoiceChanger && !m.sentText.empty()) {
                ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.05f);
                ImGui::TextUnformatted(m.sentText.c_str());
                ImGui::PopFont();
            }
            ImGui::TextColored(kMuted, "%s", m.heardText.c_str());
        } else {
            ImGui::TextColored(kMuted, "%s", T("What you say will show up here."));
        }
        ImGui::PopTextWrapPos();
    }
    endCard();
    // What others are saying (Listen), under your own line: one small window for the whole
    // conversation. It takes the room between your line and the controls, newest first, and what
    // doesn't fit is simply not shown (make the window taller to see more).
    {
        const float floorY = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - bottomH;
        const float room = floorY - ImGui::GetCursorScreenPos().y - em() * 0.2f;
        if (!c.listen.on) {
            if (room > ImGui::GetFrameHeight()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(kMuted, "%s", T("Listen is off"));
                ImGui::SameLine(0, em() * 0.8f);
                if (button("Turn on Listen", false, em() * 9.f)) c.listen.on = true;
            }
        } else if (room > em() * 2.4f) {
            ImGui::BeginChild("##minithem", ImVec2(0, room), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
            ImGui::TextColored(kMuted, "%s", T("Around you"));
            const PageState ls = listenState(m);
            if (ls.text[0]) {
                ImGui::SameLine(0, em() * 0.6f);
                ImGui::TextColored(ls.color, "%s", T(ls.text));
            }
            ImGui::PopFont();
            int shown = 0;
            for (auto it = m.captions.rbegin(); it != m.captions.rend() && shown < 6; ++it) {
                if (it->pending || it->lines.empty()) continue;
                for (const CaptionLine& line : it->lines) {
                    if (shown >= 6 || line.text.empty()) continue;
                    const bool person = line.voiceId >= 0 && c.fx.voiceIds;
                    if (person && std::find(m.mutedVoices.begin(), m.mutedVoices.end(), line.voiceId) != m.mutedVoices.end()) continue;
                    const ImVec4 who = person ? kPeople[line.voiceId % 12] : kMuted;
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x + em() * 0.3f, at.y + em() * 0.62f), em() * 0.22f, col(who), 12);
                    ImGui::SetCursorScreenPos(ImVec2(at.x + em() * 0.9f, at.y));
                    ImGui::PushTextWrapPos(0.f);
                    ImGui::TextUnformatted(line.text.c_str());
                    ImGui::PopTextWrapPos();
                    ++shown;
                }
            }
            if (shown == 0) ImGui::TextColored(kMuted, "%s", T("Captions show up here when someone speaks."));
            ImGui::EndChild();
        }
    }
    const float y = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - bottomH + em() * 0.2f;
    if (y > ImGui::GetCursorScreenPos().y) ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, y));
    // The mode, stop, and the switch.
    const float sh = std::floor(em() * 1.4f), sw = sh * 1.8f, stopW = em() * 6.5f;
    static const char* const kEngines[] = {"Changer", "Re-speak", "Translate"};
    const ImVec2 r = ImGui::GetCursorScreenPos();
    segmented("##miniengine", kEngines, 3, &c.engine, std::max(em() * 10.f, w - stopW - sw - em() * 1.6f));
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - sw - em() * 0.8f - stopW, r.y));
    if (!m.pendingSay.empty()) {
        if (button("Cancel##mini", true, stopW)) act.cancelPending = true;
    } else if (button("Stop##mini", false, stopW)) {
        act.stopSpeaking = true;
    }
    const ImVec2 sp(p.x + w - sw, r.y + (rowH - sh) * 0.5f);
    ImGui::SetCursorScreenPos(sp);
    if (ImGui::InvisibleButton("##minimaster", ImVec2(sw, sh))) c.enabled = !c.enabled;
    drawSwitch(sp, sh, c.enabled, ImGui::IsItemHovered(), ImGui::GetID("##masteranim"));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T(c.enabled ? "Turn the voice changer off" : "Turn the voice changer on"));
}

// ---------------------------------------------------------------- Usage

// "1,234" for counts.
std::string usageCount(long long n) {
    std::string s = std::to_string(n < 0 ? -n : n), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && (s.size() - i) % 3 == 0) out += ',';
        out += s[i];
    }
    return n < 0 ? "-" + out : out;
}
// "12.4k" for big counts.
std::string usageShort(double n) {
    char b[32];
    if (n >= 1e6) std::snprintf(b, sizeof(b), "%.1fM", n / 1e6);
    else if (n >= 10000) std::snprintf(b, sizeof(b), "%.0fk", n / 1e3);
    else if (n >= 1000) std::snprintf(b, sizeof(b), "%.1fk", n / 1e3);
    else std::snprintf(b, sizeof(b), "%.0f", n);
    return b;
}
std::string usageMoney(double d) {
    char b[32];
    if (d <= 0) return "$0";
    if (d < 0.001) std::snprintf(b, sizeof(b), "$%.5f", d);
    else if (d < 1) std::snprintf(b, sizeof(b), "$%.3f", d);
    else std::snprintf(b, sizeof(b), "$%.2f", d);
    return b;
}
std::string usageMinutes(long long ms) {
    char b[32];
    if (ms < 60000) std::snprintf(b, sizeof(b), TF("%.0f s"), double(ms) / 1000.0);
    else std::snprintf(b, sizeof(b), TF("%.0f min"), double(ms) / 60000.0);
    return b;
}

// A few large numbers side by side, each with a small line under it.
void usageBigNumbers(const std::vector<std::pair<std::string, const char*>>& items) {
    const float w = ImGui::GetContentRegionAvail().x / float(std::max<size_t>(1, items.size()));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < items.size(); ++i) {
        const float x = p.x + w * float(i);
        ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 1.55f);
        dl->AddText(ImVec2(x, p.y), col(kText), items[i].first.c_str());
        const float big = ImGui::GetFontSize();
        ImGui::PopFont();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
        dl->PushClipRect(ImVec2(x, p.y), ImVec2(x + w - em() * 0.4f, p.y + big + em() * 2.f), true);
        dl->AddText(ImVec2(x, p.y + big * 1.08f), col(kMuted), T(items[i].second));
        dl->PopClipRect();
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, ImGui::GetStyle().FontSizeBase * 1.55f + em() * 1.25f));
}

// A bar made of coloured parts (fractions of the whole width).
void usageBar(const std::vector<std::pair<float, ImVec4>>& parts, float h = 0.f) {
    const float w = ImGui::GetContentRegionAvail().x;
    h = h > 0 ? h : std::floor(em() * 0.5f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col(kField), h * 0.5f);
    float x = p.x;
    for (const auto& part : parts) {
        const float pw = w * std::max(0.f, std::min(1.f, part.first));
        if (pw <= 0) continue;
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(std::min(p.x + w, x + pw), p.y + h), col(part.second), h * 0.5f);
        x += pw;
    }
    ImGui::Dummy(ImVec2(w, h));
}

// A small coloured square and a label, on one line.
void usageKey(const ImVec4& c, const char* label) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = std::floor(em() * 0.6f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + (ImGui::GetTextLineHeight() - s) * 0.5f),
                                              ImVec2(p.x + s, p.y + (ImGui::GetTextLineHeight() + s) * 0.5f), col(c), s * 0.25f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + s + em() * 0.4f, p.y));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
    ImGui::TextColored(kText, "%s", T(label));
    ImGui::PopFont();
}

const char* const kUseFeatureNames[] = {"My voice and Text", "Listen: finished captions", "Listen: while they talk",
                                        "What my translated line means", "Hear them in my ear", "Changer and Re-speak",
                                        "Other (Try a line, screen, learning)"};
const ImVec4 kUseFeatureColors[] = {rgb(0x8FB2F2), rgb(0xE59AD0), rgb(0xE8B05E), rgb(0x7FD6A4), rgb(0xB9A3FF), rgb(0xFF9E8A), rgb(0x6B7385)};

// What a service does in this setup, in a few words.
std::string usageRole(const Settings& c, int svc) {
    const int mine = c.translateService, theirs = c.captionService();
    if (mine == svc && theirs == svc) return T("Translates My voice, Text and Listen");
    if (mine == svc) return T("Translates My voice and Text");
    if (theirs == svc) return T("Translates Listen");
    return std::string();
}

void usagePage(UiModel& m, UiActions& act) {
    Settings& c = m.cfg;
    // This session, today, 7 days, 30 days; and the service shown. (For tests: CIMA_TEST_USAGE="service,range".)
    static int range = 0;
    static int service = kUseEleven;
    static const bool picked = [] {
        if (const char* v = std::getenv("CIMA_TEST_USAGE")) std::sscanf(v, "%d,%d", &service, &range);
        service = std::max(0, std::min(kUseServices - 1, service));
        range = std::max(0, std::min(3, range));
        return true;
    }();
    (void)picked;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    static const char* const kRanges[] = {"This session", "Today", "7 days", "30 days"};
    segmented("##userange", kRanges, 4, &range, std::min(avail.x, em() * 26.f));
    const UseTotals t = range == 0 ? useSession() : useDays(range == 1 ? 1 : range == 2 ? 7 : 30);
    gap(0.4f);

    // The services, side by side: each with what it came to in this range.
    struct Svc { int use; const char* name; int translate; };
    static const Svc kSvcs[] = {{kUseEleven, "ElevenLabs", -1}, {kUseClaude, "Claude", kClaude}, {kUseGemini, "Gemini", kGemini},
                                {kUseOpenAI, "OpenAI", kOpenAI}, {kUseDeepL, "DeepL", kDeepL}, {kUseMicrosoft, "Microsoft Azure", kMicrosoft},
                                {kUseLocal, "On this PC", kLocal}};
    {
        const int n = int(sizeof(kSvcs) / sizeof(*kSvcs));
        const float gapW = em() * 0.5f, w = (ImGui::GetContentRegionAvail().x - gapW * float(n - 1)) / float(n);
        const float h = em() * 2.9f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < n; ++i) {
            const Svc& s = kSvcs[i];
            const UseAmount a = t.service(s.use);
            std::string value;
            if (s.use == kUseEleven) {
                const long long ms = a.audioMs;
                value = range == 0 && m.sessionCredits >= 0 ? usageCount(m.sessionCredits) + " " + T("credits")
                        : ms > 0                            ? usageMinutes(ms)
                                                            : usageShort(double(a.letters)) + " " + T("letters");
            } else if (s.use == kUseDeepL || s.use == kUseMicrosoft) {
                value = usageShort(double(a.letters)) + " " + T("letters");
            } else if (s.use == kUseLocal) {
                value = usageCount(a.requests) + " " + T("lines");
            } else {
                value = usageMoney(a.dollars);
            }
            const ImVec2 a0(p.x + (w + gapW) * float(i), p.y);
            ImGui::SetCursorScreenPos(a0);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##svc", ImVec2(w, h))) service = s.use;
            const bool hot = ImGui::IsItemHovered();
            ImGui::PopID();
            const bool on = service == s.use;
            dl->AddRectFilled(a0, ImVec2(a0.x + w, a0.y + h), col(hot && !on ? mix(kPanel, kField, 0.5f) : kPanel), em() * 0.5f);
            if (on) dl->AddRect(a0, ImVec2(a0.x + w, a0.y + h), col(kSignal), em() * 0.5f, 0, 1.6f);
            dl->PushClipRect(a0, ImVec2(a0.x + w - em() * 0.4f, a0.y + h), true);
            ImGui::PushFont(gSemibold, ImGui::GetStyle().FontSizeBase * 0.95f);
            dl->AddText(ImVec2(a0.x + em() * 0.7f, a0.y + em() * 0.45f), col(kText), T(s.name));
            ImGui::PopFont();
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
            dl->AddText(ImVec2(a0.x + em() * 0.7f, a0.y + em() * 1.6f), col(on ? kSignal : kMuted), value.c_str());
            ImGui::PopFont();
            dl->PopClipRect();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        gap(0.5f);
    }

    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float gapW = em() * 1.0f;
    const float bodyH = origin.y + avail.y - top.y;
    const float leftW = std::floor((avail.x - gapW) * 0.56f), rightW = avail.x - leftW - gapW;
    const UseAmount all = t.service(service);
    const char* name = "ElevenLabs";
    for (const Svc& s : kSvcs)
        if (s.use == service) name = s.name;

    // ---- the left column, top: the totals
    const float topH = std::min(bodyH * 0.48f, em() * 13.f);
    beginPanel("usetotals", ImVec2(leftW, topH), name);
    if (service == kUseEleven) {
        const Quota& q = m.elevenQuota;
        const long long left = q.known ? std::max(0LL, q.limit - q.used) : -1;
        int days = -1;
        if (q.known && q.resetsAt > 0) days = std::max(0, int((q.resetsAt - (int64_t)std::time(nullptr) + 86399) / 86400));
        usageBigNumbers({{m.sessionCredits >= 0 ? usageCount(m.sessionCredits) : std::string("-"), "credits this session"},
                         {left >= 0 ? usageCount(left) : std::string("-"), "credits left"},
                         {days >= 0 ? std::to_string(days) : std::string("-"), "days until they renew"}});
        if (q.known && q.limit > 0) usageBar({{float(q.used) / float(q.limit), kSignal}});
        if (!q.problem.empty()) note(T(q.problem), kBusy);
        else if (!q.known) note(c.apiKey.empty() ? "Add your ElevenLabs key in Settings > Accounts." : "Reading your account...");
        else {
            char b[200];
            std::snprintf(b, sizeof(b), TF("%s of %s credits used this month."), usageCount(q.used).c_str(), usageCount(q.limit).c_str());
            note(b);
        }
    } else if (service == kUseClaude || service == kUseGemini || service == kUseOpenAI) {
        const long long in = all.tokensIn + all.cacheRead + all.cacheWritten;
        usageBigNumbers({{usageMoney(all.dollars), "about, at list price"},
                         {usageCount(all.requests), "requests"},
                         {usageShort(double(in)), "tokens in"},
                         {usageShort(double(all.tokensOut)), "tokens out"}});
        const float reused = in > 0 ? float(all.cacheRead) / float(in) : 0.f, fresh = in > 0 ? float(all.cacheWritten) / float(in) : 0.f;
        char b[160];
        std::snprintf(b, sizeof(b), TF("Reused from its cache: %.0f%%"), reused * 100.f);
        ImGui::TextColored(kLabel, "%s", b);
        usageBar({{reused, kVoices[2]}, {fresh, kBusy}});
        gap(0.2f);
        usageKey(kVoices[2], service == kUseClaude ? "Reused: a tenth of the price" : "Reused: cheaper");
        if (service == kUseClaude) {
            ImGui::SameLine(0, em() * 1.2f);
            usageKey(kBusy, "Newly kept: a quarter more");
        }
        const std::string role = usageRole(c, service == kUseClaude ? kClaude : service == kUseGemini ? kGemini : kOpenAI);
        note(!role.empty() ? role
             : (service == kUseClaude ? c.claudeKey : service == kUseGemini ? c.geminiKey : c.openaiKey).empty()
                 ? std::string(T("No key added (Settings > Accounts)."))
                 : std::string(T("Used only as a backup: for lines the chosen AI says no to or fails on.")));
    } else if (service == kUseDeepL) {
        const Quota& q = m.deeplQuota;
        usageBigNumbers({{usageCount(all.letters), range == 0 ? "letters this session" : range == 1 ? "letters today" : range == 2 ? "letters in 7 days" : "letters in 30 days"},
                         {q.known ? usageCount(q.used) : std::string("-"), "used this month"},
                         {q.known ? usageCount(q.limit) : std::string("-"), "a month on your plan"}});
        if (q.known && q.limit > 0) usageBar({{float(q.used) / float(q.limit), kSignal}});
        note(c.deeplKey.empty() ? std::string(T("No key added (Settings > Accounts)."))
             : usageRole(c, kDeepL).empty() ? std::string(T("The backup when the AI is slow or busy."))
                                            : usageRole(c, kDeepL));
    } else if (service == kUseMicrosoft) {
        const UseAmount month = useDays(30).service(kUseMicrosoft);
        usageBigNumbers({{usageCount(all.letters), range == 0 ? "letters this session" : range == 1 ? "letters today" : range == 2 ? "letters in 7 days" : "letters in 30 days"},
                         {usageCount(month.letters), "last 30 days"},
                         {"2,000,000", "a month free (F0)"}});
        usageBar({{float(month.letters) / 2000000.f, kSignal}});
        note(c.msKey.empty() ? "No key added (Settings > Accounts)."
                             : "Microsoft doesn't tell apps how much a key has used: this is counted by CIMA Box on this PC. Your Azure portal shows the exact figure.");
    } else {  // a model on this PC
        char ms[32] = "-";
        if (all.requests > 0) std::snprintf(ms, sizeof(ms), TF("%.2f s"), double(all.workMs) / double(all.requests) / 1000.0);
        usageBigNumbers({{usageCount(all.requests), "lines translated"}, {ms, "usual time per line"}, {"$0", "free, nothing sent"}});
        if (!m.local.running.empty()) {
            char b[300];
            std::snprintf(b, sizeof(b), TF("Running: %s%s%s."), m.local.running.c_str(), m.local.device.empty() ? "" : ", ",
                          m.local.device.c_str());
            note(b);
        } else {
            note("Not loaded right now.");
        }
    }
    ImGui::EndChild();

    // ---- the left column, bottom: what it went on
    ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + topH + gapW));
    beginPanel("usefeatures", ImVec2(leftW, bodyH - topH - gapW), "What it went on");
    {
        const bool ai = service == kUseClaude || service == kUseGemini || service == kUseOpenAI;
        const bool eleven = service == kUseEleven;
        const int cols = ai ? 4 : 3;
        if (ImGui::BeginTable("##usetable", cols, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn(T("Feature"), ImGuiTableColumnFlags_WidthStretch, eleven ? 2.6f : 3.f);
            ImGui::TableSetupColumn(T("Requests"), ImGuiTableColumnFlags_WidthStretch, eleven ? 0.9f : 1.f);
            ImGui::TableSetupColumn(eleven ? T("Amount") : ai ? T("Tokens in") : service == kUseLocal ? T("Usual time") : T("Letters"),
                                    ImGuiTableColumnFlags_WidthStretch, eleven ? 1.9f : 1.1f);
            if (ai) ImGui::TableSetupColumn(T("Cost"), ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, kField);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor(2);
            int rows = 0;
            for (int f = 0; f < kUseFeatures; ++f) {
                const UseAmount& a = t.by[service][f];
                if (a.empty()) continue;
                ++rows;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                usageKey(kUseFeatureColors[f], kUseFeatureNames[f]);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(usageCount(a.requests).c_str());
                ImGui::TableNextColumn();
                std::string amount;
                if (eleven) {
                    if (a.audioMs > 0) amount = usageMinutes(a.audioMs);
                    if (a.letters > 0) amount += (amount.empty() ? "" : ", ") + usageShort(double(a.letters)) + " " + T("letters");
                } else if (ai) {
                    amount = usageShort(double(a.tokensIn + a.cacheRead + a.cacheWritten));
                } else if (service == kUseLocal) {
                    char b[32];
                    std::snprintf(b, sizeof(b), TF("%.2f s"), a.requests > 0 ? double(a.workMs) / double(a.requests) / 1000.0 : 0.0);
                    amount = b;
                } else {
                    amount = usageCount(a.letters);
                }
                ImGui::TextUnformatted(amount.c_str());
                if (ai) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(usageMoney(a.dollars).c_str());
                }
            }
            ImGui::EndTable();
            if (rows == 0) note(range == 0 ? "Nothing yet in this session." : "Nothing in these days.");
        }
        gap(0.4f);
        if (eleven)
            note("Minutes of sound written down or changed, and letters spoken. ElevenLabs charges by these; it says credits per day, "
                 "not per feature (the chart on the right).");
        else if (ai)
            note("Counted from each answer's token count, at list price. Your bill on the service's own site is the exact figure.");
        if (eleven && c.fx.liveCaptions && c.listen.tellApart && !t.by[kUseEleven][kUseLive].empty())
            note("\"Captions while they talk\" with \"Tell speakers apart\" sends what others say twice: live, and once more to tell who said what.",
                 kBusy);
        if (ai && !t.by[service][kUseLive].empty() && t.by[service][kUseLive].dollars > t.by[service][kUseCaptions].dollars)
            note("Most of it was sentences translated while they talk (\"Translate each sentence right away\", under Captions while they talk). "
                 "The finished caption is translated either way.", kBusy);
    }
    ImGui::EndChild();

    // ---- the right column
    ImGui::SetCursorScreenPos(ImVec2(top.x + leftW + gapW, top.y));
    if (service == kUseEleven) {
        beginPanel("useright", ImVec2(rightW, bodyH), "Last 7 days");
        const std::vector<ElevenDay>& days = m.elevenDays;
        if (days.empty()) {
            note(c.apiKey.empty() ? "Add your ElevenLabs key in Settings > Accounts." : "Not read yet (or this key can't read the account's usage).");
        } else {
            double most = 1;
            for (const ElevenDay& d : days) most = std::max(most, d.total());
            const float chartH = std::min(em() * 9.f, ImGui::GetContentRegionAvail().y * 0.5f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, slot = w / float(days.size()), bw = std::min(slot * 0.55f, em() * 1.6f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float labelH = ImGui::GetTextLineHeight();
            for (size_t i = 0; i < days.size(); ++i) {
                const ElevenDay& d = days[i];
                const float x = p.x + slot * float(i) + (slot - bw) * 0.5f;
                float y = p.y + labelH + chartH;
                const std::pair<double, ImVec4> parts[] = {{d.written, kUseFeatureColors[3]}, {d.live, rgb(0x8EC5FF)}, {d.voice, kSignal},
                                                           {d.changer, kUseFeatureColors[1]}, {d.other, kUseFeatureColors[6]}};
                for (const auto& part : parts) {
                    const float ph = float(part.first / most) * chartH;
                    if (ph <= 0) continue;
                    dl->AddRectFilled(ImVec2(x, y - ph), ImVec2(x + bw, y), col(part.second));
                    y -= ph;
                }
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.78f);
                const std::string v = usageShort(d.total());
                const ImVec2 vs = ImGui::CalcTextSize(v.c_str());
                dl->AddText(ImVec2(x + (bw - vs.x) * 0.5f, y - vs.y - em() * 0.15f), col(kText), v.c_str());
                char day[16] = "";
                const std::time_t ts = std::time_t(d.start);
                std::tm tm{};
#ifdef _WIN32
                gmtime_s(&tm, &ts);
#else
                gmtime_r(&ts, &tm);
#endif
                std::strftime(day, sizeof(day), "%d.%m", &tm);
                const ImVec2 ds = ImGui::CalcTextSize(day);
                dl->AddText(ImVec2(x + (bw - ds.x) * 0.5f, p.y + labelH + chartH + em() * 0.3f), col(i + 1 == days.size() ? kSignal : kMuted), day);
                ImGui::PopFont();
            }
            ImGui::Dummy(ImVec2(w, labelH + chartH + em() * 1.6f));
            usageKey(kUseFeatureColors[3], "Written down");
            ImGui::SameLine(0, em() * 1.2f);
            usageKey(rgb(0x8EC5FF), "Live words");
            ImGui::SameLine(0, em() * 1.2f);
            usageKey(kSignal, "Voice");
            ImGui::SameLine(0, em() * 1.2f);
            usageKey(kUseFeatureColors[1], "Changer");
            gap(0.4f);
            double sum = 0;
            for (const ElevenDay& d : days) sum += d.total();
            char b[200];
            std::snprintf(b, sizeof(b), TF("%s credits in these 7 days, from your ElevenLabs account. Its days start at midnight UTC."),
                          usageCount((long long)sum).c_str());
            note(b);
        }
        ImGui::EndChild();
    } else if (service == kUseClaude || service == kUseGemini || service == kUseOpenAI) {
        beginPanel("useright", ImVec2(rightW, bodyH), "Last requests in this session");
        const std::vector<UseRecent> recent = useRecent(service);
        if (recent.empty()) {
            note("None yet in this session.");
        } else if (ImGui::BeginTable("##userecent", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                     ImVec2(0, ImGui::GetContentRegionAvail().y - em() * 3.f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(T("Time"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn(T("For"), ImGuiTableColumnFlags_WidthStretch, 2.f);
            ImGui::TableSetupColumn(T("In"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn(T("Cached"), ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn(T("Cost"), ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, kField);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor(2);
            static const char* const kShort[] = {"My voice", "Caption", "While they talk", "Means check", "In my ear", "Changer", "Other"};
            for (const UseRecent& r : recent) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char hm[16];
                const std::time_t ts = std::time_t(r.at);
                std::tm tm{};
#ifdef _WIN32
                localtime_s(&tm, &ts);
#else
                localtime_r(&ts, &tm);
#endif
                std::strftime(hm, sizeof(hm), "%H:%M:%S", &tm);
                ImGui::TextColored(kMuted, "%s", hm);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(T(kShort[std::max(0, std::min(kUseFeatures - 1, r.feature))]));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(usageCount(r.amount.tokensIn + r.amount.cacheRead + r.amount.cacheWritten).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(usageCount(r.amount.cacheRead).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(usageMoney(r.amount.dollars).c_str());
            }
            ImGui::EndTable();
        }
        gap(0.3f);
        note("Only counts and times are kept, never what anyone said.");
        ImGui::EndChild();
    } else {
        beginPanel("useright", ImVec2(rightW, bodyH), "Worth knowing");
        if (service == kUseDeepL)
            note("DeepL is accurate, with textbook wording. With an AI as the translation service, it's the backup for lines the AI "
                 "takes too long for, and it translates your \"Words for DeepL\". The free plan has 500,000 letters a month.");
        else if (service == kUseMicrosoft)
            note("Accurate, textbook wording in 100+ languages, 2 million letters a month free. It can translate Listen's captions on "
                 "its own: pick it on the Listen page under \"Translation service\".");
        else
            note("Free and private: nothing leaves this PC. It uses your graphics card's memory, so a game that needs it can push part "
                 "of the model to the processor, and lines take longer. The log says when that happens.");
        gap(0.5f);
        const std::string role = usageRole(c, service == kUseDeepL ? kDeepL : service == kUseMicrosoft ? kMicrosoft : kLocal);
        if (!role.empty()) note(role);
        ImGui::EndChild();
    }
    (void)act;
}

// The whole window when it isn't the setup, the caption window or the mini window: the sidebar
// on the left, the page on the right.
void appFrame(UiModel& m, UiActions& act, bool picture) {
    const ImVec2 full = ImGui::GetContentRegionAvail();
    const float sideW = std::floor(std::max(em() * 12.f, std::min(em() * 14.5f, full.x * 0.22f)));
    const ImVec4 sideBg = mix(kBg, kPanel, 0.45f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(sideBg.x, sideBg.y, sideBg.z, picture ? gPanelAlpha : 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 0.8f, em() * 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::BeginChild("side", ImVec2(sideW, full.y), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    sidebar(m, act);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em() * 1.4f, em() * 0.9f));
    ImGui::BeginChild("main", ImVec2(0, full.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    pageHeader(m, act);
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) m.findOpen = true;
    if (!gJump.want.empty() && --gJump.framesLeft < 0) gJump.want.clear();  // not shown right now (hidden by another choice)
    if (!m.spendAlert.empty()) spendBanner(m, act);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    switch (m.cfg.page) {
    case kPageHome: homePage(m, act); break;
    case kPageListen: listenPage(m, act); break;
    case kPageText: textPage(m, act); break;
    case kPageVR: vrPage(m, act); break;
    case kPageFeatures: featuresPage(m, act); break;
    case kPageSettings: settingsPage(m, act); break;
    case kPageUsage: usagePage(m, act); break;
    default: voicePage(m, act); break;
    }
    ImGui::EndChild();
    ImGui::EndChild();
}
