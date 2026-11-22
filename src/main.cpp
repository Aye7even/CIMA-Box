// CIMA Box.
//   My voice: microphone -> ElevenLabs -> output device (e.g. a virtual cable).
//   Listen:   a game's or device's sound -> captions in your language (see listen.cpp).
#include "hotkey.h"  // pulls in <windows.h> with the right defines on Windows
#include "i18n.h"

#include "audio.h"  // miniaudio itself is compiled in voiceout.cpp

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <shellapi.h>  // ShellExecuteW: "Open settings folder"
#include <commdlg.h>   // GetOpenFileNameW: Settings > Look > Choose a picture
#include <shlobj.h>    // SHGetKnownFolderPath: the Documents folder
#include <timeapi.h>   // timeBeginPeriod: 1 ms timer precision
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <mutex>
#include <thread>

#include "app.h"
#include "appaudio.h"
#include "logo_data.h"
#include "core.h"
#include "eleven.h"
#include "emotion.h"
#include "livestt.h"
#include "listen.h"
#include "micmatch.h"
#include "applog.h"
#include "bidi.h"
#include "screen.h"
#include "guidepics.h"
#include "servicestatus.h"
#include "usage.h"
#include "voiceout.h"
#include "osc.h"
#include "vroverlay.h"
#include "translate.h"
#include "early.h"
#include "ui.h"

#ifndef CIMA_VERSION
#define CIMA_VERSION "2.6"  // normally set by CMakeLists.txt
#endif
#ifndef CIMA_FULL_VERSION
#define CIMA_FULL_VERSION CIMA_VERSION
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F  // missing from the OpenGL 1.1 header Windows ships
#endif

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- settings file

namespace fs = std::filesystem;

// Where cimabox.json lives, chosen in Settings > About:
//   the settings folder (default): %APPDATA%\CIMA Box (~/.config/cimabox elsewhere)
//   Documents:                     Documents\CIMA Box Config
//   next to the program:           everything in one folder (Desktop, a USB stick...)
// The choice itself is remembered in the settings folder (settings-location.txt). With no choice made,
// a cimabox.json next to CIMABox.exe is used where it is.
static fs::path appDataDir() {
    std::error_code ec;
#ifdef _WIN32
    if (const wchar_t* a = _wgetenv(L"APPDATA"); a && *a) return fs::path(a) / L"CIMA Box";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return fs::path(x) / "cimabox";
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / ".config" / "cimabox";
#endif
    return fs::current_path(ec);
}

static fs::path documentsDir() {
#ifdef _WIN32
    PWSTR p = nullptr;  // the real Documents folder, also when OneDrive has moved it
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p)) && p) {
        const fs::path r(p);
        CoTaskMemFree(p);
        return r;
    }
    if (p) CoTaskMemFree(p);
    if (const wchar_t* u = _wgetenv(L"USERPROFILE"); u && *u) return fs::path(u) / L"Documents";
#else
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h) / "Documents";
#endif
    return appDataDir();
}

static fs::path desktopDir() {
#ifdef _WIN32
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)) && p) {
        const fs::path r(p);
        CoTaskMemFree(p);
        return r;
    }
    if (p) CoTaskMemFree(p);
#endif
    return documentsDir();
}

static fs::path programDir() {
#ifdef _WIN32
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    return fs::path(std::wstring(buf.data(), n)).parent_path();
#else
    std::error_code ec;
    return fs::current_path(ec);
#endif
}

static const char* const kPlaceWords[] = {"settings-folder", "documents", "program"};
static int gPlace = kPlaceSettingsFolder;

static fs::path placeDir(int place) {
    if (place == kPlaceDocuments) return documentsDir() / "CIMA Box Config";
    if (place == kPlaceProgram) return programDir();
    return appDataDir();
}
fs::path settingsDir() { return placeDir(gPlace); }
static fs::path settingsFile() { return settingsDir() / "cimabox.json"; }
static fs::path locationFile() { return appDataDir() / "settings-location.txt"; }

static int choosePlace() {
    std::ifstream lf(locationFile());
    std::string word;
    if (lf >> word)
        for (int i = 0; i < 3; ++i)
            if (word == kPlaceWords[i]) return i;
    // No choice made: a cimabox.json next to the program means "keep it here". (If 2.6 already copied
    // that file into the settings folder, whichever was changed last wins.)
    std::error_code ec;
    const fs::path here = programDir() / "cimabox.json", there = appDataDir() / "cimabox.json";
    if (fs::exists(here, ec)) {
        if (!fs::exists(there, ec)) return kPlaceProgram;
        const auto a = fs::last_write_time(here, ec), b = fs::last_write_time(there, ec);
        if (!ec && a >= b) return kPlaceProgram;
    }
    return kPlaceSettingsFolder;
}

static bool gSettingsExisted = false;  // false on a brand-new install

static void readFeatures(const json& j, Settings& s);

// Everything cimabox.json (or a profile) says, on top of what `s` already holds.
static void settingsFromJson(const json& j, Settings& s) {
    if (!j.is_object()) return;
    try {
        s.apiKey = j.value("api_key", s.apiKey);
        s.engine = j.value("engine", s.engine);
        s.voiceId = j.value("voice_id", s.voiceId);
        s.stsModel = j.value("sts_model", s.stsModel);
        s.ttsModel = j.value("tts_model", s.ttsModel);
        s.vs.stability = j.value("stability", s.vs.stability);
        s.vs.similarity = j.value("similarity", s.vs.similarity);
        s.vs.style = j.value("style", s.vs.style);
        s.vs.speed = j.value("speed", s.vs.speed);
        s.vs.speakerBoost = j.value("speaker_boost", s.vs.speakerBoost);
        s.removeNoise = j.value("remove_noise", s.removeNoise);
        s.spokenLang = j.value("language", s.spokenLang);
        s.targetLang = j.value("target_language", s.targetLang);
        s.translateService = j.value("translate_service", s.translateService);
        s.deeplKey = j.value("deepl_key", s.deeplKey);
        s.claudeKey = j.value("claude_key", s.claudeKey);
        s.geminiKey = j.value("gemini_key", s.geminiKey);
        s.claudeModel = j.value("claude_model", s.claudeModel);
        s.geminiModel = j.value("gemini_model", s.geminiModel);
        s.openaiKey = j.value("openai_key", s.openaiKey);
        s.msKey = j.value("microsoft_key", s.msKey);
        s.msRegion = j.value("microsoft_region", s.msRegion);
        s.aiBackup = j.value("ai_backup", s.aiBackup);
        s.localModel = j.value("local_model", s.localModel);
        s.localGpu = j.value("local_gpu", s.localGpu);
        s.localOnDecline = j.value("local_on_decline", s.localOnDecline);
        s.aiStream = j.value("ai_stream", s.aiStream);
        s.aiPriority = j.value("ai_priority", s.aiPriority);
        s.shortToPlain = j.value("short_to_plain", s.shortToPlain);
        s.rememberLines = j.value("remember_lines", s.rememberLines);
        s.showMeaning = j.value("show_meaning", s.showMeaning);
        s.earlyTranslate = j.value("early_translate", s.earlyTranslate);
        s.speakSentences = j.value("speak_sentences", s.speakSentences);
        s.openaiModel = j.value("openai_model", s.openaiModel);
        s.customStyle = j.value("custom_style", s.customStyle);
        s.speechLevel = j.value("speech_level", s.speechLevel);
        s.persona = j.value("persona", s.persona);
        s.aiTimeoutMs = j.value("ai_timeout_ms", s.aiTimeoutMs);
        s.mirror = j.value("mirror", s.mirror);
        auto strMap = [&](const char* key, std::map<std::string, std::string>& m) {
            if (!j.contains(key) || !j[key].is_object()) return;
            for (auto it = j[key].begin(); it != j[key].end(); ++it)
                if (it.value().is_string()) m[it.key()] = it.value().get<std::string>();
        };
        strMap("dialects", s.variety);
        strMap("custom_dialects", s.customVariety);
        strMap("voice_for_language", s.voiceFor);
        s.myGender = j.value("my_gender", s.myGender);
        s.talkingTo = j.value("talking_to", s.talkingTo);
        s.talkKey = j.value("talking_to_key", s.talkKey);
        s.voicePerLanguage = j.value("voice_per_language", s.voicePerLanguage);
        s.outVolume = j.value("output_volume", s.outVolume);
        s.monVolume = j.value("monitor_volume", s.monVolume);
        s.uiLanguage = j.value("app_language", s.uiLanguage);
        s.checkUpdates = j.value("check_updates", s.checkUpdates);
        s.skipUpdate = j.value("skip_update", s.skipUpdate);
        s.lastSeenVersion = j.value("last_seen_version", s.lastSeenVersion);
        s.usageNotice = j.value("usage_notice", s.usageNotice);
        s.inDevice = j.value("input_device", s.inDevice);
        s.outDevice = j.value("output_device", s.outDevice);
        s.monDevice = j.value("monitor_device", s.monDevice);
        s.monitor = j.value("monitor", s.monitor);
        s.enabled = j.value("enabled", s.enabled);
        s.activation = j.value("activation", s.activation);
        s.thresholdDb = j.value("threshold_db", s.thresholdDb);
        s.holdMs = j.value("hold_ms", s.holdMs);
        s.sentenceWaitMs = std::max(0, std::min(3000, j.value("sentence_wait_ms", s.sentenceWaitMs)));
        s.liveStt = j.value("live_transcription", s.liveStt);
        s.maxSeconds = j.value("max_seconds", s.maxSeconds);
        s.pttKey = j.value("ptt_key", s.pttKey);
        s.page = j.value("page", s.page);
        if (j.contains("home_tiles") && j["home_tiles"].is_array()) {
            s.homeTiles.clear();
            for (const auto& x : j["home_tiles"])
                if (x.is_string()) s.homeTiles.push_back(x.get<std::string>());
        }
        if (j.contains("recent_targets") && j["recent_targets"].is_array()) {
            s.recentTargets.clear();
            for (const auto& x : j["recent_targets"])
                if (x.is_string() && s.recentTargets.size() < 6) s.recentTargets.push_back(x.get<std::string>());
        }
        s.emotion = j.value("emotion", s.emotion);
        s.quality = j.value("audio_quality", s.quality);
        s.matchMic = j.value("match_mic", s.matchMic);
        if (j.contains("look") && j["look"].is_object()) {
            const json& l = j["look"];
            LookSettings& o = s.look;
            o.textScale = std::min(1.3f, std::max(0.85f, l.value("text_scale", o.textScale)));
            o.preset = std::min(int(kLookCount) - 1, std::max(-1, l.value("preset", o.preset)));
            o.accent = l.value("accent", o.accent) & 0xFFFFFF;
            o.background = l.value("background", o.background) & 0xFFFFFF;
            o.panel = l.value("panel", o.panel) & 0xFFFFFF;
            o.text = l.value("text", o.text) & 0xFFFFFF;
            o.picture = l.value("picture", o.picture);
            if (o.picture.find_first_of("/\\:") != std::string::npos) o.picture.clear();  // only a file name in our folder
            o.pictureShow = std::min(0.8f, std::max(0.1f, l.value("picture_show", o.pictureShow)));
        }
        if (j.contains("vr") && j["vr"].is_object()) {
            const json& v = j["vr"];
            VrSettings& r = s.vr;  // "on" is saved: if you use VR captions, they come back on
            r.on = v.value("on", r.on);
            r.placement = v.value("placement", r.placement);
            r.distance = v.value("distance", r.distance);
            r.height = v.value("height", r.height);
            r.sideways = v.value("sideways", r.sideways);
            r.width = v.value("width", r.width);
            r.handWidth = v.value("hand_width", r.handWidth);
            if (v.contains("fixed_pose") && v["fixed_pose"].is_array() && v["fixed_pose"].size() == 12)
                for (size_t i = 0; i < 12; ++i) r.fixedPose[i] = v["fixed_pose"][i].get<float>();
            r.showMe = v.value("show_me", r.showMe);
            r.showOriginal = v.value("show_original", r.showOriginal);
            r.fadeSeconds = v.value("fade_seconds", r.fadeSeconds);
            r.opacity = v.value("opacity", r.opacity);
            r.style.textPx = v.value("text_px", r.style.textPx);
            r.style.maxLines = v.value("max_lines", r.style.maxLines);
            r.style.textColor = v.value("text_color", r.style.textColor);
            r.style.accentColor = v.value("accent_color", r.style.accentColor);
            r.style.meColor = v.value("me_color", r.style.meColor);
            r.style.panelOpacity = v.value("panel_opacity", r.style.panelOpacity);
            r.style.glow = v.value("glow", r.style.glow);
            r.showTalkingTo = v.value("show_talking_to", r.showTalkingTo);
            r.pttButton = v.value("ptt_button", r.pttButton);
            r.buzz = v.value("buzz", r.buzz);
        }
        s.matchAmount = j.value("match_amount", s.matchAmount);
        s.passthrough = j.value("passthrough", s.passthrough);
        s.pttRealVoice = j.value("ptt_real_voice", s.pttRealVoice);
        s.pttTap = j.value("ptt_tap", s.pttTap);
        s.chatbox = j.value("voice_chatbox", s.chatbox);
        s.chatboxShow = j.value("voice_chatbox_show", s.chatboxShow);
        s.textTranslate = j.value("text_translate", s.textTranslate);
        s.textSpeak = j.value("text_speak", s.textSpeak);
        // People who used CIMA Box before the walkthrough existed don't need it.
        s.setupDone = j.value("setup_done", !s.apiKey.empty());
        s.spendWarn = j.value("spend_warn", s.spendWarn);
        s.setupAt = j.value("setup_at", s.setupAt);
        s.setupUse = j.value("setup_use", s.setupUse);
        s.setupRoute = j.value("setup_route", s.setupRoute);
        s.setupAi = j.value("setup_ai", s.setupAi);
        s.spendCredits = std::max(500, j.value("spend_credits", s.spendCredits));
        s.spendDollars = std::max(0.05f, j.value("spend_dollars", s.spendDollars));
        s.spendStop = j.value("spend_stop", s.spendStop);
        s.vs.setCloneStrength = j.value("set_clone_strength", s.vs.setCloneStrength);
        s.vs.cloneStrength = j.value("clone_strength", s.vs.cloneStrength);
        s.emotionStrength = j.value("emotion_strength", s.emotionStrength);
        if (j.contains("usual_voice") && j["usual_voice"].is_object()) {
            const json& b = j["usual_voice"];
            // Saved before 3.0.3 (no "v"): learned the old way, from too little and without the mic's
            // brightness. Starts again (quickly, while you talk).
            if (b.value("v", 0) >= 2) {
                s.baseline.loudDb = b.value("loud_db", s.baseline.loudDb);
                s.baseline.pitch = b.value("pitch", s.baseline.pitch);
                s.baseline.range = b.value("range", s.baseline.range);
                s.baseline.rate = b.value("rate", s.baseline.rate);
                s.baseline.tilt = b.value("tilt", s.baseline.tilt);
                s.baseline.pitchVar = b.value("pitch_var", s.baseline.pitchVar);
                s.baseline.loudVar = b.value("loud_var", s.baseline.loudVar);
                s.baseline.rateVar = b.value("rate_var", s.baseline.rateVar);
                s.baseline.phrases = b.value("phrases", s.baseline.phrases);
                s.baseline.micParts = b.value("mic_parts", s.baseline.micParts);
            }
        }
        if (j.contains("listen") && j["listen"].is_object()) {
            const json& l = j["listen"];
            ListenSettings& ls = s.listen;  // "on" is not saved: captioning never starts by itself
            ls.source = l.value("source", ls.source);
            ls.app = l.value("app", ls.app);
            ls.speakers = l.value("speakers", ls.speakers);
            ls.input = l.value("input", ls.input);
            ls.fromLang = l.value("from", ls.fromLang);
            ls.toLang = l.value("to", ls.toLang);
            ls.thresholdDb = l.value("threshold_db", ls.thresholdDb);
            ls.pace = l.value("pace", ls.pace);
            ls.focus = std::max(0, std::min(2, l.value("focus", ls.focus)));
            ls.tellApart = l.value("tell_speakers_apart", ls.tellApart);
            ls.showOriginal = l.value("show_original", ls.showOriginal);
            ls.onlyOtherLanguages = l.value("only_other_languages", ls.onlyOtherLanguages);
            ls.chatbox = l.value("chatbox", ls.chatbox);
            ls.textScale = l.value("text_scale", ls.textScale);
        }
        readFeatures(j, s);
    } catch (const json::exception&) { /* wrong type somewhere: keep defaults for the rest */ }
}

// Values that are out of range (an old or hand-edited file) put back in range.
static void tidySettings(Settings& s) {
    if (s.engine < kVoiceChanger || s.engine > kTranslate) s.engine = kVoiceChanger;
    if (const Language* l = findLanguage(s.spokenLang)) s.spokenLang = l->code; else s.spokenLang.clear();
    if (!findLanguage(s.targetLang)) s.targetLang = "ja";
    if (s.page < kPageVoice || s.page > kPageHome) s.page = kPageHome;
    {
        VrSettings& r = s.vr;
        if (r.placement < kVrHead || r.placement > kVrFixed) r.placement = kVrHead;
        r.distance = std::max(0.3f, std::min(4.f, r.distance));
        r.height = std::max(-1.5f, std::min(1.5f, r.height));
        r.sideways = std::max(-1.5f, std::min(1.5f, r.sideways));
        r.width = std::max(0.2f, std::min(3.f, r.width));
        r.handWidth = std::max(0.08f, std::min(0.6f, r.handWidth));
        r.fadeSeconds = std::max(0.f, std::min(60.f, r.fadeSeconds));
        r.opacity = std::max(0.2f, std::min(1.f, r.opacity));
        r.style.textPx = std::max(20.f, std::min(72.f, r.style.textPx));
        r.style.maxLines = std::max(1, std::min(8, r.style.maxLines));
        r.style.panelOpacity = std::max(0.f, std::min(1.f, r.style.panelOpacity));
    }
    if (s.translateService < kMyMemory || s.translateService > kMicrosoft) s.translateService = kMyMemory;
    if (s.speechLevel < kLevelPolite || s.speechLevel > kLevelCustom) s.speechLevel = kLevelCasual;
    s.aiTimeoutMs = std::max(800, std::min(6000, s.aiTimeoutMs));
    if (s.myGender < kGenderUnknown || s.myGender > kGenderNonbinary) s.myGender = kGenderUnknown;
    if (s.talkingTo < 0 || s.talkingTo >= kTalkCount) s.talkingTo = kTalkAuto;
    s.outVolume = std::max(0.f, std::min(2.f, s.outVolume));
    s.monVolume = std::max(0.f, std::min(2.f, s.monVolume));
    if (s.claudeModel.empty()) s.claudeModel = "claude-haiku-5-5";
    if (s.geminiModel.empty()) s.geminiModel = "gemini-3.5-flash-lite";
    if (s.openaiModel.empty()) s.openaiModel = "gpt-6-luna";
    if (s.emotionStrength != kEmotionStrong) s.emotionStrength = kEmotionSubtle;
    s.matchAmount = std::max(0.f, std::min(1.f, s.matchAmount));
    if (s.quality < kQualityStandard || s.quality > kQualityBest) s.quality = kQualityHigh;
    s.vs.cloneStrength = std::max(0.f, std::min(1.f, s.vs.cloneStrength));
    if (s.chatboxShow < kShowSaid || s.chatboxShow > kShowBoth) s.chatboxShow = kShowSaid;
    if (s.baseline.phrases < 0) s.baseline = VoiceBaseline();
    ListenSettings& ls = s.listen;
    if (ls.source < kSrcApp || ls.source > kSrcInput) ls.source = kSrcInput;
    if (ls.pace < kPaceQuick || ls.pace > kPaceFull) ls.pace = kPaceBalanced;
    if (const Language* l = findLanguage(ls.fromLang)) ls.fromLang = l->code; else ls.fromLang.clear();
    if (!findLanguage(ls.toLang)) ls.toLang = "en";
    ls.textScale = std::max(0.9f, std::min(2.4f, ls.textScale));
}

// Going back to an earlier version: that version doesn't know the newer settings (a newer key, a
// newer feature's choices) and drops them the first time it saves. So before going back, everything
// is kept in a file of its own, and once a version at least as new as the one that kept it starts,
// whatever is missing from the settings is put back from it (nothing that's there is changed).
static std::string gRestoredFrom;  // for the log, which opens after the settings are read
static fs::path keptSettingsFile() { return settingsDir() / "cimabox.before-going-back.json"; }

static void mergeMissing(json& into, const json& from) {
    for (auto it = from.begin(); it != from.end(); ++it) {
        if (it.key() == "saved_by_version") continue;
        if (!into.contains(it.key())) into[it.key()] = it.value();
        else if (into[it.key()].is_object() && it.value().is_object()) mergeMissing(into[it.key()], it.value());
    }
}

static void restoreAfterGoingBack(json& j) {
    std::error_code ec;
    const fs::path kept = keptSettingsFile();
    if (!j.is_object() || !fs::exists(kept, ec)) return;
    std::ifstream k(kept);
    const json b = k ? json::parse(k, nullptr, false) : json();
    k.close();
    if (!b.is_object()) { fs::remove(kept, ec); return; }
    const std::string by = b.value("saved_by_version", "");
    if (by.empty() || compareVersions(CIMA_FULL_VERSION, by) < 0) return;  // still on the older version
    mergeMissing(j, b);
    fs::remove(kept, ec);
    gRestoredFrom = by;
}

static void keepSettingsForLater(const std::string& settingsText) {
    std::error_code ec;
    const fs::path kept = keptSettingsFile();
    // Going back twice in a row: keep the copy made by the newest version.
    if (fs::exists(kept, ec)) {
        std::ifstream k(kept);
        const json b = k ? json::parse(k, nullptr, false) : json();
        if (b.is_object() && compareVersions(b.value("saved_by_version", ""), CIMA_FULL_VERSION) > 0) return;
    }
    json j = json::parse(settingsText, nullptr, false);
    if (!j.is_object()) return;
    j["saved_by_version"] = CIMA_FULL_VERSION;
    std::ofstream out(kept, std::ios::binary | std::ios::trunc);
    out << j.dump(2);
}

static void loadSettings(Settings& s) {
    std::error_code ec;
    gPlace = choosePlace();
    fs::path file = settingsFile();
    if (!fs::exists(file, ec))  // settings from before the app was called CIMA Box
        for (const fs::path& old : {programDir() / "voicechanger.json", fs::current_path(ec) / "voicechanger.json"})
            if (fs::exists(old, ec)) {
                file = old;
                break;
            }
    std::ifstream f(file);
    gSettingsExisted = bool(f);
    json j = f ? json::parse(f, nullptr, false) : json();
    restoreAfterGoingBack(j);
    settingsFromJson(j, s);
    tidySettings(s);
    if (s.apiKey.empty())
        if (const char* env = std::getenv("ELEVENLABS_API_KEY")) s.apiKey = env;
}

// ---- the Features page's settings ("features" in cimabox.json)
static json featuresJson(const Settings& s) {
    const FeatureSettings& f = s.fx;
    const ScreenSettings& c = s.screen;
    json words = json::array();
    for (const WordFix& w : f.words) words.push_back({{"heard", w.heard}, {"write", w.write}});
    json profiles = json::object();
    for (const auto& kv : f.profiles) profiles[kv.first] = kv.second;
    return {
        {"voice_swear", f.voiceSwear}, {"mask_captions", f.maskCaptions}, {"words", words},
        {"deepl_words_on", f.deeplWordsOn}, {"deepl_words", f.deeplWords},
        {"quick_phrases", f.quickPhrases}, {"sent_counts", f.sentCounts},
        {"hear_them", f.hearThem}, {"ear_voice", f.earVoice}, {"ear_device", f.earDevice}, {"ear_volume", f.earVolume},
        {"live_captions", f.liveCaptions}, {"live_translate", f.liveTranslate}, {"answer_theirs", f.answerTheirs}, {"my_languages", f.myLanguages},
        {"around_languages", f.aroundLanguages}, {"preview_ms", f.previewMs}, {"cancel_key", f.cancelKey},
        {"best", f.best}, {"gender_from_words", f.genderFromWords}, {"learning", f.learning},
        {"saved_lines", f.savedLines}, {"keep_history", f.keepHistory}, {"sound_labels", f.soundLabels},
        {"ignore_noise", f.ignoreNoise}, {"high_contrast", f.highContrast}, {"voice_ids", f.voiceIds},
        {"click_through", f.clickThrough}, {"overlay_opacity", f.overlayOpacity}, {"overlay_key", f.overlayKey},
        {"real_voice_key", f.realVoiceKey}, {"stop_key", f.stopKey}, {"profiles", profiles},
        {"screen", {{"on", c.on}, {"agreed", c.agreed}, {"app", c.app}, {"reader", c.reader}, {"read_lang", c.readLang}, {"to_lang", c.toLang},
                    {"skip_preview", c.skipPreview}, {"key", c.key}, {"watch", c.watch}, {"watch_seconds", c.watchSeconds},
                    {"area", {c.area[0], c.area[1], c.area[2], c.area[3]}}, {"to_vr", c.toVr}}},
    };
}

static void readFeatures(const json& root, Settings& s) {
    if (!root.contains("features") || !root["features"].is_object()) return;
    const json& j = root["features"];
    FeatureSettings& f = s.fx;
    auto strList = [&](const char* key, std::vector<std::string>& v) {
        if (!j.contains(key) || !j[key].is_array()) return;
        v.clear();
        for (const json& x : j[key])
            if (x.is_string()) v.push_back(x.get<std::string>());
    };
    f.voiceSwear = std::max(0, std::min(2, j.value("voice_swear", f.voiceSwear)));
    f.maskCaptions = j.value("mask_captions", f.maskCaptions);
    if (j.contains("words") && j["words"].is_array()) {
        f.words.clear();
        for (const json& w : j["words"])
            if (w.is_object()) f.words.push_back({w.value("heard", std::string()), w.value("write", std::string())});
    }
    strList("quick_phrases", f.quickPhrases);
    f.deeplWordsOn = j.value("deepl_words_on", f.deeplWordsOn);
    strList("deepl_words", f.deeplWords);
    if (j.contains("sent_counts") && j["sent_counts"].is_object())
        for (auto it = j["sent_counts"].begin(); it != j["sent_counts"].end(); ++it)
            if (it.value().is_number_integer()) f.sentCounts[it.key()] = it.value().get<int>();
    f.hearThem = j.value("hear_them", f.hearThem);
    f.earVoice = j.value("ear_voice", f.earVoice);
    f.earDevice = j.value("ear_device", f.earDevice);
    f.earVolume = std::max(0.f, std::min(2.f, j.value("ear_volume", f.earVolume)));
    f.liveCaptions = j.value("live_captions", f.liveCaptions);
    f.liveTranslate = j.value("live_translate", f.liveTranslate);
    f.answerTheirs = j.value("answer_theirs", f.answerTheirs);
    strList("my_languages", f.myLanguages);
    strList("around_languages", f.aroundLanguages);
    f.previewMs = std::max(0, std::min(3000, j.value("preview_ms", f.previewMs)));
    f.cancelKey = j.value("cancel_key", f.cancelKey);
    f.best = j.value("best", f.best);
    f.genderFromWords = j.value("gender_from_words", f.genderFromWords);
    f.learning = j.value("learning", f.learning);
    strList("saved_lines", f.savedLines);
    f.keepHistory = j.value("keep_history", f.keepHistory);
    f.soundLabels = j.value("sound_labels", f.soundLabels);
    f.ignoreNoise = j.value("ignore_noise", f.ignoreNoise);
    f.highContrast = j.value("high_contrast", f.highContrast);
    f.voiceIds = j.value("voice_ids", f.voiceIds);
    f.clickThrough = j.value("click_through", f.clickThrough);
    f.overlayOpacity = std::max(0.3f, std::min(1.f, j.value("overlay_opacity", f.overlayOpacity)));
    f.overlayKey = j.value("overlay_key", f.overlayKey);
    f.realVoiceKey = j.value("real_voice_key", f.realVoiceKey);
    f.stopKey = j.value("stop_key", f.stopKey);
    if (j.contains("profiles") && j["profiles"].is_object())
        for (auto it = j["profiles"].begin(); it != j["profiles"].end(); ++it)
            if (it.value().is_string()) f.profiles[it.key()] = it.value().get<std::string>();
    if (j.contains("screen") && j["screen"].is_object()) {
        const json& c = j["screen"];
        ScreenSettings& sc = s.screen;
        sc.on = c.value("on", sc.on);
        sc.agreed = c.value("agreed", sc.agreed);
        sc.app = c.value("app", sc.app);
        sc.reader = std::max(0, std::min(1, c.value("reader", sc.reader)));
        sc.readLang = c.value("read_lang", sc.readLang);
        sc.toLang = c.value("to_lang", sc.toLang);
        sc.skipPreview = c.value("skip_preview", sc.skipPreview);
        sc.key = c.value("key", sc.key);
        sc.watch = c.value("watch", sc.watch);
        sc.watchSeconds = std::max(1.f, std::min(30.f, c.value("watch_seconds", sc.watchSeconds)));
        if (c.contains("area") && c["area"].is_array() && c["area"].size() == 4)
            for (size_t i = 0; i < 4; ++i) sc.area[i] = std::max(0.f, std::min(1.f, c["area"][i].get<float>()));
        if (sc.area[2] - sc.area[0] < 0.02f || sc.area[3] - sc.area[1] < 0.02f) { sc.area[0] = sc.area[1] = 0; sc.area[2] = sc.area[3] = 1; }
        sc.toVr = c.value("to_vr", sc.toVr);
    }
}

static std::string settingsJson(const Settings& s) {
    json j = {
        {"api_key", s.apiKey}, {"engine", s.engine}, {"voice_id", s.voiceId}, {"sts_model", s.stsModel},
        {"tts_model", s.ttsModel}, {"stability", s.vs.stability}, {"similarity", s.vs.similarity},
        {"style", s.vs.style}, {"speed", s.vs.speed}, {"speaker_boost", s.vs.speakerBoost},
        {"remove_noise", s.removeNoise}, {"language", s.spokenLang}, {"target_language", s.targetLang},
        {"translate_service", s.translateService}, {"deepl_key", s.deeplKey},
        {"claude_key", s.claudeKey}, {"gemini_key", s.geminiKey}, {"claude_model", s.claudeModel},
        {"gemini_model", s.geminiModel}, {"openai_key", s.openaiKey}, {"openai_model", s.openaiModel},
        {"microsoft_key", s.msKey}, {"microsoft_region", s.msRegion}, {"ai_backup", s.aiBackup},
        {"local_model", s.localModel}, {"local_gpu", s.localGpu}, {"local_on_decline", s.localOnDecline}, {"ai_stream", s.aiStream}, {"ai_priority", s.aiPriority},
        {"short_to_plain", s.shortToPlain}, {"remember_lines", s.rememberLines}, {"show_meaning", s.showMeaning}, {"early_translate", s.earlyTranslate}, {"speak_sentences", s.speakSentences},
        {"speech_level", s.speechLevel}, {"custom_style", s.customStyle}, {"persona", s.persona},
        {"ai_timeout_ms", s.aiTimeoutMs}, {"mirror", s.mirror}, {"input_device", s.inDevice},
        {"output_device", s.outDevice}, {"monitor_device", s.monDevice}, {"monitor", s.monitor},
        {"enabled", s.enabled}, {"activation", s.activation}, {"threshold_db", s.thresholdDb},
        {"hold_ms", s.holdMs}, {"sentence_wait_ms", s.sentenceWaitMs}, {"live_transcription", s.liveStt}, {"max_seconds", s.maxSeconds}, {"ptt_key", s.pttKey},
        {"page", s.page}, {"home_tiles", s.homeTiles}, {"recent_targets", s.recentTargets},
        {"emotion", s.emotion}, {"emotion_strength", s.emotionStrength},
        {"audio_quality", s.quality}, {"match_mic", s.matchMic}, {"match_amount", s.matchAmount}, {"passthrough", s.passthrough}, {"ptt_real_voice", s.pttRealVoice}, {"ptt_tap", s.pttTap},
        {"voice_chatbox", s.chatbox}, {"voice_chatbox_show", s.chatboxShow},
        {"text_translate", s.textTranslate}, {"text_speak", s.textSpeak}, {"setup_done", s.setupDone},
        {"setup_at", s.setupAt}, {"setup_use", s.setupUse}, {"setup_route", s.setupRoute}, {"setup_ai", s.setupAi},
        {"spend_warn", s.spendWarn}, {"spend_credits", s.spendCredits}, {"spend_dollars", s.spendDollars}, {"spend_stop", s.spendStop},
        {"set_clone_strength", s.vs.setCloneStrength}, {"clone_strength", s.vs.cloneStrength},
        {"dialects", s.variety}, {"custom_dialects", s.customVariety}, {"voice_for_language", s.voiceFor},
        {"my_gender", s.myGender}, {"talking_to", s.talkingTo}, {"talking_to_key", s.talkKey},
        {"voice_per_language", s.voicePerLanguage}, {"output_volume", s.outVolume}, {"monitor_volume", s.monVolume},
        {"app_language", s.uiLanguage}, {"check_updates", s.checkUpdates}, {"skip_update", s.skipUpdate}, {"last_seen_version", s.lastSeenVersion}, {"usage_notice", s.usageNotice},
        {"usual_voice", {{"v", 2}, {"loud_db", s.baseline.loudDb}, {"pitch", s.baseline.pitch}, {"range", s.baseline.range},
                         {"rate", s.baseline.rate}, {"tilt", s.baseline.tilt}, {"pitch_var", s.baseline.pitchVar},
                         {"loud_var", s.baseline.loudVar}, {"rate_var", s.baseline.rateVar}, {"phrases", s.baseline.phrases},
                         {"mic_parts", s.baseline.micParts}}},
    };
    j["look"] = {{"text_scale", s.look.textScale}, {"preset", s.look.preset}, {"accent", s.look.accent},
                 {"background", s.look.background}, {"panel", s.look.panel}, {"text", s.look.text},
                 {"picture", s.look.picture}, {"picture_show", s.look.pictureShow}};
    {
        const VrSettings& r = s.vr;
        json pose = json::array();
        for (float v : r.fixedPose) pose.push_back(v);
        j["vr"] = {
            {"on", r.on}, {"placement", r.placement}, {"distance", r.distance}, {"height", r.height},
            {"sideways", r.sideways}, {"width", r.width}, {"hand_width", r.handWidth}, {"fixed_pose", pose},
            {"show_me", r.showMe}, {"show_original", r.showOriginal}, {"fade_seconds", r.fadeSeconds},
            {"opacity", r.opacity}, {"text_px", r.style.textPx}, {"max_lines", r.style.maxLines},
            {"text_color", r.style.textColor}, {"accent_color", r.style.accentColor}, {"me_color", r.style.meColor},
            {"panel_opacity", r.style.panelOpacity}, {"glow", r.style.glow}, {"show_talking_to", r.showTalkingTo},
            {"ptt_button", r.pttButton}, {"buzz", r.buzz},
        };
    }
    const ListenSettings& ls = s.listen;
    j["listen"] = {
        {"source", ls.source}, {"app", ls.app}, {"speakers", ls.speakers}, {"input", ls.input},
        {"from", ls.fromLang}, {"to", ls.toLang}, {"threshold_db", ls.thresholdDb}, {"pace", ls.pace}, {"focus", ls.focus},
        {"tell_speakers_apart", ls.tellApart}, {"show_original", ls.showOriginal},
        {"only_other_languages", ls.onlyOtherLanguages}, {"chatbox", ls.chatbox}, {"text_scale", ls.textScale},
    };
    j["features"] = featuresJson(s);
    return j.dump(2);
}

// A profile: everything but the keys and what belongs to this PC's install.
static std::string profileJson(const Settings& s, bool shared = false) {
    json j = json::parse(settingsJson(s), nullptr, false);
    for (const char* k : {"api_key", "deepl_key", "claude_key", "gemini_key", "openai_key", "microsoft_key", "microsoft_region", "page", "setup_done", "setup_at",
                          "setup_use", "setup_route", "setup_ai", "last_seen_version", "usual_voice", "usage_notice"})
        j.erase(k);
    if (j.contains("features")) {
        json& f = j["features"];
        for (const char* k : {"profiles", "sent_counts", "saved_lines"}) f.erase(k);  // not settings: what you typed and kept
        if (shared) {  // for someone else: nothing personal you wrote, and their own PC decides about the screen
            for (const char* k : {"words", "quick_phrases", "deepl_words"}) f.erase(k);
            if (j.contains("look")) j["look"].erase("picture");
            if (f.contains("screen")) for (const char* k : {"on", "agreed", "watch"}) f["screen"].erase(k);
        }
    }
    return j.dump();
}

// Loads a profile (or a shared setup) over the current settings. Keys and the install stay.
static void applyProfile(Settings& s, const std::string& text) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return;
    const auto profiles = s.fx.profiles;
    const int page = s.page;
    const bool agreed = s.screen.agreed;
    settingsFromJson(j, s);
    tidySettings(s);
    s.fx.profiles = profiles;
    s.page = page;
    // The screen translation notice is agreed to on this PC, by this person: a profile (which may
    // have come from someone else) can't do that for them, or switch capturing on before they have.
    s.screen.agreed = agreed;
    if (!agreed) s.screen.on = s.screen.watch = false;
}

// Written to a temporary file first, so a crash mid-write never leaves a broken settings file.
static std::string gLastSaved;
static void saveSettings(const Settings& s) {
    const std::string text = settingsJson(s);
    if (text == gLastSaved) return;
    std::error_code ec;
    const fs::path file = settingsFile(), tmp = file.parent_path() / "cimabox.json.tmp";
    fs::create_directories(file.parent_path(), ec);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << text;
        if (!out.flush()) return;
    }
    fs::rename(tmp, file, ec);
    if (!ec) gLastSaved = text;
}

// Settings > About > "Save settings in": write the file in the new place, then remove the old one.
static bool moveSettings(const Settings& s, int place, std::string& err) {
    if (place == gPlace) return true;
    std::error_code ec;
    const fs::path oldFile = settingsFile();
    const int oldPlace = gPlace;
    gPlace = place;
    gLastSaved.clear();
    saveSettings(s);
    if (gLastSaved.empty()) {  // that folder can't be written to (e.g. the program is in Program Files)
        gPlace = oldPlace;
        saveSettings(s);
        err = "Couldn't save there, so your settings stayed where they were.";
        return false;
    }
    if (!fs::equivalent(oldFile, settingsFile(), ec)) fs::remove(oldFile, ec);
    fs::create_directories(appDataDir(), ec);
    std::ofstream(locationFile()) << kPlaceWords[place];
    err.clear();
    return true;
}

// ---------------------------------------------------------------- shared state

// Microphone and playback devices all run at 48 kHz: full-quality passthrough, and room for the
// 44.1 kHz voice of the High and Best qualities. Whatever ElevenLabs sends is converted to this.
constexpr int kDeviceRate = 48000;

struct Phrase {
    std::vector<int16_t> pcm;   // 16 kHz: speech-to-text, and the voice changer at Standard quality
    std::vector<int16_t> full;  // the same at kDeviceRate: the voice changer at High and Best
    std::string text;           // typed on the Text page instead (no audio)
    bool test = false;          // the setup's test sound: only played, nowhere else
    std::chrono::steady_clock::time_point ended{};  // when you stopped talking (mic phrases)
    std::shared_future<LiveText> live;  // its words from live transcription (when it was on)
    bool midCut = false;                // cut while you were still talking, after a finished sentence: its last words go on
};

struct Sink {  // one playback device fed by a ring buffer
    SpscRing<int16_t> ring{size_t(kDeviceRate) * 120};
    std::atomic<bool> flush{false};
    // Your real voice comes from the mic 10 ms at a time, and not exactly on time. While it plays,
    // the device waits for this many samples before starting (and again after running dry), so a
    // late block becomes a short wait at the start instead of a gap (a crackle) in the middle.
    std::atomic<int> cushion{0};
    std::atomic<bool> filling{false};
    ma_device dev;
    std::atomic<bool> on{false};
    // What was asked for, so a device that disappears (unplugged) can be opened again.
    std::string name;       // empty = system default
    bool wanted = false;
    bool fellBack = false;  // the named device wasn't there, so the system default was opened
    const char* what = "Output";
};

// The conversation so far, newest last: what you said and what the others said (from Listen).
// Gives the AI translator its context, and feeds the VR captions.
struct ConvoLine {
    bool me = false;
    bool typed = false;    // yours, typed on the Text page
    std::string original;  // as spoken
    std::string shown;     // translation, or the same as original
    std::string language;  // spoken language name, for the others' lines
    int speaker = -1;      // "Voice 1/2" inside one caption
    int voice = 0;         // Gender guessed from their voice (others only)
    double at = 0;         // glfwGetTime()-like seconds since start, for fading
    uint64_t id = 0;       // stays the same for this line
    int voiceId = -1;      // the same voice across captions (others only, a best guess)
    bool screen = false;   // read from the screen
    std::string code;      // their language's code ("" = unknown)
};

struct App {
    std::mutex mtx;  // guards: cfg, voices, models, catalogMsg, lastError, heardText, sentText, jobs, warmWanted
    Settings cfg;
    std::vector<Voice> voices;
    std::vector<Model> models;
    std::string catalogMsg, lastError, heardText, sentText, delivery;
    std::string meaningText, meaningFor;  // the check of a spoken line, and the line it belongs to
    std::string sentTo, sentMine;         // the languages of sentText (empty when it wasn't translated)
    VoiceBaseline baseline;  // learned in the worker; copied into the settings for saving
    int catalogVersion = 0;
    std::deque<Phrase> jobs;
    bool warmWanted = false;
    // Early translation: the words heard so far in the phrase being spoken, when it began, the
    // language last spoken, and a nudge for the worker when a pause may be its end. (mtx)
    std::string livePartial, lastSpokenCode;
    Clock::time_point phraseOpened{};
    bool earlyWanted = false, earlyQuiet = false;
    std::condition_variable jobCv;

    std::atomic<bool> running{true}, loading{false}, busy{false}, cancel{false};
    std::atomic<bool> pttDown{false}, recording{false};
    std::atomic<bool> holding{false};           // the start of a sentence, waiting a moment for the rest
    std::atomic<int> listener{0};               // Listener the translation uses (Talking to, resolved)
    std::atomic<float> outGain{1.f}, monGain{1.f};
    std::atomic<int> pttKey{0}, talkKey{0}, talkPresses{0};  // hotkeys, read by the key thread
    std::atomic<bool> pttTap{false}, pttActive{false}, vrButton{false};  // push to talk: tap mode, in use, VR button
    std::atomic<bool> checkQuiet{false};  // Check my setup: send nothing to the output for a moment
    std::atomic<bool> binding{false};
    std::atomic<float> levelDb{-100.f};
    std::atomic<int> sttMs{0}, translateMs{0}, voiceMs{0}, totalMs{0};
    std::atomic<bool> sttLive{false};
    std::atomic<bool> earlyUsed{false};  // the last line was translated during the pause, ahead of time

    ma_context ctx;
    std::vector<ma_device_info> capInfos, playInfos;
    ma_device capDev;
    bool capOn = false;
    std::string capName;      // the microphone asked for (empty = system default)
    bool capFellBack = false;  // it wasn't there, so the system default was opened
    std::atomic<bool> deviceEvent{false};  // a device stopped by itself (unplugged, driver reset)
    SpscRing<float> capRing{size_t(kDeviceRate) * 5};  // mic at the output rate (passthrough quality)
    Sink out, mon;
    MicMatch match{kDeviceRate};
    std::atomic<bool> passing{false};
    std::string chatText;  // waiting to go to VRChat's chatbox (guarded by mtx)
    // Audio quality the plan turned out to allow: a refused format drops a step for this session.
    std::atomic<int> qualityCap{kQualityBest};
    std::atomic<int> mp3Step{0};  // 1: 192 kbps MP3 refused, use 128 kbps
    std::string qualityNote;      // guarded by mtx
    std::string translateVia;     // guarded by mtx
    std::deque<ConvoLine> convo;  // guarded by mtx
    uint64_t convoVersion = 0;    // guarded by mtx; changes whenever convo does
    uint64_t nextConvoId = 1;     // guarded by mtx

    // ---- Features page
    Sink ear;                     // "Hear them in my ear" and learning-mode replays
    Sink preview;                 // voice samples, in your headphones (opened when first used)
    std::deque<std::pair<std::string, std::string>> earJobs;  // text, language code; guarded by mtx
    std::condition_variable earCv;
    std::atomic<float> earGain{0.9f};
    std::atomic<bool> realVoice{false};      // "my real voice" key held
    std::atomic<bool> cancelPending{false};  // don't say what's about to be spoken
    std::string pendingSay;                  // guarded by mtx: what's about to be spoken
    double pendingUntil = 0;                 // guarded by mtx
    std::atomic<int> realVoiceKey{0}, stopKey{0}, cancelKey{0}, overlayKey{0}, screenKey{0};
    std::atomic<int> stopPresses{0}, cancelPresses{0}, overlayPresses{0}, screenPresses{0};
    std::atomic<bool> keepHistory{false};
};
static App g;
static Listener gListen;  // the Listen page's own pipeline; runs next to the voice changer
static VrOverlay gVr;     // captions inside the headset (idle unless switched on)
// Buttons in the SteamVR menu (ids; ranges for the numbered ones).
enum {
    kMenuTalk = 0,        // + TalkingTo
    kMenuScreen = 10,
    kMenuOnOff = 20,
    kMenuEngine = 30,     // + engine
    kMenuLang = 40,       // + position in the row
    kMenuSwap = 49,
    kMenuListen = 50, kMenuEar, kMenuSmaller, kMenuBigger, kMenuPlace, kMenuResetVr,
    kMenuStop = 60, kMenuCancel,
};

// Push to talk from the key or the VR controller button, held or tapped (tap to start, tap again
// to stop). The controller gives a short buzz when recording starts and stops.
struct PttInput {
    bool was = false, latched = false, last = false;
    bool update(bool physical, bool tap, bool active) {
        if (!active) latched = false;
        bool down;
        if (tap) {
            if (physical && !was) latched = !latched;
            down = latched;
        } else {
            latched = false;
            down = physical;
        }
        was = physical;
        down = down && active;
        if (down != last && g.vrButton) gVr.buzz();
        last = down;
        return down;
    }
};
static StatusWatcher gStatus;  // ElevenLabs, Claude, Gemini, DeepL status pages
static UsageWatcher gUsage;    // ElevenLabs credits and DeepL characters left
static const auto gStart = std::chrono::steady_clock::now();
static double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count(); }

// ---- Keep a history (Features page): one text file per day in the settings folder's "history".
static fs::path historyDir() { return settingsDir() / "history"; }
static void writeHistory(const ConvoLine& l) {
    const std::time_t t = std::time(nullptr);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char day[16], clock[8];
    std::strftime(day, sizeof(day), "%Y-%m-%d", &tm);
    std::strftime(clock, sizeof(clock), "%H:%M", &tm);
    std::error_code ec;
    fs::create_directories(historyDir(), ec);
    std::ofstream f(historyDir() / (std::string(day) + ".txt"), std::ios::app | std::ios::binary);
    if (!f) return;
    const std::string who = l.me ? (l.typed ? "Me (typed)" : "Me") : l.screen ? "Screen" : (l.language.empty() ? "Them" : "Them (" + l.language + ")");
    f << clock << "  " << who << ": " << l.shown;
    if (!l.original.empty() && l.original != l.shown) f << "   [" << l.original << "]";
    f << "\n";
}
// The newest lines (newest first) that contain `query`, from the newest files.
static std::vector<std::string> searchHistory(const std::string& query) {
    std::vector<std::string> out;
    auto low = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string q = low(query);
    if (q.find_first_not_of(' ') == std::string::npos) return out;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(historyDir(), ec))
        if (e.path().extension() == ".txt") files.push_back(e.path());
    std::sort(files.rbegin(), files.rend());
    for (const fs::path& file : files) {
        std::ifstream f(file, std::ios::binary);
        std::vector<std::string> hits;
        for (std::string line; std::getline(f, line);)
            if (low(line).find(q) != std::string::npos) hits.push_back(file.stem().u8string() + " " + line);
        for (auto it = hits.rbegin(); it != hits.rend() && out.size() < 40; ++it) out.push_back(*it);
        if (out.size() >= 40) break;
    }
    return out;
}

// mtx held
static void addConvo(ConvoLine line) {
    line.at = nowSeconds();
    line.id = g.nextConvoId++;
    if (g.keepHistory) writeHistory(line);
    g.convo.push_back(std::move(line));
    while (g.convo.size() > 60) g.convo.pop_front();
    ++g.convoVersion;
}

// The translation settings as the translator wants them.
void applyVariety(const Settings& s, const std::string& langCode, TranslateSetup& t) {
    t.variety.clear();
    t.varietyName.clear();
    t.deeplVariant.clear();
    const auto it = s.variety.find(langCode);
    if (it == s.variety.end() || it->second.empty()) return;  // the standard language
    const Variety* v = findVariety(it->second);
    if (it->second == "custom") {
        const auto c = s.customVariety.find(langCode);
        std::string typed = c == s.customVariety.end() ? std::string() : c->second;
        while (!typed.empty() && std::isspace(static_cast<unsigned char>(typed.back()))) typed.pop_back();
        while (!typed.empty() && std::isspace(static_cast<unsigned char>(typed.front()))) typed.erase(typed.begin());
        if (typed.empty()) return;
        v = findVariety(typed);  // "ar-KW" typed by hand is still the known Kuwaiti
        if (!v) {
            const Language* l = findLanguage(langCode);
            t.variety = typed + " (a regional variety of " + std::string(l ? l->name : langCode.c_str()) + ")";
            t.varietyName = typed;
            return;
        }
    }
    if (!v || langCode != v->lang) return;
    t.variety = v->ai;
    t.varietyName = v->name;
    t.deeplVariant = v->deepl;
}

// `speaking`: My voice (your dialect, gender, who you talk to). Listen sets its own per line.
// The local model as it is right now, for every thread that translates (the main loop keeps it
// up to date from LocalAi).
struct LocalNow { std::string url, key, model, note; };
static std::mutex gLocalMx;
static LocalNow gLocalNow;

static TranslateSetup translateSetup(const Settings& s, bool speaking = true) {
    TranslateSetup t;
    if (speaking) {
        applyVariety(s, s.targetLang, t);
        t.speaker = s.myGender;
        t.listener = g.listener.load();
    }
    t.service = s.translateService;
    t.deeplKey = s.deeplKey;
    t.claudeKey = s.claudeKey;
    t.geminiKey = s.geminiKey;
    t.claudeModel = s.claudeModel;
    t.geminiModel = s.geminiModel;
    t.openaiKey = s.openaiKey;
    t.msKey = s.msKey;
    t.msRegion = s.msRegion;
    t.aiBackup = s.aiBackup;
    t.localOnDecline = s.localOnDecline;
    t.stream = s.aiStream;
    t.priority = s.aiPriority;
    t.shortToPlain = s.shortToPlain;
    t.rememberLines = s.rememberLines;
    {   // a model on this PC, when one is loaded (as the service, or as another AI's backup)
        std::lock_guard<std::mutex> lk(gLocalMx);
        t.localUrl = gLocalNow.url;
        t.localKey = gLocalNow.key;
        t.localModel = gLocalNow.model;
        t.localNote = gLocalNow.note;
    }
    t.openaiModel = s.openaiModel;
    t.level = s.speechLevel;
    t.customStyle = s.customStyle;
    t.persona = s.persona;
    t.timeoutMs = s.aiTimeoutMs;
    t.wordNote = wordListNote(s.fx.words);
    if (speaking) t.swear = s.fx.voiceSwear;
    if (speaking && s.fx.deeplWordsOn) t.deeplWords = s.fx.deeplWords;
    else t.readGender = s.fx.genderFromWords;
    if (s.fx.best) {  // Best translations: the bigger models (unless one was picked by hand), and more time
        if (t.claudeModel == "claude-haiku-5-5") t.claudeModel = "claude-sonnet-5-5";
        if (t.geminiModel == "gemini-3.5-flash-lite") t.geminiModel = "gemini-3.6-flash";
        if (t.openaiModel == "gpt-6-luna") t.openaiModel = "gpt-6.2-sol";
        t.timeoutMs *= 2;
    }
    return t;
}

// The last few lines of the conversation, oldest first, for the AI to match the others' tone. mtx held.
static std::string recentConversation(size_t lines) {
    std::vector<std::string> out;
    for (auto it = g.convo.rbegin(); it != g.convo.rend() && out.size() < lines; ++it)
        out.push_back((it->me ? "Me: " : "Them: ") + (it->me ? it->shown : it->original));
    std::string s;
    for (auto it = out.rbegin(); it != out.rend(); ++it) s += *it + "\n";
    return s;
}

static void fatal(const char* message) {
#ifdef _WIN32
    MessageBoxA(nullptr, message, "CIMA Box", MB_OK | MB_ICONERROR);
#else
    std::fprintf(stderr, "%s\n", message);
#endif
}

// ---------------------------------------------------------------- audio devices

static void captureCb(ma_device*, void*, const void* in, ma_uint32 frames) {
    if (in) g.capRing.push(static_cast<const float*>(in), frames);
}

// Called by miniaudio when something happens to a device. Only a flag here; the UI thread does the rest.
static void deviceNoteCb(const ma_device_notification* n) {
    if (n && (n->type == ma_device_notification_type_stopped || n->type == ma_device_notification_type_rerouted))
        g.deviceEvent = true;
}

static void playCb(ma_device* d, void* out, const void*, ma_uint32 frames) {
    auto* s = static_cast<Sink*>(d->pUserData);
    if (s->flush.exchange(false)) s->ring.clear();
    const size_t cushion = size_t(std::max(0, s->cushion.load()));
    if (cushion > 0 && s->filling) {
        if (s->ring.size() < cushion + frames) return;  // still filling up: silence (pre-filled by miniaudio)
        s->filling = false;
    }
    const size_t got = s->ring.pop(static_cast<int16_t*>(out), frames);  // miniaudio pre-fills the rest with silence
    if (cushion > 0 && got < frames) s->filling = true;  // ran dry: build the cushion up again
}

static void refreshDeviceList(UiModel& ui) {
    ma_device_info *play = nullptr, *cap = nullptr;
    ma_uint32 nPlay = 0, nCap = 0;
    if (ma_context_get_devices(&g.ctx, &play, &nPlay, &cap, &nCap) != MA_SUCCESS) return;
    g.playInfos.assign(play, play + nPlay);
    g.capInfos.assign(cap, cap + nCap);
    ui.inputDevices.clear();
    ui.outputDevices.clear();
    for (const auto& d : g.capInfos) ui.inputDevices.push_back(d.name);
    for (const auto& d : g.playInfos) ui.outputDevices.push_back(d.name);
}

static const ma_device_id* findDevice(const std::vector<ma_device_info>& list, const std::string& name) {
    for (const auto& d : list)
        if (name == d.name) return &d.id;
    return nullptr;  // system default
}

// A device that keeps failing would otherwise write the same line every few seconds.
static void logQuietly(const std::string& text) {  // any thread
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    static std::string last;
    static double at = -1e9;
    if (text == last && nowSeconds() - at < 120.0) return;
    last = text;
    at = nowSeconds();
    logLine(text);
}

static void openCapture(const std::string& name) {
    static bool opened = false;
    if (opened && name != g.capName) {  // another mic: Carry my emotion learns its loudness and brightness again
        std::lock_guard<std::mutex> lk(g.mtx);
        g.baseline.micParts = 0;
    }
    opened = true;
    if (g.capOn) { ma_device_uninit(&g.capDev); g.capOn = false; }
    g.capName = name;
    ma_device_config c = ma_device_config_init(ma_device_type_capture);
    c.capture.pDeviceID = findDevice(g.capInfos, name);
    g.capFellBack = !name.empty() && !c.capture.pDeviceID;
    c.notificationCallback = deviceNoteCb;
    c.capture.format = ma_format_f32;
    c.capture.channels = 1;
    c.sampleRate = kDeviceRate;  // miniaudio resamples from the device's native rate
    c.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
    c.dataCallback = captureCb;
    g.match.forgetMic();  // a different mic sounds different
    if (ma_device_init(&g.ctx, &c, &g.capDev) == MA_SUCCESS && ma_device_start(&g.capDev) == MA_SUCCESS) {
        g.capOn = true;
        logLine("Microphone: " + (name.empty() ? std::string("system default") : name) +
                (g.capFellBack ? " (not found, using the system default until it's back)" : ""));
    } else {
        logQuietly("Microphone: couldn't open " + (name.empty() ? std::string("the system default") : name));
        std::lock_guard<std::mutex> lk(g.mtx);
        g.lastError = "Couldn't open the microphone. Pick another one under My voice > Microphone.";
    }
}

static void openSink(Sink& s, const std::string& name, bool wanted) {
    if (s.on.exchange(false)) ma_device_uninit(&s.dev);
    s.name = name;
    s.wanted = wanted;
    s.fellBack = false;
    if (!wanted) return;
    ma_device_config c = ma_device_config_init(ma_device_type_playback);
    c.playback.pDeviceID = findDevice(g.playInfos, name);
    s.fellBack = !name.empty() && !c.playback.pDeviceID;
    c.notificationCallback = deviceNoteCb;
    c.playback.format = ma_format_s16;
    c.playback.channels = 1;
    c.sampleRate = kDeviceRate;
    c.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;  // cleanest conversion if the device runs at another rate
    c.dataCallback = playCb;
    c.pUserData = &s;
    if (ma_device_init(&g.ctx, &c, &s.dev) == MA_SUCCESS && ma_device_start(&s.dev) == MA_SUCCESS) {
        s.on = true;
        logLine(std::string(s.what) + ": " + (name.empty() ? std::string("system default") : name) +
                (s.fellBack ? " (not found, using the system default until it's back)" : ""));
    } else {
        logQuietly(std::string(s.what) + ": couldn't open " + (name.empty() ? std::string("the system default") : name));
        std::lock_guard<std::mutex> lk(g.mtx);
        g.lastError = "Couldn't open that output device. Pick another one under My voice > Where it goes.";
    }
}

// ---------------------------------------------------------------- voice samples
// A voice's free sample (an MP3 ElevenLabs keeps for every voice), played in your headphones: the
// "Hear it yourself" device, or the default speakers. Never into the cable.
struct VoicePreview {
    std::thread worker;
    std::atomic<bool> stop{false}, loading{false};
    std::string playing;  // voice id (UI thread)

    void play(const std::string& id, const std::string& url, const std::string& headphones) {
        halt();
        if (!g.preview.on || g.preview.name != headphones) openSink(g.preview, headphones, true);
        if (!g.preview.on) return;
        playing = id;
        stop = false;
        loading = true;
        worker = std::thread([this, url] {
            Http http;
            http.cancel = &stop;
            HttpRequest r;
            r.url = url;
            r.timeoutSec = 15;
            r.followRedirects = true;
            long status = 0;
            std::string body, err;
            const bool ok = http.perform(r, status, body, err) && status == 200 && !body.empty();
            if (ok && !stop) {
                VoiceOut dec(kDeviceRate);
                dec.begin("mp3_44100_128");
                const VoiceOut::Emit emit = [](int16_t* s, size_t n) { g.preview.ring.push(s, n); };
                dec.push(body.data(), body.size(), emit);
                dec.finish(emit);
            } else if (!ok) {
                logQuietly("Voice sample: couldn't download it (" + (err.empty() ? std::to_string(status) : err) + ")");
            }
            loading = false;
        });
    }
    void halt() {
        stop = true;
        if (worker.joinable()) worker.join();
        g.preview.flush = true;
        playing.clear();
        loading = false;
    }
    // What the UI shows as playing: until the sample has played out.
    const std::string& shown() {
        if (!playing.empty() && !loading && g.preview.ring.size() == 0) playing.clear();
        return playing;
    }
};
static VoicePreview gPreview;

// ---------------------------------------------------------------- Check my setup
// A guided test, one step at a time: the mic hears you, the voice goes to a cable, both ends of the
// cable run at the same rate, a test beep goes in one end and comes out the other, nothing else sends
// your mic into the cable, push to talk works, and the ElevenLabs key loads your voices.
struct SetupCheck {
    enum Step { kMic, kOut, kRates, kTone, kRoute, kPtt, kKey, kDone };
    int step = kDone;
    double stepAt = 0;
    std::string cableIn, cableOut;    // the two ends of the cable (empty when there's none)
    // Listening on the cable's far end
    ma_device probe;
    bool probeOn = false;
    std::atomic<float> probeDb{-100.f}, tone{0.f};
    // The route check
    double spokeFor = 0, last = 0;
    float quietDb = -100.f, loudDb = -100.f;
    bool toneSent = false;

    static void probeCb(ma_device* d, void*, const void* in, ma_uint32 frames) {
        auto* self = static_cast<SetupCheck*>(d->pUserData);
        const float* x = static_cast<const float*>(in);
        if (!x || frames == 0) return;
        double sum = 0, s1 = 0, s2 = 0;
        const double k = 2.0 * std::cos(2.0 * 3.14159265358979 * 880.0 / kDeviceRate);  // Goertzel at the beep's pitch
        for (ma_uint32 i = 0; i < frames; ++i) {
            sum += double(x[i]) * x[i];
            const double s0 = x[i] + k * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = std::max(0.0, s1 * s1 + s2 * s2 - k * s1 * s2);
        self->probeDb = float(10.0 * std::log10(sum / frames + 1e-12));
        const float amp = float(2.0 * std::sqrt(power) / frames);
        if (amp > self->tone) self->tone = amp;
    }
    void openProbe() {
        closeProbe();
        const ma_device_id* id = findDevice(g.capInfos, cableOut);
        if (!id) return;
        ma_device_config c = ma_device_config_init(ma_device_type_capture);
        c.capture.pDeviceID = id;
        c.capture.format = ma_format_f32;
        c.capture.channels = 1;
        c.sampleRate = kDeviceRate;
        c.dataCallback = probeCb;
        c.pUserData = this;
        if (ma_device_init(&g.ctx, &c, &probe) == MA_SUCCESS) {
            if (ma_device_start(&probe) == MA_SUCCESS) probeOn = true;
            else ma_device_uninit(&probe);
        }
    }
    void closeProbe() {
        if (probeOn) ma_device_uninit(&probe);
        probeOn = false;
    }
    static int nativeRate(ma_device_type type, const std::string& name) {
        const ma_device_id* id = findDevice(type == ma_device_type_playback ? g.playInfos : g.capInfos, name);
        if (!id) return 0;
        ma_device_info info;
        if (ma_context_get_device_info(&g.ctx, type, id, &info) != MA_SUCCESS) return 0;
        for (ma_uint32 i = 0; i < info.nativeDataFormatCount; ++i)  // the rate Windows mixes at (shared mode)
            if (!(info.nativeDataFormats[i].flags & MA_DATA_FORMAT_FLAG_EXCLUSIVE_MODE)) return int(info.nativeDataFormats[i].sampleRate);
        return 0;
    }

    void start(UiModel& ui) {
        ui.check.clear();
        for (const char* t : {"Your microphone", "Where the new voice goes", "Both ends of the cable match",
                              "Sound reaches the cable", "Nothing else sends your mic into the cable", "Push to talk",
                              "Your ElevenLabs key"})
            ui.check.push_back({t, "", "", "", kCheckWaiting, "", ""});
        ui.checkOpen = ui.checkRunning = true;
        step = kMic;
        stepAt = nowSeconds();
        ui.check[kMic].state = kCheckNow;
        cableIn.clear();
        cableOut.clear();
        toneSent = false;
        last = 0;
        spokeFor = 0;
        quietDb = loudDb = -100.f;
    }
    void stop(UiModel& ui) {
        closeProbe();
        g.checkQuiet = false;
        step = kDone;
        ui.checkRunning = false;
        ui.checkPrompt.clear();
        for (CheckItem& c : ui.check)
            if (c.state == kCheckNow || c.state == kCheckWaiting) c.state = kCheckWaiting;
    }
    void finish(UiModel& ui, int state, const char* detail, const std::string& a1 = "", const std::string& a2 = "",
                const char* fix = "", const char* jump = "") {
        CheckItem& c = ui.check[size_t(step)];
        c.state = state;
        c.detail = detail;
        c.arg1 = a1;
        c.arg2 = a2;
        c.fix = fix;
        c.fixJump = jump;
        ++step;
        stepAt = nowSeconds();
        g.checkQuiet = false;
        if (step < kDone) ui.check[size_t(step)].state = kCheckNow;
        else stop(ui);
    }
    void skipCableChecks(UiModel& ui, const char* why) {
        while (step >= kRates && step <= kRoute) finish(ui, kCheckNote, why);
    }

    void update(UiModel& ui) {
        if (step >= kDone) return;
        const double t = nowSeconds() - stepAt;
        switch (step) {
        case kMic:
            ui.checkPrompt = "Say something, like \"testing, one, two\".";
            if (g.levelDb > -42.f) finish(ui, kCheckOk, "It hears you.");
            else if (t > 10.0)
                finish(ui, kCheckProblem, "Nothing came through from %s. Pick the microphone you talk into.",
                       ui.cfg.inDevice.empty() ? std::string("the default microphone") : ui.cfg.inDevice, "", "Pick a microphone", "Microphone");
            break;
        case kOut: {
            ui.checkPrompt.clear();
            const std::string& out = ui.cfg.outDevice;
            if (out.empty()) {
                finish(ui, kCheckProblem, "The new voice goes to your default speakers, so games and Discord won't hear it. Pick CABLE Input.", "", "",
                       "Pick where it goes", "Send the new voice to");
                skipCableChecks(ui, "Skipped: the voice isn't going to a cable.");
                break;
            }
            // The other end of the cable: "CABLE Input (VB-Audio ...)" -> "CABLE Output (VB-Audio ...)".
            std::string pair = out;
            const size_t at = pair.find("Input");
            if (at != std::string::npos) pair.replace(at, 5, "Output");
            const bool found = at != std::string::npos && findDevice(g.capInfos, pair) != nullptr;
            if (found) {
                cableIn = out;
                cableOut = pair;
                finish(ui, kCheckOk, "It goes into %s. Your game and Discord should use %s as their microphone.", out, pair);
            } else {
                finish(ui, kCheckNote, "It goes to %s. If that isn't a virtual cable, only you will hear the new voice.", out);
                skipCableChecks(ui, "Skipped: couldn't find the other end of this cable.");
            }
            break;
        }
        case kRates: {
            const int a = nativeRate(ma_device_type_playback, cableIn), b = nativeRate(ma_device_type_capture, cableOut);
            if (a <= 0 || b <= 0) finish(ui, kCheckNote, "Couldn't read the cable's settings, so this was skipped.");
            else if (a != b)
                finish(ui, kCheckProblem,
                       "One end runs at %s Hz and the other at %s Hz, which makes voices crackle. In Windows Sound settings, set both "
                       "CABLE Input (Playback tab) and CABLE Output (Recording tab) to 48000 Hz: Properties > Advanced.",
                       std::to_string(a), std::to_string(b), "Open Sound settings", "sound");
            else finish(ui, kCheckOk, "Both ends run at %s Hz.", std::to_string(a));
            break;
        }
        case kTone: {
            ui.checkPrompt = "Playing a short, quiet beep into the cable. Stay quiet for a moment.";
            g.checkQuiet = true;
            if (!probeOn && t < 0.1) {
                g.out.flush = true;  // anything still playing goes, so the beep is heard right away
                openProbe();
            }
            if (!probeOn) { finish(ui, kCheckNote, "Couldn't listen to %s, so this was skipped.", cableOut); break; }
            if (!toneSent && t > 0.4) {  // 0.7 s at 880 Hz, faded in and out
                std::vector<int16_t> beep(size_t(kDeviceRate) * 7 / 10);
                for (size_t i = 0; i < beep.size(); ++i) {
                    const double env = std::min(1.0, std::min(double(i), double(beep.size() - i)) / (kDeviceRate * 0.02));
                    beep[i] = int16_t(std::sin(2.0 * 3.14159265358979 * 880.0 * double(i) / kDeviceRate) * env * 0.12 * 32767.0);
                }
                tone = 0.f;
                g.out.ring.push(beep.data(), beep.size());
                toneSent = true;
            }
            if (toneSent && tone > 0.02f) finish(ui, kCheckOk, "A test beep went into %s and came out of %s.", cableIn, cableOut);
            else if (t > 3.0)
                finish(ui, kCheckProblem, "A test beep went into %s but nothing came out of %s. Reinstall VB-Cable, or check it isn't muted or disabled.",
                       cableIn, cableOut, "Open Sound settings", "sound");
            break;
        }
        case kRoute: {
            ui.checkPrompt = "Now talk normally for a few seconds. CIMA Box sends nothing meanwhile.";
            g.checkQuiet = true;
            if (!probeOn) openProbe();
            if (!probeOn) { finish(ui, kCheckNote, "Couldn't listen to %s, so this was skipped.", cableOut); break; }
            const double now = nowSeconds();
            const double dt = last > 0 ? std::min(0.1, now - last) : 0.0;
            last = now;
            if (t < 0.6) break;  // the beep and anything still playing go out first
            if (g.levelDb > -40.f) {
                spokeFor += dt;
                loudDb = std::max(loudDb, probeDb.load());
            } else {
                quietDb = std::max(quietDb, probeDb.load());
            }
            if (spokeFor > 2.0 || t > 9.0) {
                if (spokeFor < 0.8) finish(ui, kCheckNote, "Didn't hear you talk, so this was skipped.");
                else if (loudDb > -50.f && loudDb > quietDb + 10.f)
                    finish(ui, kCheckProblem,
                           "Your voice reached %s while CIMA Box sent nothing, so something else sends your mic into the cable and people hear "
                           "you twice. Usually it's \"Listen to this device\" on your microphone (Sound settings > Recording > your mic > "
                           "Properties > Listen), or another audio app.",
                           cableOut, "", "Open Sound settings", "sound");
                else finish(ui, kCheckOk, "Only CIMA Box sends sound into the cable.");
            }
            break;
        }
        case kPtt: {
            closeProbe();
            if (ui.cfg.activation != kPushToTalk) { finish(ui, kCheckNote, "You use voice activation: nothing to press."); break; }
            const bool vr = ui.cfg.vr.pttButton;
            if (ui.cfg.pttKey <= 0 && !vr) {
                finish(ui, kCheckProblem, "No push-to-talk key is set.", "", "", "Set the key", "Start talking by");
                break;
            }
            ui.checkPrompt = vr ? "Press your push-to-talk key or VR button." : "Press your push-to-talk key.";
            if (keyDown(ui.cfg.pttKey) || (vr && gVr.buttonDown())) finish(ui, kCheckOk, "Push to talk works.");
            else if (t > 10.0)
                finish(ui, kCheckProblem, "Didn't see the key or button being pressed. Check which one is set.", "", "", "Set the key", "Start talking by");
            break;
        }
        case kKey:
            ui.checkPrompt.clear();
            if (ui.cfg.apiKey.empty()) finish(ui, kCheckProblem, "No ElevenLabs key yet.", "", "", "Open Accounts", "Accounts");
            else if (ui.voices.empty()) finish(ui, kCheckProblem, "Your voices didn't load. Check the key.", "", "", "Open Accounts", "Accounts");
            else finish(ui, kCheckOk, "It works: %s voices loaded.", std::to_string(ui.voices.size()));
            break;
        }
    }
};
static SetupCheck gCheck;

// Unplugged headsets, Bluetooth that reconnects, a driver that resets: notices a device that
// stopped by itself, or a named device that is back after the system default stood in for it,
// and opens it again. Checked every few seconds, and right away when a device reports a change.
static void keepDevicesAlive(UiModel& ui) {
    static double lastCheck = 0;
    static bool capDown = false, outDown = false, monDown = false;
    const bool event = g.deviceEvent.exchange(false);
    if (!event && nowSeconds() - lastCheck < 3.0) return;
    lastCheck = nowSeconds();
    auto sinkDown = [](Sink& s) { return s.wanted && (!s.on || !ma_device_is_started(&s.dev)); };
    const bool cap = !g.capOn || !ma_device_is_started(&g.capDev), out = sinkDown(g.out), mon = sinkDown(g.mon);
    if (cap != capDown) logLine(cap ? "Microphone stopped (unplugged?). Will reopen it when it's back." : "Microphone working again.");
    if (out != outDown) logLine(out ? "Output stopped (unplugged?). Will reopen it when it's back." : "Output working again.");
    if (mon != monDown) logLine(mon ? "Monitor output stopped (unplugged?). Will reopen it when it's back." : "Monitor output working again.");
    capDown = cap;
    outDown = out;
    monDown = mon;
    if (!event && !cap && !out && !mon && !g.capFellBack && !g.out.fellBack && !g.mon.fellBack) return;
    refreshDeviceList(ui);
    auto back = [](const std::vector<ma_device_info>& list, const std::string& name) { return findDevice(list, name) != nullptr; };
    // Reopen what is down (if its device exists again, or it's the default), or move back to the real device.
    if ((cap && (g.capName.empty() ? !g.capInfos.empty() : back(g.capInfos, g.capName))) ||
        (g.capFellBack && back(g.capInfos, g.capName)))
        openCapture(g.capName);
    for (Sink* s : {&g.out, &g.mon, &g.ear}) {
        const bool down = sinkDown(*s);
        if ((down && (s->name.empty() ? !g.playInfos.empty() : back(g.playInfos, s->name))) ||
            (s->wanted && s->fellBack && back(g.playInfos, s->name)))
            openSink(*s, s->name, true);
    }
}

// ---------------------------------------------------------------- pipeline threads

// Live transcription (My voice, Translate and Re-speak): one connection for the app, fed by the
// mic loop while you talk. Its words show in "You said" as they come.
static LiveStt& liveStt() {
    static LiveStt live;
    static const bool hooked = [] {
        live.onLog = [](const std::string& line) { logLine(line); };
        live.onPartial = [](const std::string& words) {
            if (words.empty() || !g.recording) return;
            std::lock_guard<std::mutex> lk(g.mtx);
            g.heardText = words + "\xE2\x80\xA6";  // "…": still listening
            g.livePartial = words;
            if (g.earlyQuiet) {  // the last word arrived during the pause: the early translation wants it
                g.earlyWanted = true;
                g.jobCv.notify_one();
            }
        };
        return true;
    }();
    (void)hooked;
    return live;
}

// A little of what you said last, for live transcription to hear names and the topic (it works best
// under 50 characters). mtx held.
static std::string lastWordsSaid() {
    std::string t = g.heardText;
    if (t.size() >= 3 && t.compare(t.size() - 3, 3, "\xE2\x80\xA6") == 0) return std::string();  // a phrase still being heard
    if (t.size() <= 50) return t;
    size_t at = t.find(' ', t.size() - 50);
    return at == std::string::npos ? std::string() : t.substr(at + 1);
}

// Mic -> phrases. Runs continuously, queues each finished phrase for conversion. With the voice
// changer off and passthrough on, it sends the mic itself to the output instead.
static void captureLoop() {
    constexpr int kIn = kDeviceRate / 100;  // 10 ms at the capture rate
    Segmenter seg;
    float in[kIn];
    int16_t pass[kIn];
    std::vector<float> low;     // the mic at 16 kHz, for the phrase cutter and speech-to-text
    std::deque<int16_t> fullQ;  // the mic at kDeviceRate, waiting to be paired with its 16 kHz block
    constexpr size_t kFullPerBlock = size_t(kDeviceRate) / 100;  // 10 ms
    int16_t fullBlock[kFullPerBlock];
    ma_data_converter_config cc = ma_data_converter_config_init(ma_format_f32, ma_format_f32, 1, 1,
                                                                kDeviceRate, Segmenter::kRate);
    ma_data_converter conv;
    const bool convOk = ma_data_converter_init(&cc, nullptr, &conv) == MA_SUCCESS;
    float convOut[kIn];
    bool wasOpen = false;
    bool voiceTurn = false;  // push to talk with your real voice between presses: the new voice's turn
    bool liveThis = false;   // this phrase is going to live transcription
    size_t liveSent = 0;     // how much of it has gone
    bool wasQuiet = false;   // early translation: in a pause that may end the phrase
    bool cutAsked = false;   // a sentence is finished and you're on the next: this phrase ends at the next dip
    int cutLook = 0;
    while (g.running) {
        if (!convOk || g.capRing.size() < size_t(kIn)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        g.capRing.pop(in, kIn);

        GateParams p;
        bool enabled, passthrough, matchMic, realBetween, jobsWaiting, liveWanted, earlyOn, sentencesOn;
        LiveStt::Options liveOptions;
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            enabled = g.cfg.enabled && !g.realVoice && !g.checkQuiet;   // "my real voice" key: your own mic goes out instead
            passthrough = (g.cfg.passthrough || g.realVoice) && !g.checkQuiet;
            matchMic = g.cfg.matchMic;
            p.pushToTalk = g.cfg.activation == kPushToTalk;
            realBetween = p.pushToTalk && g.cfg.pttRealVoice;
            jobsWaiting = !g.jobs.empty();
            p.thresholdDb = g.cfg.thresholdDb;
            p.holdMs = g.cfg.holdMs;
            p.maxSeconds = g.cfg.maxSeconds;
            // Live transcription: for the modes that write down what you say.
            liveWanted = g.cfg.liveStt && !g.cfg.apiKey.empty() &&
                         (g.cfg.engine == kTranslate || g.cfg.engine == kRespeak);
            sentencesOn = g.cfg.speakSentences && g.cfg.liveStt && g.cfg.activation != kPushToTalk &&
                          (g.cfg.engine == kTranslate || g.cfg.engine == kRespeak);
            earlyOn = liveWanted && g.cfg.earlyTranslate && g.cfg.engine == kTranslate && isAiService(g.cfg.translateService);
            if (liveWanted && !wasOpen) {  // what a phrase starting now goes with
                liveOptions.apiKey = g.cfg.apiKey;
                liveOptions.language = g.cfg.spokenLang;
                liveOptions.timestamps = g.cfg.emotion;
                liveOptions.previousText = lastWordsSaid();
            }
        }
        p.pttDown = g.pttDown;

        // Your real voice to the output, kept short so it stays in sync (drops 10 ms when the two
        // devices' clocks have drifted 150 ms apart).
        bool passing = !enabled && passthrough && g.out.on;
        // Push to talk with "my real voice" between presses: your own mic goes out whenever the key is
        // up, except during the new voice's turn. That turn starts the moment you press (so nothing is
        // heard twice) and lasts until the phrase was recorded, translated and fully played, so people
        // never hear you and the new voice on top of each other.
        if (enabled && realBetween && g.out.on) {
            if (g.pttDown || g.recording || g.busy || g.holding || jobsWaiting) voiceTurn = true;
            else if (voiceTurn && g.out.ring.size() < size_t(kDeviceRate) / 50) voiceTurn = false;  // played out
            passing = !voiceTurn;
        } else {
            voiceTurn = false;
        }
        for (int i = 0; i < kIn; ++i) pass[i] = int16_t(std::max(-1.f, std::min(1.f, in[i])) * 32767.f);
        fullQ.insert(fullQ.end(), pass, pass + kIn);
        if (passing) {
            // 50 ms of cushion (see Sink::cushion). The mic and the output have their own clocks, so the
            // amount slowly drifts: one sample left out or said twice now and then keeps it near 50 ms
            // without anything you could hear (dropping 10 ms at once would click).
            const size_t target = size_t(kDeviceRate) / 20, have = g.out.ring.size();
            if (!g.passing) { g.out.cushion = int(target); g.out.filling = true; }
            if (have > target * 3) {
                // far behind (it was busy, or the AI voice was still playing): just wait for it to play out
            } else if (have > target * 2) {
                g.out.ring.push(pass, kIn - 1);
            } else if (have < target / 2 && !g.out.filling) {
                g.out.ring.push(pass, kIn);
                g.out.ring.push(pass + kIn - 1, 1);
            } else {
                g.out.ring.push(pass, kIn);
            }
        } else if (g.passing) {
            g.out.cushion = 0;  // the new voice plays as it comes
            g.out.filling = false;
        }
        g.passing = passing;

        ma_uint64 nIn = kIn, nOut = kIn;
        ma_data_converter_process_pcm_frames(&conv, in, &nIn, convOut, &nOut);
        low.insert(low.end(), convOut, convOut + nOut);
        bool speaking = false;
        while (low.size() >= size_t(Segmenter::kBlock)) {
            Phrase phrase;
            // The matching 10 ms at full quality (the 16 kHz copy trails it by a few samples).
            for (size_t i = 0; i < kFullPerBlock; ++i) {
                fullBlock[i] = fullQ.empty() ? 0 : fullQ.front();
                if (!fullQ.empty()) fullQ.pop_front();
            }
            p.cutSoon = cutAsked;
            const bool done = seg.feed(low.data(), p, phrase.pcm, fullBlock, kFullPerBlock, &phrase.full);
            low.erase(low.begin(), low.begin() + Segmenter::kBlock);
            g.levelDb = seg.levelDb();
            if (!enabled) {
                if (liveThis) liveStt().discard();
                liveThis = false;
                seg.reset();
                g.recording = false;
                wasOpen = false;
                speaking = speaking || g.levelDb > p.thresholdDb;
                continue;
            }
            const bool open = seg.open();
            speaking = speaking || open;
            // Live transcription: the phrase streams to ElevenLabs while you say it, and is committed
            // when it ends, so its words are back almost the moment you stop.
            if (open && !wasOpen) {
                liveThis = liveWanted && liveStt().available();
                liveSent = 0;
                if (liveThis) liveStt().begin(liveOptions);
            }
            if (liveThis && open) {
                const std::vector<int16_t>& sofar = seg.current();
                if (sofar.size() > liveSent) {
                    liveStt().audio(sofar.data() + liveSent, sofar.size() - liveSent);
                    liveSent = sofar.size();
                }
            }
            // Sentence by sentence: the words so far show a finished sentence and at least two words of
            // the next one. The phrase is ended here, so that sentence is translated and said now; the
            // words after it wait for the rest of their sentence, as after any pause in mid-sentence.
            if (liveThis && open && !done && sentencesOn && !cutAsked && ++cutLook >= 10) {
                cutLook = 0;
                std::string sofar;
                {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    sofar = g.livePartial;
                }
                bool unfinished = false;
                const std::vector<std::string> parts = sentencesOf(sofar, unfinished);
                if (parts.size() >= 2) {
                    // Worth it only when what's finished is a real sentence (a short "Yeah." stays
                    // with what follows), and the next one has clearly begun.
                    auto count = [&](size_t from, size_t to, size_t& bytes) {
                        size_t words = 0;
                        bytes = 0;
                        for (size_t i = from; i < to; ++i) {
                            bytes += parts[i].size();
                            words += 1 + size_t(std::count(parts[i].begin(), parts[i].end(), ' '));
                        }
                        return words;
                    };
                    const size_t last = unfinished ? parts.size() - 1 : parts.size();  // where the finished ones end
                    size_t doneBytes = 0, nextBytes = 0;
                    const size_t doneWords = count(0, last, doneBytes), nextWords = last < parts.size() ? count(last, parts.size(), nextBytes) : 0;
                    // No spaces in Chinese or Japanese: by length there.
                    cutAsked = (doneWords >= 6 || (doneWords <= 2 && doneBytes >= 36)) && (nextWords >= 2 || nextBytes >= 12);
                }
            }
            if (done) {
                phrase.midCut = cutAsked && !p.pushToTalk;
                cutAsked = false;
                cutLook = 0;
            } else if (!open) {
                cutAsked = false;
            }
            if (liveThis && done) {
                if (phrase.pcm.size() > liveSent) liveStt().audio(phrase.pcm.data() + liveSent, phrase.pcm.size() - liveSent);
                phrase.live = liveStt().commit();
                liveThis = false;
            } else if (liveThis && !open) {
                liveStt().discard();  // too short to be a phrase (a click, a cough)
                liveThis = false;
            }
            // Early translation: a pause long enough that it may be the end of the phrase (half the
            // pause that ends one). The worker translates the words so far now; they're used only if
            // the phrase really ends here with those words.
            const bool quietNow = earlyOn && liveThis && open && !p.pushToTalk && p.holdMs >= 250 &&
                                  seg.quietMs() >= std::max(150, p.holdMs / 2);
            if (quietNow != wasQuiet) {
                std::lock_guard<std::mutex> lk(g.mtx);
                g.earlyQuiet = quietNow;
                if (quietNow) {
                    g.earlyWanted = true;
                    g.jobCv.notify_one();
                }
                wasQuiet = quietNow;
            }
            if (done || (open && !wasOpen)) {
                std::lock_guard<std::mutex> lk(g.mtx);
                if (open && !wasOpen) {
                    g.phraseOpened = Clock::now();
                    g.livePartial.clear();
                }
                if (done) {
                    phrase.ended = Clock::now();
                    g.jobs.push_back(std::move(phrase));
                }
                else g.warmWanted = true;  // speech just started: connect while the user is still talking
                g.jobCv.notify_one();
            }
            // Only now, after a finished phrase is in the queue: the worker never sees "not talking"
            // and "nothing queued" in between (it would say a cut-off sentence before its rest arrived).
            g.recording = open;
            wasOpen = open;
        }
        if (matchMic && speaking) g.match.learnMic(in, kIn);  // learns how your mic sounds while you talk
    }
    if (convOk) ma_data_converter_uninit(&conv, nullptr);
}

static int msSince(Clock::time_point t) {
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count());
}

// Removes [tags] for places that show text to people (the chatbox).
static std::string withoutTags(const std::string& s) {
    std::string out;
    bool in = false;
    for (char c : s) {
        if (c == '[') in = true;
        else if (c == ']') in = false;
        else if (!in && !(c == ' ' && (out.empty() || out.back() == ' '))) out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// The language a phrase is taken to be in: the one picked, else the one heard. With "Languages I
// speak", anything else is a misheard short phrase: with one language on the list it's that one;
// otherwise the translator works it out (null).
static const Language* spokenLanguage(const Settings& s, const std::string& detected) {
    const Language* from = findLanguage(s.spokenLang.empty() ? detected : s.spokenLang);
    if (s.spokenLang.empty() && !s.fx.myLanguages.empty() && from &&
        std::find(s.fx.myLanguages.begin(), s.fx.myLanguages.end(), std::string(from->code)) == s.fx.myLanguages.end())
        from = s.fx.myLanguages.size() == 1 ? findLanguage(s.fx.myLanguages[0]) : nullptr;
    return from;
}

// "Which means": `line` (in `target`) put back into the speaker's own language by a plain translator,
// never an AI, and shown under "They heard" when it arrives. Nothing waits for it. Called by the
// voice worker for each new line, and by the window when the switch is turned on, for the line
// already showing.
static void checkMeaning(const Settings& s, const std::string& line, const Language& target, const Language& mine) {
    static std::mutex mx;
    static Translator checker;
    static std::atomic<bool> stop{false};
    static std::future<void> job;
    if (line.empty()) return;
    std::lock_guard<std::mutex> lk(mx);
    stop = true;  // the one for the line before, if it's still going
    if (job.valid()) job.wait();
    stop = false;
    checker.http.cancel = &stop;
    const TranslateSetup cs = translateSetup(s, false);
    job = std::async(std::launch::async, [cs, line, target, mine] {
        std::string meaning, e;
        if (!checker.checkBack(cs, line, target, mine, meaning, e)) return;
        std::lock_guard<std::mutex> lk2(g.mtx);
        g.meaningText = meaning;
        g.meaningFor = line;
    });
}

// Phrases -> ElevenLabs -> playback rings. One at a time so the audio stays in order.
static void workerLoop() {
    EarlyTranslation gEarly;  // translations started during a pause (an option); stopped when this thread ends
    VoiceOut voiceOut(kDeviceRate);  // ElevenLabs audio (PCM or MP3, any rate) -> playback rate
    Eleven api;             // both keep their connection open between phrases
    Translator translator;
    Eleven captionApi;      // Changer mode + chatbox: transcribes alongside the voice conversion
    api.http.cancel = &g.cancel;
    translator.http.cancel = &g.cancel;
    captionApi.http.cancel = &g.cancel;

    // Sentence-aware phrases (Translate and Re-speak): the start of a sentence that a pause cut off,
    // waiting a moment for the rest, so the whole sentence is translated and said as one.
    struct Held {
        bool on = false;
        int engine = kTranslate;
        std::string text, lang;
        std::vector<int16_t> pcm;          // its audio (16 kHz), for following your tone
        std::vector<HeardToken> tokens;    // its words, with times in `pcm`
        Clock::time_point due{}, since{};  // when to stop waiting for the rest; when it began
        int pieces = 0;
    } held;
    // What you said just before, in this same stretch of talking. The translator gets it as context,
    // so a story told over several sentences reads as one (who "he" is, the same word for the same thing).
    struct SaidLine { std::string heard, said; Clock::time_point ended; };
    std::deque<SaidLine> stretch;
    // What a line is translated with: the settings, the recent conversation, and the line or two said
    // just before in this stretch (context only, so the sentences fit together).
    auto lineSetup = [&](const Settings& s, Clock::time_point lineStarted, bool typed) {
        TranslateSetup setup = translateSetup(s);
        if (s.mirror) {
            std::lock_guard<std::mutex> lk(g.mtx);
            setup.context = recentConversation(6);
        }
        if (!typed)
            for (const SaidLine& l : stretch)
                if (lineStarted - l.ended <= std::chrono::seconds(4)) setup.before.push_back({l.heard, l.said});
        return setup;
    };
    // Early translation: requests sent for the line being said (all its pieces). At most three, so at
    // most two are thrown away, however many pauses a long line has.
    int earlyThisLine = 0;

    while (g.running) {
        std::vector<int16_t> pcm, full;
        std::string typedText;
        bool testJob = false;
        Settings s;
        bool haveJob = false, professional = false, flushHeld = false;
        Clock::time_point ended = Clock::now();
        std::shared_future<LiveText> liveResult;  // the phrase's words from live transcription, if it was on
        bool heardLive = false;
        bool midCut = false;  // this phrase was ended mid-speech after a finished sentence
        bool earlyNow = false;  // a pause that may end the phrase being spoken: translate its words so far
        std::string partial, lastCode;
        Clock::time_point opened{};
        {
            std::unique_lock<std::mutex> lk(g.mtx);
            for (;;) {
                if (!g.running || !g.jobs.empty() || g.warmWanted || g.earlyWanted) break;
                // A cut-off sentence: once its wait is over and you're not talking, it's said as it is.
                if (held.on && !g.recording && Clock::now() >= held.due) {
                    flushHeld = true;
                    break;
                }
                if (held.on) g.jobCv.wait_for(lk, std::chrono::milliseconds(20));
                else g.jobCv.wait(lk);
            }
            if (!g.running) break;
            s = g.cfg;
            earlyNow = g.earlyWanted && g.jobs.empty();
            partial = g.livePartial;
            lastCode = g.lastSpokenCode;
            opened = g.phraseOpened;
            g.earlyWanted = false;
            g.warmWanted = false;
            if (!flushHeld && !g.jobs.empty()) {
                // With a sentence waiting, only your next phrase from the mic continues it. Typed text
                // or the setup's test goes after that sentence has been said.
                const Phrase& next = g.jobs.front();
                const bool continues = next.text.empty() && !next.test && (s.engine == kTranslate || s.engine == kRespeak);
                if (held.on && !continues) {
                    flushHeld = true;
                } else {
                    pcm = std::move(g.jobs.front().pcm);
                    full = std::move(g.jobs.front().full);
                    typedText = std::move(g.jobs.front().text);
                    testJob = g.jobs.front().test;
                    liveResult = g.jobs.front().live;
                    midCut = g.jobs.front().midCut;
                    if (g.jobs.front().ended != Clock::time_point{}) ended = g.jobs.front().ended;
                    g.jobs.pop_front();
                    haveJob = true;
                    g.busy = true;  // in the same moment the queue empties, so nothing sees "no job and not busy" in between
                }
            }
            if (flushHeld) g.busy = true;
            for (const Voice& v : g.voices) professional = professional || (v.id == s.voiceId && v.category == "professional");
        }
        if (flushHeld) s.engine = held.engine;  // said the way it was heard, even if the mode changed since
        if (!typedText.empty()) s.engine = s.textTranslate ? kTranslate : kRespeak;  // the Text page's own choice
        if (testJob) {  // the setup's test: just the voice, as written
            s.engine = kRespeak;
            s.chatbox = false;
            s.emotion = false;
            s.textSpeak = true;
        }
        g.cancel = false;
        if (!g.running) break;  // quitting: don't start anything that shutdown could no longer cancel
        api.apiKey = captionApi.apiKey = s.apiKey;
        if (!haveJob && !flushHeld && earlyNow) {
            // Early translation, during the pause: the words so far (after a sentence that was waiting
            // for its rest, the whole sentence), unless they sound unfinished, in which case more is
            // coming and a request now would only be thrown away.
            const bool mine = s.earlyTranslate && s.engine == kTranslate && isAiService(s.translateService) &&
                              (!held.on || held.engine == kTranslate);
            if (mine && !partial.empty() && earlyThisLine < 3) {
                const std::string lang = !s.spokenLang.empty() ? s.spokenLang : held.on ? held.lang : lastCode;
                std::string text = applyWordFixes(partial, s.fx.words);
                if (held.on) text = joinHeard(held.text, text, lang);
                const Language* to = findLanguage(s.targetLang);
                const Language* from = spokenLanguage(s, lang);
                if (to && from && from != to && !sentenceUnfinished(text, lang) &&
                    !gEarly.has(text)) {
                    gEarly.start(lineSetup(s, held.on ? held.since : opened, false), text, from, *to);
                    ++earlyThisLine;
                }
            }
            continue;
        }
        if (!haveJob && !flushHeld) {
            // All connections at once (each waits 3 s at most), so a phrase that ends soon after
            // isn't kept waiting behind them.
            auto warmVoice = std::async(std::launch::async, [&] { api.warm(); });
            if (s.engine == kTranslate) translator.warm(translateSetup(s));
            if (s.engine == kVoiceChanger && s.chatbox) captionApi.warm();
            warmVoice.get();
            continue;
        }
        g.busy = true;
        // Clone strength exists only for professional clones on v4; anything else refuses it.
        if (!professional || s.ttsModel.find("eleven_v4") == std::string::npos) s.vs.setCloneStrength = false;

        const auto t0 = Clock::now();
        auto tVoice = t0;
        int sttMs = 0, translateMs = 0, voiceMs = 0, totalMs = 0;
        bool first = true;
        std::vector<int16_t> scaled;
        auto gainInto = [&](const int16_t* in, size_t count, float gain) -> const int16_t* {
            if (std::fabs(gain - 1.f) < 0.005f) return in;
            scaled.resize(count);
            for (size_t i = 0; i < count; ++i)
                scaled[i] = int16_t(std::max(-32768.f, std::min(32767.f, float(in[i]) * gain)));
            return scaled.data();
        };
        auto play = [&](int16_t* raw, size_t count) {
            if (s.matchMic) g.match.process(raw, count, s.matchAmount);  // you hear what others hear
            if (g.mon.on) g.mon.ring.push(gainInto(raw, count, g.monGain), count);
            const int16_t* samples = gainInto(raw, count, g.outGain);
            size_t off = 0;
            while (off < count && g.out.on && !g.cancel && g.running) {
                off += g.out.ring.push(samples + off, count - off);
                if (off < count) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };
        const VoiceOut::Emit emit = [&](int16_t* samples, size_t count) { play(samples, count); };
        AudioSink sink = [&](const char* d, size_t n) {
            if (first) {
                first = false;
                voiceMs = msSince(tVoice);
                totalMs = msSince(t0);
            }
            voiceOut.push(d, n, emit);
        };
        // Runs one voice request at the chosen quality. If the plan refuses that format, steps
        // down (Best -> High -> High at 128 kbps -> Standard) and tries again before any audio played.
        auto speak = [&](const std::function<bool(const std::string& format, std::string& e)>& request, std::string& e) {
            for (int attempt = 0; attempt < 4; ++attempt) {
                const int q = std::min(s.quality, g.qualityCap.load());
                const std::string format = outputFormatFor(q, g.mp3Step);
                voiceOut.begin(format);
                const bool good = request(format, e);
                if (good) {
                    voiceOut.finish(emit);
                    return true;
                }
                std::string low;
                for (char c : e) low += char(std::tolower(static_cast<unsigned char>(c)));
                const bool refused = low.find("subscri") != std::string::npos || low.find("tier") != std::string::npos ||
                                     low.find("upgrade") != std::string::npos || low.find("output_format") != std::string::npos ||
                                     low.find("output format") != std::string::npos;
                if (!first || !refused || q == kQualityStandard) return false;
                std::string note;
                if (format == "mp3_44100_192") { g.mp3Step = 1; note = "Your plan doesn't allow 192 kbps MP3, so High uses 128 kbps."; }
                else if (q == kQualityBest) { g.qualityCap = kQualityHigh; note = "Best needs the Pro plan or above, so High is used instead."; }
                else { g.qualityCap = kQualityStandard; note = "Your plan doesn't allow 44.1 kHz audio, so Standard is used instead."; }
                std::lock_guard<std::mutex> lk(g.mtx);
                g.qualityNote = note;
            }
            return false;
        };

        std::string err, heard, spoken, delivery;
        std::string lineTo, lineMine;  // a translated line's two languages, for "Which means"
        bool ok = true;
        if (s.voiceId.empty() && (typedText.empty() || s.textSpeak)) {
            ok = false;
            err = "Pick a voice first";
        } else if (s.engine == kVoiceChanger && typedText.empty()) {
            // The chatbox needs words, and Changer has none: transcribe at the same time, so the
            // voice isn't kept waiting.
            std::future<std::string> words;
            if (s.chatbox)
                words = std::async(std::launch::async, [&] {
                    std::string text, lang, e;
                    return captionApi.transcribe(pcm, s.spokenLang, text, lang, e) ? text : std::string();
                });
            // At High and Best the voice changer also gets your voice at full quality.
            const bool fullUpload = s.quality != kQualityStandard && !full.empty();
            ok = speak([&](const std::string& format, std::string& e) {
                return api.speechToSpeech(s.voiceId, s.stsModel, s.vs, s.removeNoise, pcm, sink, e, format,
                                          fullUpload ? &full : nullptr, kDeviceRate);
            }, err);
            if (words.valid()) heard = words.get();
        } else {
            std::string detected, say, sayLang;
            const bool typed = !typedText.empty();
            const bool emotion = s.emotion && !typed;  // nothing to listen to in typed text
            const bool tags = modelTakesTags(s.ttsModel);
            VoiceTrack track;
            std::vector<HeardToken> tokens;
            // The start of this sentence, if a pause cut it off: this phrase is the rest of it (or,
            // when flushing, there is no more and it's said as it is).
            const bool joining = held.on && !typed;
            Held was;
            if (joining) was = std::move(held);
            held = Held();
            g.holding = false;
            // All the audio of this sentence: what came before the pause, then this.
            std::vector<int16_t> heardPcm = was.pcm;
            heardPcm.insert(heardPcm.end(), pcm.begin(), pcm.end());
            // Measured on this computer while the upload and transcription run, so it adds no wait.
            std::future<VoiceTrack> measuring;
            if (emotion) measuring = std::async(std::launch::async, [heardPcm] { return trackVoice(heardPcm); });
            std::string piece;
            std::vector<HeardToken> pieceTokens;
            if (typed) heard = typedText;
            else if (!flushHeld) {
                // Live transcription heard it while you talked: the words are normally there already.
                // It gets a short wait at most; otherwise (or after any live problem) the regular way
                // below hears the phrase, so nothing you said is lost.
                if (liveResult.valid()) {
                    static std::string lastLiveProblem;
                    std::string problem;
                    if (liveResult.wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready) {
                        const LiveText r = liveResult.get();
                        // The free translator has to be told the language; live may not have said it.
                        const bool noLanguage = r.language.empty() && s.spokenLang.empty() && s.engine == kTranslate &&
                                                s.translateService == kMyMemory;
                        if (r.ok && !noLanguage) {
                            piece = r.text;
                            detected = r.language;
                            if (emotion) pieceTokens = r.words;
                            heardLive = true;
                        } else {
                            problem = r.ok ? "it didn't say the language" : r.err;
                        }
                    } else {
                        problem = "it took too long";
                    }
                    if (!problem.empty() && problem != lastLiveProblem)
                        logLine("Live transcription didn't have a phrase (" + problem + "), so it was uploaded instead.");
                    lastLiveProblem = problem;
                }
                if (!heardLive) ok = api.transcribe(pcm, s.spokenLang, piece, detected, err, emotion ? &pieceTokens : nullptr);
            }
            if (ok && !typed && !flushHeld) piece = applyWordFixes(piece, s.fx.words);
            // Ended in mid-speech: whatever closes its last words isn't a real sentence end (the speech
            // service likes to add a full stop to anything it's handed). Taken off, so those words
            // wait for the rest of their sentence.
            if (ok && midCut) {
                while (!piece.empty() && (piece.back() == ' ' || piece.back() == '.' || piece.back() == '!' || piece.back() == '?'))
                    piece.pop_back();
                for (const char* mark : {"\xE3\x80\x82", "\xEF\xBC\x81", "\xEF\xBC\x9F"})
                    if (piece.size() >= 3 && piece.compare(piece.size() - 3, 3, mark) == 0) piece.resize(piece.size() - 3);
            }
            std::string langCode;
            if (!typed) {
                if (!ok && joining && err != "cancelled") {
                    // The rest couldn't be heard: say the start of the sentence on its own.
                    logLine("Couldn't hear the rest of a sentence (" + err + "), so its start is said on its own.");
                    ok = true;
                    err.clear();
                    piece.clear();
                    pieceTokens.clear();
                }
                const float offset = float(was.pcm.size()) / float(Segmenter::kRate);  // this piece starts after the first
                for (HeardToken& tk : pieceTokens) {
                    if (tk.start >= 0) tk.start += offset;
                    if (tk.end >= 0) tk.end += offset;
                }
                if (detected.empty()) detected = was.lang;
                const Language* spokenAs = findLanguage(s.spokenLang.empty() ? detected : s.spokenLang);
                langCode = spokenAs ? spokenAs->code : detected;
                if (!langCode.empty()) {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    g.lastSpokenCode = langCode;
                }
                heard = joining ? joinHeard(was.text, piece, langCode) : piece;
                tokens = std::move(was.tokens);
                tokens.insert(tokens.end(), pieceTokens.begin(), pieceTokens.end());
            }
            if (emotion) track = measuring.get();
            sttMs = msSince(t0);
            const bool blank = heard.find_first_not_of(" \t\r\n") == std::string::npos;
            const auto lineStarted = joining ? was.since : ended - std::chrono::milliseconds(pcm.size() * 1000 / Segmenter::kRate);

            // Did a pause cut this sentence in half? Then wait a moment for the rest, and translate the
            // whole sentence at once (half a sentence translated on its own comes out with broken
            // grammar). A finished sentence never waits. In a long stretch of talking, the sentences
            // already finished are said right away, and only the last, unfinished one waits.
            if (ok && !blank && !typed && !testJob && !flushHeld) {
                const bool ptt = s.activation == kPushToTalk;
                const bool talking = g.recording;   // already saying more, or still holding the key
                const int waitMs = ptt ? 0 : s.sentenceWaitMs;  // push to talk: you decide where it ends
                const int pieces = was.pieces + 1;
                // Never held forever: after several pieces, or 40 seconds, it's said as it is.
                const bool enough = pieces >= 6 || Clock::now() - lineStarted > std::chrono::seconds(40);
                if ((waitMs > 0 || talking || midCut) && !enough && (midCut || sentenceUnfinished(heard, langCode))) {
                    size_t cut = lastSentenceEnd(heard);
                    if (cut != std::string::npos && roughWords(heard.substr(0, cut)) < (midCut ? 5 : 8)) cut = std::string::npos;  // keep short bits together
                    Held h;
                    h.on = true;
                    h.engine = s.engine;
                    h.lang = langCode;
                    h.pieces = pieces;
                    h.due = ended + std::chrono::milliseconds(waitMs);
                    if (cut == std::string::npos) {
                        // All of it waits.
                        h.text = heard;
                        h.tokens = tokens;
                        h.pcm = std::move(heardPcm);
                        h.since = lineStarted;
                        held = std::move(h);
                        g.holding = true;
                        {
                            std::lock_guard<std::mutex> lk(g.mtx);
                            g.heardText = heard;  // what it heard so far
                        }
                        // Early translation: while it waits for the rest, the sentence as it stands is
                        // translated. If no more comes, it's ready the moment the wait is over.
                        if (s.earlyTranslate && !talking && s.engine == kTranslate && isAiService(s.translateService) &&
                            earlyThisLine < 3 && !gEarly.has(held.text)) {
                            const Language* to = findLanguage(s.targetLang);
                            const Language* from = spokenLanguage(s, held.lang);
                            if (to && from != to) {
                                gEarly.start(lineSetup(s, held.since, false), held.text, from, *to);
                                ++earlyThisLine;
                            }
                        }
                        g.busy = false;
                        continue;
                    }
                    // The finished sentences go now; the unfinished one waits. Its words (and audio, for
                    // your tone) are found by where the text was cut.
                    std::string rest = heard.substr(cut);
                    heard.resize(cut);
                    while (!heard.empty() && heard.back() == ' ') heard.pop_back();
                    size_t split = tokens.size(), chars = 0;
                    for (size_t i = 0; i < tokens.size(); ++i) {
                        if (tokens[i].kind == kTokSound) continue;
                        if (chars >= cut && tokens[i].kind == kTokWord) { split = i; break; }
                        chars += tokens[i].text.size();
                    }
                    size_t from = heardPcm.size();
                    if (split < tokens.size() && tokens[split].start >= 0)
                        from = size_t(std::max(0.f, tokens[split].start - 0.15f) * Segmenter::kRate);
                    from = std::min(from, heardPcm.size());
                    const float shift = float(from) / float(Segmenter::kRate);
                    for (size_t i = split; i < tokens.size(); ++i) {
                        HeardToken tk = tokens[i];
                        if (tk.start >= 0) tk.start = std::max(0.f, tk.start - shift);
                        if (tk.end >= 0) tk.end = std::max(0.f, tk.end - shift);
                        h.tokens.push_back(tk);
                    }
                    tokens.resize(split);
                    h.pcm.assign(heardPcm.begin() + std::ptrdiff_t(from), heardPcm.end());
                    h.text = rest;
                    h.pieces = 1;
                    h.since = ended;
                    held = std::move(h);
                    g.holding = true;
                }
            }

            Direction dir;
            if (ok && !blank && emotion) {
                VoiceBaseline base;
                {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    base = g.baseline;
                }
                const int learnedBefore = base.phrases;
                dir = directSpeech(track, tokens, base, s.emotionStrength, tags);
                if (!base.ready() && base.phrases == learnedBefore)  // for Report a problem: why learning seems stuck
                    logLine("Usual voice: nothing learned from this phrase (" + std::to_string(tokens.size()) + " words with times, " +
                            std::to_string(int(track.pitch.size())) + " frames). Too short, too quiet or unclear.");
                std::lock_guard<std::mutex> lk(g.mtx);
                g.baseline = base;
                delivery = dir.summary;
                if (!tags) delivery += " (this model takes no tags or capitals, so only punctuation is used)";
            }

            say = heard;
            const Language* to = findLanguage(s.targetLang);
            const Language* from = spokenLanguage(s, detected);
            const bool translating = s.engine == kTranslate && to && from != to;
            if (ok && !blank && s.engine == kTranslate && !to) {
                ok = false;
                err = "Pick a language to translate to";
            } else if (ok && !blank && translating) {
                const auto tTr = Clock::now();
                // The line or two you said just before, if this goes on from them (a story, a long
                // explanation): context only, so the sentences fit together.
                while (!stretch.empty() && lineStarted - stretch.front().ended > std::chrono::seconds(4)) stretch.pop_front();
                TranslateSetup setup = lineSetup(s, lineStarted, typed);
                const bool ai = isAiService(s.translateService);
                // Parts that sound different are translated separately (at the same time), each keeping
                // its own delivery. Not with the AI services: they word a whole sentence more naturally,
                // and one call is quicker than several.
                if (!ai && emotion && dir.parts.size() > 1 && partsDiffer(dir)) {
                    // The parts sound different: translate each on its own (at the same time) so
                    // each keeps its own delivery.
                    std::vector<std::future<std::pair<bool, std::string>>> jobs;
                    for (const SpokenPart& part : dir.parts)
                        jobs.push_back(std::async(std::launch::async, [&setup, &part, from, to] {
                            Translator tr;
                            tr.http.cancel = &g.cancel;
                            std::string outText, e;
                            if (part.plain.empty()) return std::make_pair(true, std::string());
                            const bool good = tr.translate(setup, part.plain, from, *to, outText, e);
                            return std::make_pair(good, good ? outText : e);
                        }));
                    say.clear();
                    for (size_t i = 0; i < jobs.size(); ++i) {
                        const auto r = jobs[i].get();
                        if (!r.first) { ok = false; err = r.second; continue; }
                        if (r.second.empty()) continue;
                        if (!say.empty()) say += " ";
                        say += renderTranslatedPart(dir.parts[i], r.second, tags, say.empty());
                    }
                } else {
                    // Translated ahead of time, during the pause? Then it's here already (or nearly).
                    EarlyTranslation::Result early;
                    const bool tookEarly = ai && !typed && gEarly.take(setup, heard, from, *to, early);
                    if (s.earlyTranslate && ai && !typed) {  // every line says how it went, for the log
                        logLine(tookEarly ? "Early translation: used (" + std::to_string(gEarly.used()) + " of " +
                                                std::to_string(gEarly.started()) + " early requests so far)."
                                          : "Early translation: not used, " + gEarly.whyNot() + ".");
                        g.earlyUsed = tookEarly;
                    }
                    earlyThisLine = 0;  // the next line starts its own count
                    if (tookEarly) {
                        ok = true;
                        say = early.out;
                        translator.via = early.via;
                    } else {
                        ok = translator.translate(setup, heard, from, *to, say, err);
                    }
                    if (ok) {
                        static std::string lastVia;  // say when another service had to step in, and when that ends
                        if (translator.via != lastVia &&
                            (translator.via.find('(') != std::string::npos || lastVia.find('(') != std::string::npos))
                            logLine("My voice translated with " + translator.via);
                        lastVia = translator.via;
                        std::lock_guard<std::mutex> lk(g.mtx);
                        g.translateVia = translator.via;
                    }
                    if (ok && emotion && !dir.parts.empty()) {
                        // One delivery for the whole phrase: the longest part's, and the last part's ending.
                        SpokenPart whole = dir.parts.front();
                        size_t longest = 0;
                        for (const SpokenPart& p : dir.parts)
                            if (p.plain.size() > longest) { longest = p.plain.size(); whole.tag = p.tag; }
                        whole.question = dir.parts.back().question;
                        whole.exclaim = dir.parts.back().exclaim;
                        whole.trailSounds.clear();
                        for (size_t i = 0; i < dir.parts.size(); ++i)
                            for (const std::string& t : dir.parts[i].trailSounds) whole.trailSounds.push_back(t);
                        say = renderTranslatedPart(whole, say, tags, true);
                    }
                }
                translateMs = msSince(tTr);
                spoken = say;
            } else if (ok && !blank && emotion) {
                const std::string line = renderLine(dir, tags);
                if (!line.empty()) say = line;
                spoken = say;
            }
            if (to && s.engine == kTranslate) sayLang = to->code;
            // Clean up my swearing: the AI services already did it; this catches the rest (and DeepL, Free, Re-speak).
            if (ok && !blank && s.fx.voiceSwear == kSwearBleep) say = replaceSwears(say, "beep");
            else if (ok && !blank && s.fx.voiceSwear == kSwearSoften && !(translating && isAiService(s.translateService)))
                say = replaceSwears(say, "");
            if (ok && !blank && s.fx.voiceSwear != kSwearKeep && !spoken.empty()) spoken = say;
            // A moment to cancel before the voice speaks (Features page).
            if (ok && !blank && !testJob && s.fx.previewMs > 0 && (!typed || s.textSpeak)) {
                g.cancelPending = false;
                {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    g.pendingSay = withoutTags(say);
                    g.pendingUntil = nowSeconds() + s.fx.previewMs / 1000.0;
                }
                const auto until = Clock::now() + std::chrono::milliseconds(s.fx.previewMs);
                while (Clock::now() < until && !g.cancelPending && !g.cancel && g.running)
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    g.pendingSay.clear();
                }
                if (g.cancelPending || g.cancel) {
                    g.cancelPending = false;
                    ok = false;
                    err = "cancelled";
                    logLine("A phrase was cancelled before it was spoken.");
                }
            }
            // "Which means": the translated line put back into your language by a plain translator,
            // alongside the voice (which doesn't wait for it), so a wrong or refused line shows. The
            // languages are kept with the line, so the switch can also be turned on afterwards.
            if (ok && !blank && !testJob && translating && !spoken.empty()) {
                const Language* back = from ? from : findLanguage(guessLanguage(heard));
                lineTo = to->code;
                lineMine = back ? back->code : "";
                if (s.showMeaning && back && back != to) checkMeaning(s, withoutTags(spoken), *to, *back);
            }
            if (ok && !blank && (!typed || s.textSpeak)) {
                tVoice = Clock::now();
                ok = speak([&](const std::string& format, std::string& e) {
                    return api.textToSpeech(s.voiceId, s.ttsModel, s.vs, say, sayLang, sink, e, format);
                }, err);
            }
            if (ok && !blank && !typed) {  // for the next line, if it goes on from this one
                stretch.push_back({heard, withoutTags(spoken.empty() ? heard : spoken), ended});
                while (stretch.size() > 2) stretch.pop_front();
            }
        }

        // VRChat chatbox: what you said, what the voice said, or both.
        if (ok && s.chatbox) {
            const std::string said = withoutTags(heard), voiced = withoutTags(spoken.empty() ? heard : spoken);
            std::string text;
            if (s.chatboxShow == kShowSaid) text = said;
            else if (s.chatboxShow == kShowSpoken) text = voiced;
            else text = chatboxPair(voiced.empty() ? said : voiced, said);  // what the voice said, your own words under it
            if (!text.empty()) {
                std::lock_guard<std::mutex> lk(g.mtx);
                g.chatText = text;
            }
        }
        if (testJob) {
            std::lock_guard<std::mutex> lk(g.mtx);
            if (ok) g.lastError.clear();
            else if (err != "cancelled") g.lastError = err;
        } else {
            std::lock_guard<std::mutex> lk(g.mtx);
            if (!heard.empty()) g.heardText = heard;
            g.sentText = spoken;
            g.sentTo = lineTo;
            g.sentMine = lineMine;
            if (ok && !heard.empty()) {
                ConvoLine me;
                me.me = true;
                me.typed = !typedText.empty();
                me.original = heard;
                me.shown = withoutTags(spoken.empty() ? heard : spoken);
                gListen.noteMine(withoutTags(heard));  // captions of the replies know what was asked
                addConvo(std::move(me));
            }
            if (!delivery.empty()) g.delivery = delivery;
            if (ok) g.lastError.clear();
            else if (err != "cancelled") g.lastError = err;
        }
        if (totalMs > 0) {
            g.sttMs = sttMs;
            g.sttLive = heardLive;
            if (!s.earlyTranslate) g.earlyUsed = false;
            g.translateMs = translateMs;
            g.voiceMs = voiceMs;
            g.totalMs = totalMs;
        }
        g.busy = false;
    }
}

// "Hear them in my ear" and learning-mode replays: lines read aloud on the ear device, one at a time.
static void earLoop() {
    VoiceOut voiceOut(kDeviceRate);
    Eleven api;
    api.http.cancel = &g.cancel;
    while (g.running) {
        std::pair<std::string, std::string> job;
        Settings s;
        {
            std::unique_lock<std::mutex> lk(g.mtx);
            g.earCv.wait(lk, [] { return !g.earJobs.empty() || !g.running; });
            if (!g.running) break;
            job = std::move(g.earJobs.front());
            g.earJobs.pop_front();
            s = g.cfg;
        }
        const std::string voice = s.fx.earVoice.empty() ? s.voiceId : s.fx.earVoice;
        if (!g.ear.on || s.apiKey.empty() || voice.empty() || job.first.empty()) continue;
        api.apiKey = s.apiKey;
        // The quick model when it speaks the language; otherwise the one picked on My voice.
        const Language* lang = findLanguage(job.second);
        const std::string model = !lang || lang->everyModel ? std::string("eleven_flash_v2_5") : s.ttsModel;
        const VoiceOut::Emit emit = [](int16_t* samples, size_t n) {
            const float gain = g.earGain;
            for (size_t i = 0; i < n; ++i) samples[i] = int16_t(std::max(-32768.f, std::min(32767.f, float(samples[i]) * gain)));
            size_t off = 0;
            while (off < n && g.ear.on && g.running) {
                off += g.ear.ring.push(samples + off, n - off);
                if (off < n) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };
        const std::string format = outputFormatFor(kQualityStandard, 0);
        voiceOut.begin(format);
        std::string err;
        VoiceSettings vs = s.vs;
        vs.setCloneStrength = false;
        if (api.textToSpeech(voice, model, vs, job.first, lang ? lang->code : std::string(),
                             [&](const char* d, size_t n) { voiceOut.push(d, n, emit); }, err, format))
            voiceOut.finish(emit);
        else if (err != "cancelled")
            logQuietly("Hear them in my ear: " + err);
    }
}

// ---- Screen translation (Features page): pictures of one window, read and translated on a thread
// of its own (Windows' text reader sets up the Windows runtime on the thread that uses it).
struct ScreenWork {
    std::mutex mtx;
    std::condition_variable cv;
    bool snapshot = false, direct = false;  // take a picture; direct = translate it right away (no preview)
    bool translate = false;                 // read and translate the picture with `crop`
    float crop[4] = {0, 0, 1, 1};
    Picture picture;                        // the last picture
    bool newPicture = false;                // the UI should show it
    bool busy = false;
    std::string status, app;
    std::vector<ScreenLine> lines;
    int tokens = 0;
    std::vector<std::string> seen;          // Watch: lines already translated (as read)
    std::vector<std::string> readerLangs;   // what Windows' text reader can read here
    bool readerKnown = false;
};
static ScreenWork gScreen;

// Reads `pic` and translates what's new (not in `seen`, when given). Lines go into `out`.
// `firstLook`: Watch just started, so only the last few lines are new; the rest count as seen.
static bool readAndTranslate(Translator& tr, const Settings& s, const Picture& pic, std::vector<std::string>* seen,
                             std::vector<ScreenLine>& out, std::string& err, bool firstLook = false) {
    out.clear();
    const Language* to = findLanguage(s.screen.toLang.empty() ? s.listen.toLang : s.screen.toLang);
    if (!to) { err = "Pick a language to translate into."; return false; }
    TranslateSetup t = translateSetup(s, false);
    t.faithful = true;
    auto isNew = [&](const std::string& line) {
        if (!seen) return true;
        for (const std::string& x : *seen)
            if (sameLine(x, line)) return false;
        return true;
    };
    auto remember = [&](const std::string& line) {
        if (!seen) return;
        seen->push_back(line);
        if (seen->size() > 400) seen->erase(seen->begin(), seen->begin() + 100);
    };
    if (s.screen.reader == kReadAi) {
        const Picture shrunk = scaledDown(pic, 1280);
        std::vector<std::pair<std::string, std::string>> pairs;
        if (!tr.readPicture(t, base64(encodePng(shrunk)), *to, pairs, err)) return false;
        if (firstLook && pairs.size() > 3)
            for (size_t i = 0; i + 3 < pairs.size(); ++i) remember(pairs[i].first);
        for (const auto& p : pairs) {
            if (!isNew(p.first)) continue;
            remember(p.first);
            out.push_back({p.first, s.fx.maskCaptions ? maskSwears(p.second) : p.second});
        }
        return true;
    }
    std::vector<std::string> read;
    if (!readText(pic, s.screen.readLang, read, err)) return false;
    if (firstLook && read.size() > 3)
        for (size_t i = 0; i + 3 < read.size(); ++i) remember(read[i]);
    const Language* from = findLanguage(s.screen.readLang);
    for (const std::string& line : read) {
        if (line.find_first_not_of(" .,-_|") == std::string::npos || !isNew(line)) continue;
        remember(line);
        std::string translated = line, e;
        if (from != to && !tr.translate(t, line, from, *to, translated, e)) translated = line;
        out.push_back({line, s.fx.maskCaptions ? maskSwears(translated) : translated});
    }
    return true;
}

static void screenLoop() {
    Translator tr;
    tr.http.cancel = nullptr;
    auto nextWatch = Clock::now();
    auto nextLangs = Clock::now();
    bool watching = false;
    float watchArea[4] = {0, 0, 1, 1};
    std::string watchApp;
    while (g.running) {
        bool snap = false, direct = false, translate = false;
        float crop[4];
        {
            std::unique_lock<std::mutex> lk(gScreen.mtx);
            gScreen.cv.wait_for(lk, std::chrono::milliseconds(200),
                                [] { return gScreen.snapshot || gScreen.translate || !g.running; });
            snap = std::exchange(gScreen.snapshot, false);
            direct = std::exchange(gScreen.direct, false);
            translate = std::exchange(gScreen.translate, false);
            std::copy(gScreen.crop, gScreen.crop + 4, crop);
        }
        if (!g.running) break;
        Settings s;
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            s = g.cfg;
        }
        if (!s.screen.on || !s.screen.agreed) { watching = false; continue; }
        if (Clock::now() >= nextLangs) {  // languages can be added in Windows while the app runs
            nextLangs = Clock::now() + std::chrono::seconds(20);
            std::vector<std::string> langs = readerLanguages();
            std::lock_guard<std::mutex> lk(gScreen.mtx);
            gScreen.readerLangs = std::move(langs);
            gScreen.readerKnown = true;
        }
        auto setStatus = [](const std::string& st, bool busy) {
            std::lock_guard<std::mutex> lk(gScreen.mtx);
            gScreen.status = st;
            gScreen.busy = busy;
        };
        auto addLines = [&s](const std::vector<ScreenLine>& lines) {
            std::lock_guard<std::mutex> lk(g.mtx);
            for (const ScreenLine& l : lines) {
                ConvoLine c;
                c.screen = true;
                c.original = l.text;
                c.shown = l.translated.empty() ? l.text : l.translated;
                c.code = s.screen.readLang;
                addConvo(std::move(c));
                gListen.addScreenCaption(l.translated.empty() ? l.text : l.translated, l.text);
            }
        };
        if (snap) {
            setStatus("Taking a picture", true);
            Picture pic;
            std::string err;
            if (!captureWindow(s.screen.app, pic, err)) {
                setStatus(err, false);
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(gScreen.mtx);
                gScreen.picture = pic;
                gScreen.newPicture = !direct;
                gScreen.app = s.screen.app;
                gScreen.lines.clear();
                gScreen.tokens = s.screen.reader == kReadAi ? pictureTokens(s.translateService, std::min(pic.width, 1280), std::min(pic.height, 1280)) : 0;
                gScreen.status.clear();
                gScreen.busy = direct;
            }
            if (direct) {
                translate = true;
                std::copy(s.screen.area, s.screen.area + 4, crop);
            }
        }
        if (translate) {
            Picture pic;
            {
                std::lock_guard<std::mutex> lk(gScreen.mtx);
                pic = gScreen.picture;
                gScreen.busy = true;
            }
            if (pic.rgba.empty()) { setStatus("Take a snapshot first.", false); continue; }
            std::vector<ScreenLine> lines;
            std::string err;
            const bool ok = readAndTranslate(tr, s, cropPicture(pic, crop), nullptr, lines, err);
            if (ok) addLines(lines);
            logLine(std::string("Screen translation: ") + (ok ? std::to_string(lines.size()) + " lines" : err));
            std::lock_guard<std::mutex> lk(gScreen.mtx);
            gScreen.lines = lines;
            gScreen.busy = false;
            gScreen.status = ok ? (lines.empty() ? std::string("No text found in the picture.") : std::string()) : err;
        }
        // Watch a chat box: every few seconds, translate the lines that weren't there before.
        if (s.screen.watch) {
            bool firstLook = false;
            if (watching && (watchApp != s.screen.app || !std::equal(watchArea, watchArea + 4, s.screen.area))) watching = false;  // another window or area: start over
            if (!watching) {
                watchApp = s.screen.app;
                std::copy(s.screen.area, s.screen.area + 4, watchArea);
            }
            if (!watching) {  // starting: what's already there counts as seen, apart from the last few lines
                watching = true;
                firstLook = true;
                nextWatch = Clock::now();
                std::lock_guard<std::mutex> lk(gScreen.mtx);
                gScreen.seen.clear();
            }
            if (Clock::now() >= nextWatch) {
                nextWatch = Clock::now() + std::chrono::milliseconds(int(s.screen.watchSeconds * 1000));
                Picture pic;
                std::string err;
                if (!captureWindow(s.screen.app, pic, err)) { setStatus(err, false); continue; }
                std::vector<std::string> seen;
                {
                    std::lock_guard<std::mutex> lk(gScreen.mtx);
                    seen = gScreen.seen;
                }
                std::vector<ScreenLine> lines;
                if (readAndTranslate(tr, s, cropPicture(pic, s.screen.area), &seen, lines, err, firstLook)) {
                    addLines(lines);
                    std::lock_guard<std::mutex> lk(gScreen.mtx);
                    gScreen.seen = seen;
                    if (!lines.empty()) gScreen.status.clear();
                } else {
                    setStatus(err, false);
                }
            }
        } else {
            watching = false;
        }
    }
}

static void stopSpeaking() {  // drop everything queued or playing
    {
        std::lock_guard<std::mutex> lk(g.mtx);
        g.jobs.clear();
    }
    g.cancel = true;
    g.out.flush = true;
    g.mon.flush = true;
}

static void refreshCatalog(const std::string& key) {
    if (g.loading.exchange(true)) return;
    std::thread([key] {
        Eleven api;
        api.apiKey = key;
        std::vector<Voice> voices;
        std::vector<Model> models;
        std::string e1, e2;
        bool ok1 = false, ok2 = false;
        try {
            ok1 = api.listVoices(voices, e1);
            ok2 = ok1 && api.listModels(models, e2);
        } catch (const std::exception& ex) {  // never leave the button stuck on "Loading voices"
            e1 = std::string("Couldn't read the voice list: ") + ex.what();
        }
        auto explain = [](const std::string& e) {
            if (e.find("Timeout") != std::string::npos || e.find("timed out") != std::string::npos)
                return std::string("ElevenLabs didn't answer within 20 seconds. Check your internet connection, VPN or firewall.");
            return e;
        };
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            if (ok1) g.voices = std::move(voices);
            if (ok2) g.models = std::move(models);
            g.catalogMsg = !ok1 ? explain(e1) : !ok2 ? explain(e2) : "Loaded " + std::to_string(g.voices.size()) + " voices";
            ++g.catalogVersion;
        }
        g.loading = false;
    }).detach();
}

// ---------------------------------------------------------------- VR captions

// The last lines of the conversation, as the VR panel shows them. mtx held.
static std::vector<PanelLine> vrLines(const VrSettings& v, bool showScreen) {
    std::vector<PanelLine> out;
    const size_t from = g.convo.size() > 12 ? g.convo.size() - 12 : 0;
    for (size_t i = from; i < g.convo.size(); ++i) {
        const ConvoLine& c = g.convo[i];
        if (c.me && !v.showMe) continue;
        if (c.screen && !showScreen) continue;
        PanelLine l;
        l.text = c.shown;
        l.sub = c.original != c.shown ? c.original : std::string();
        l.me = c.me;
        l.speaker = c.speaker;
        out.push_back(std::move(l));
    }
    return out;
}

// What the preview shows before anyone has said anything.
static std::vector<PanelLine> sampleLines() {
    return {{"Is this your first time in this world?", "\xE3\x81\x93\xE3\x81\xAE\xE3\x83\xAF\xE3\x83\xBC\xE3\x83\xAB\xE3\x83\x89\xE3\x81\xAF\xE5\x88\x9D\xE3\x82\x81\xE3\x81\xA6\xEF\xBC\x9F", false, -1},
            {"Yeah, I just got here. It's really pretty.", "", true, -1},
            {"Come on, let's go to the mirror.", "", false, 0}};
}

// ---------------------------------------------------------------- app icon

static unsigned gLogoTexture = 0;

// The picture built into the program: window icon (on Windows the .exe's own icon resource
// already provides it) and the logo next to the name.
static void setupLogo(GLFWwindow* win, UiModel& ui, float scale) {
#ifndef _WIN32
    std::vector<GLFWimage> images;
    for (int i = 0; i < kLogoCount; ++i)
        images.push_back({kLogos[i].size, kLogos[i].size, const_cast<unsigned char*>(kLogos[i].rgba)});
    glfwSetWindowIcon(win, int(images.size()), images.data());
#else
    (void)win;
#endif
    // The smallest stored size that is at least as big as it is drawn, so it is never blurry.
    const float drawn = 16.f * scale * 2.3f * 0.92f;
    const LogoImage* pick = &kLogos[0];
    for (int i = 0; i < kLogoCount; ++i)
        if (kLogos[i].size >= drawn && kLogos[i].size < pick->size) pick = &kLogos[i];
    glGenTextures(1, &gLogoTexture);
    glBindTexture(GL_TEXTURE_2D, gLogoTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pick->size, pick->size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pick->rgba);
    ui.logoTexture = gLogoTexture;
    ui.logoSize = float(pick->size);
}

// ---------------------------------------------------------------- Settings > Look: background picture

static unsigned gPictureTexture = 0;

// Loads the background picture from the settings folder into a texture (or clears it).
static void loadPicture(UiModel& ui) {
    if (gPictureTexture) glDeleteTextures(1, &gPictureTexture);
    gPictureTexture = 0;
    ui.pictureTexture = 0;
    ui.pictureW = ui.pictureH = 0;
    if (ui.cfg.look.picture.empty()) return;
    std::ifstream f(settingsDir() / fs::u8path(ui.cfg.look.picture), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!decodePicture(bytes, rgba, w, h)) {
        ui.pictureMsg = "That picture couldn't be read. Pick a JPEG or PNG file.";
        ui.cfg.look.picture.clear();
        return;
    }
    glGenTextures(1, &gPictureTexture);
    glBindTexture(GL_TEXTURE_2D, gPictureTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    ui.pictureTexture = gPictureTexture;
    ui.pictureW = float(w);
    ui.pictureH = float(h);
}

// "Choose a picture": a Windows file picker, then a copy into the settings folder so the look
// survives the original being moved or deleted.
static void pickPicture(GLFWwindow* win, UiModel& ui) {
#ifdef _WIN32
    wchar_t path[1024] = L"";
    OPENFILENAMEW of = {};
    of.lStructSize = sizeof(of);
    of.hwndOwner = glfwGetWin32Window(win);
    of.lpstrFilter = L"Pictures (JPEG, PNG)\0*.jpg;*.jpeg;*.png\0";
    of.lpstrFile = path;
    of.nMaxFile = 1024;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return;  // cancelled
    const fs::path from(path);
    std::string ext = from.extension().u8string();
    for (char& ch : ext) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") {
        ui.pictureMsg = "That picture couldn't be read. Pick a JPEG or PNG file.";
        return;
    }
    const std::string name = "background" + ext;
    std::error_code ec;
    for (const char* old : {"background.jpg", "background.jpeg", "background.png"})
        if (name != old) fs::remove(settingsDir() / old, ec);
    fs::copy_file(from, settingsDir() / name, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        ui.pictureMsg = "Couldn't copy that picture into CIMA Box's settings folder.";
        return;
    }
    ui.pictureMsg.clear();
    ui.cfg.look.picture = name;
    loadPicture(ui);
    logLine("Look: background picture set (" + std::to_string(int(ui.pictureW)) + "x" + std::to_string(int(ui.pictureH)) + ")");
#else
    (void)win;
    (void)ui;
#endif
}

// The pictures in the "how do I get this key?" guides, as textures the window can draw.
static void setupGuidePictures(UiModel& ui) {
    for (int i = 0; i < kPicCount && i < guidePicCount(); ++i) {
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!decodeGuidePic(i, rgba, w, h)) continue;
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        ui.guidePics[i] = {uint64_t(tex), w, h};
    }
}

// ---------------------------------------------------------------- log, usage line

static std::string windowsVersion() {
#ifdef _WIN32
    struct OsInfo { ULONG size, major, minor, build, platform; WCHAR sp[128]; } v{};
    v.size = sizeof(v);
    using Fn = LONG(WINAPI*)(OsInfo*);
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))); fn && fn(&v) == 0)
            return "Windows " + std::string(v.build >= 22000 ? "11" : std::to_string(v.major)) + " (build " + std::to_string(v.build) + ")";
    return "Windows";
#else
    return "not Windows";
#endif
}

static void openPath(const fs::path& what) {  // a web page, or a file in its usual program
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", what.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)what;
#endif
}

static void startLog() {
    std::error_code ec;
    fs::create_directories(settingsDir(), ec);
    logOpen(settingsDir() / "cimabox.log");
}

// 12,400 -> "12.4k", 1,250,000 -> "1.25M"
static std::string shortCount(long long n) {
    char b[32];
    if (n < 0) n = 0;
    if (n >= 1000000) std::snprintf(b, sizeof(b), n >= 10000000 ? "%.0fM" : "%.2gM", double(n) / 1e6);
    else if (n >= 1000) std::snprintf(b, sizeof(b), n >= 100000 ? "%.0fk" : "%.3gk", double(n) / 1e3);
    else std::snprintf(b, sizeof(b), "%lld", n);
    return b;
}

static std::string withCommas(long long n) {
    std::string d = std::to_string(n < 0 ? 0 : n), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}

static UsageItem quotaItem(const char* name, const Quota& q, const char* unitsLeft, const char* unitsWord) {
    UsageItem it;
    it.name = name;
    if (!q.known) {
        it.level = kUsageUnknown;
        it.text = q.problem.empty() ? T("checking") : T("not available");
        it.detail = q.problem.empty() ? T("Checking how much is left.") : T(q.problem);
        return it;
    }
    const long long left = q.limit - q.used;
    char b[200];
    std::snprintf(b, sizeof(b), TF(unitsLeft), shortCount(left).c_str());
    it.text = b;
    it.level = left <= 0 ? kUsageOut : (q.limit > 0 && left * 10 < q.limit) ? kUsageLow : kUsageOk;
    std::snprintf(b, sizeof(b), TF("Used %s of %s %s."), withCommas(q.used).c_str(), withCommas(q.limit).c_str(), T(unitsWord));
    it.detail = b;
    if (q.resetsAt > 0) {
        const std::time_t t = std::time_t(q.resetsAt);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char day[32];
        std::strftime(day, sizeof(day), "%Y-%m-%d", &tm);
        std::snprintf(b, sizeof(b), TF("Starts over on %s."), day);
        it.detail += std::string(" ") + b;
    }
    if (!q.problem.empty()) it.detail += std::string(" ") + T("(Couldn't check just now; this is the last known amount.)");
    return it;
}

static std::string usingLine(const Settings& c) {
    const char* engines[] = {"Changer", "Re-speak", "Translate"};
    const char* services[] = {"Free", "DeepL", "Claude", "Gemini", "OpenAI", "Local", "Microsoft"};
    const char* sources[] = {"one program", "speakers", "an input"};
    std::string line = std::string("Using: My voice ") + (c.enabled ? "on" : "off") + ", " +
                       engines[std::max(0, std::min(2, c.engine))] + " (" + (c.engine == kVoiceChanger ? c.stsModel : c.ttsModel) + ")";
    if (c.engine == kTranslate) line += ", " + (c.spokenLang.empty() ? std::string("auto") : c.spokenLang) + " to " + c.targetLang;
    line += std::string(", translation ") + services[std::max(0, std::min(6, c.translateService))];
    if (c.translateService == kLocal) line += " (" + (c.localModel.empty() ? std::string("no model") : c.localModel) + (c.localGpu ? ")" : ", processor only)");
    line += ", quality " + std::to_string(c.quality) + (c.matchMic ? ", match my mic" : "") + (c.passthrough ? ", passthrough" : "") +
            (c.activation == kPushToTalk && c.pttRealVoice ? ", real voice between presses" : "");
    line += std::string(". Listen ") + (c.listen.on ? "on" : "off") + " (" + sources[std::max(0, std::min(2, c.listen.source))] +
            (c.listen.source == kSrcApp ? " " + c.listen.app : "") + ", to " + c.listen.toLang + "). VR " + (c.vr.on ? "on" : "off") +
            ". Chatbox " + (c.chatbox || c.listen.chatbox ? "on" : "off") + ". App language " + c.uiLanguage + ".";
    // Which keys are filled in (never the keys themselves), and the AI time limit: the first things to
    // check when translations fall back or fail.
    std::string keys;
    for (const auto& k : {std::make_pair("ElevenLabs", &c.apiKey), std::make_pair("DeepL", &c.deeplKey),
                          std::make_pair("Claude", &c.claudeKey), std::make_pair("Gemini", &c.geminiKey),
                          std::make_pair("OpenAI", &c.openaiKey), std::make_pair("Microsoft", &c.msKey)})
        if (!k.second->empty()) keys += (keys.empty() ? "" : ", ") + std::string(k.first);
    line += " Keys: " + (keys.empty() ? std::string("none") : keys) + ". AI time limit " +
            std::to_string(c.aiTimeoutMs / 1000) + "." + std::to_string(c.aiTimeoutMs % 1000 / 100) + " s.";
    std::string fx;
    auto add = [&fx](bool on, const char* name) {
        if (on) fx += (fx.empty() ? "" : ", ") + std::string(name);
    };
    add(c.fx.previewMs > 0, "cancel window");
    add(c.fx.answerTheirs, "answer in their language");
    add(!c.fx.myLanguages.empty() || !c.fx.aroundLanguages.empty(), "language lists");
    add(c.fx.voiceSwear != kSwearKeep, "swear filter");
    add(c.fx.best, "best translations");
    add(c.fx.hearThem, "hear them");
    add(c.fx.liveCaptions, "live captions");
    add(c.screen.on, c.screen.reader == kReadAi ? "screen translation (AI)" : "screen translation");
    add(c.screen.watch, "watch chat");
    add(c.fx.learning, "learning");
    add(c.fx.keepHistory, "history");
    add(c.fx.clickThrough, "click-through window");
    add(!c.fx.words.empty(), "word list");
    if (!fx.empty()) line += " Features: " + fx + ".";
    return line;
}

static UsageItem aiItem(int service, const std::string& model) {
    const AiSpend sp = aiSpend(service, model);
    UsageItem it;
    it.name = service == kClaude ? "Claude" : service == kGemini ? "Gemini" : "OpenAI";
    it.level = kUsageOk;
    char b[200];
    if (sp.dollars < 0) std::snprintf(b, sizeof(b), TF("%s tokens this session"), shortCount(sp.tokensIn + sp.tokensOut).c_str());
    else if (sp.tokensIn + sp.tokensOut == 0) std::snprintf(b, sizeof(b), "%s", T("$0 this session"));
    else if (sp.dollars < 0.01) std::snprintf(b, sizeof(b), "%s", T("under $0.01 this session"));
    else std::snprintf(b, sizeof(b), TF("$%.2f this session"), sp.dollars);
    it.text = b;
    std::snprintf(b, sizeof(b), TF("Counted by CIMA Box since it started: %s tokens in, %s out. %s can't report what's left on a key."),
                  withCommas(sp.tokensIn).c_str(), withCommas(sp.tokensOut).c_str(), it.name.c_str());
    it.detail = b;
    return it;
}

// ---------------------------------------------------------------- main

int main() {
#ifdef _WIN32
    timeBeginPeriod(1);  // short waits really are short (otherwise about 15 ms), so audio moves evenly
#endif
    UiModel ui;  // the UI thread's copy of everything; ui.cfg is published to g.cfg every frame
    loadSettings(ui.cfg);
    ui.cfg.page = kPageHome;  // it opens on Home: everything at a glance
    setUiLanguage(ui.cfg.uiLanguage);
    startLog();
    logLine(std::string("CIMA Box ") + CIMA_FULL_VERSION + " started on " + windowsVersion() + ". Settings: " +
            (gPlace == kPlaceDocuments ? "Documents" : gPlace == kPlaceProgram ? "with the program" : "settings folder") + ".");
    ui.logPath = (settingsDir() / "cimabox.log").u8string();
    if (!gRestoredFrom.empty())
        logLine("Settings from " + gRestoredFrom + " that the earlier version didn't know were put back.");
    if (!logLastRunClosed()) logLine("The last time, CIMA Box didn't close normally (it crashed, or was ended by force or by a shutdown).");
    logLine(usingLine(ui.cfg));  // what this person uses, for reading the log later (no keys, nothing they said)
    ui.settingsPath = settingsFile().u8string();
    ui.settingsPlace = gPlace;
    // After an update: show what changed since the version this person last used.
    if (gSettingsExisted && compareVersions(CIMA_FULL_VERSION, ui.cfg.lastSeenVersion.empty() ? "0" : ui.cfg.lastSeenVersion) > 0) {
        ui.showChanges = 1;
        ui.changesSince = ui.cfg.lastSeenVersion.empty() ? "2.5.99" : ui.cfg.lastSeenVersion;  // from before 2.6: just this one
    }
    ui.cfg.lastSeenVersion = CIMA_FULL_VERSION;
    if (!ui.cfg.setupDone) {  // a new person: language first, then the walkthrough (or where they left it)
        ui.setupStep = std::max(0, ui.cfg.setupAt);
        ui.setupUse = ui.cfg.setupUse;
        ui.setupRoute = ui.cfg.setupRoute;
        ui.setupAi = ui.cfg.setupAi == kGuideClaude || ui.cfg.setupAi == kGuideOpenAI || ui.cfg.setupAi == kSetupLocal ? ui.cfg.setupAi : kGuideGemini;
    }
    double lastAutosave = 0;
    Updater updater(CIMA_FULL_VERSION);
    // Models on this PC live next to the other app data (never in the settings folder: they're big).
    LocalAi localAi(appDataDir() / "local-models", [](const std::string& line) { logLine(line); });
    VoiceId voiceId(appDataDir() / "voice-id", [](const std::string& line) { logLine(line); });
    setVoiceIdEngine(&voiceId);
    ui.pc = readPcSpecs();
    if (ui.pc.known)
        logLine("This PC: " + (ui.pc.gpu.empty() ? std::string("no separate graphics card") : ui.pc.gpu + " (" + std::to_string(ui.pc.gpuBytes >> 20) + " MB)") +
                ", " + std::to_string(ui.pc.ramBytes >> 20) + " MB of memory, " + std::to_string(ui.pc.threads) + " threads.");
    Updater::removeOldVersion();  // what the last update replaced
    ui.updatesConfigured = Updater::configured();
    bool updateChecked = false;
    g.cfg = ui.cfg;
    g.baseline = ui.cfg.baseline;

    if (ma_context_init(nullptr, 0, nullptr, &g.ctx) != MA_SUCCESS) {
        fatal("Couldn't start the audio system.");
        return 1;
    }
    refreshDeviceList(ui);
    g.mon.what = "Monitor output";
    g.ear.what = "Hear them output";
    g.preview.what = "Voice samples";
    openCapture(ui.cfg.inDevice);
    openSink(g.out, ui.cfg.outDevice, true);
    openSink(g.mon, ui.cfg.monDevice, ui.cfg.monitor);

    std::thread capThread(captureLoop), workThread(workerLoop), earThread(earLoop), screenThread(screenLoop);
    std::thread keyThread([] {  // global hotkeys (Windows): checked every 8 ms whatever the window is doing
        bool talkWas = false, stopWas = false, cancelWas = false, overlayWas = false, screenWas = false;
        PttInput ptt;
        while (g.running) {
            if (kGlobalHotkey) {
                const bool binding = g.binding;
                g.pttDown = ptt.update(!binding && (keyDown(g.pttKey) || (g.vrButton && gVr.buttonDown())), g.pttTap, g.pttActive);
                const bool t = !binding && keyDown(g.talkKey);
                if (t && !talkWas) ++g.talkPresses;
                talkWas = t;
                g.realVoice = !binding && keyDown(g.realVoiceKey);
                auto press = [binding](int key, bool& was, std::atomic<int>& count) {
                    const bool down = !binding && keyDown(key);
                    if (down && !was) ++count;
                    was = down;
                };
                press(g.stopKey, stopWas, g.stopPresses);
                press(g.cancelKey, cancelWas, g.cancelPresses);
                press(g.overlayKey, overlayWas, g.overlayPresses);
                press(g.screenKey, screenWas, g.screenPresses);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });
    gListen.start();
    gStatus.start();
    gUsage.start();
    gVr.setThumbnail(kLogos[0].rgba, kLogos[0].size);  // the SteamVR menu tab's icon
    // SteamVR Input: CIMA Box's controls, so a controller button can be push to talk. You pick the
    // button in SteamVR's controller bindings (VRChat still gets that button too).
    {
        const fs::path actions = appDataDir() / "steamvr_actions.json";
        const json manifest = {
            {"default_bindings", json::array()},
            {"action_sets", {{{"name", "/actions/cimabox"}, {"usage", "leftright"}}}},
            {"actions", {{{"name", "/actions/cimabox/in/push_to_talk"}, {"type", "boolean"}, {"requirement", "optional"}},
                         {{"name", "/actions/cimabox/out/buzz"}, {"type", "vibration"}}}},
            {"localization", {{{"language_tag", "en_US"},
                               {"/actions/cimabox", "CIMA Box"},
                               {"/actions/cimabox/in/push_to_talk", "Push to talk"},
                               {"/actions/cimabox/out/buzz", "Buzz when recording starts and stops"}}}},
        };
        std::error_code ec;
        fs::create_directories(appDataDir(), ec);
        std::ofstream f(actions, std::ios::binary | std::ios::trunc);
        if (f) {
            f << manifest.dump(2);
            f.close();
            gVr.setActionManifest(actions.u8string());
        }
    }
    gVr.onLog = [](const std::string& s) { logQuietly(s); };
    gVr.start();
    if (!ui.cfg.apiKey.empty()) refreshCatalog(ui.cfg.apiKey);

    if (!glfwInit()) { fatal("Couldn't start the window system."); return 1; }
#ifdef __APPLE__
    const char* glsl = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    const float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    int winW = int(1240 * scale), winH = int(820 * scale), minW = int(980 * scale), minH = int(620 * scale);  // room for the sidebar
    {  // never open larger than the screen
        int x = 0, y = 0, areaW = 0, areaH = 0;
        glfwGetMonitorWorkarea(glfwGetPrimaryMonitor(), &x, &y, &areaW, &areaH);
        if (areaW > 0 && areaH > 0) {
            winW = std::min(winW, areaW * 95 / 100);
            winH = std::min(winH, areaH * 90 / 100);
            minW = std::min(minW, winW);
            minH = std::min(minH, winH);
        }
    }
    GLFWwindow* win = glfwCreateWindow(winW, winH, "CIMA Box " CIMA_VERSION, nullptr, nullptr);
    if (!win) { fatal("Couldn't open a window. This app needs OpenGL 3."); return 1; }
    glfwSetWindowSizeLimits(win, minW, minH, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
#ifdef _WIN32
    {  // dark title bar to match the window
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(glfwGetWin32Window(win), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    }
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    bidi::installImGuiHook();  // Arabic, Persian, Urdu and Hebrew drawn right to left, letters joined
    ImGui::GetIO().IniFilename = nullptr;
    uiSetup(scale);
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);
    setupLogo(win, ui, scale);
    setupGuidePictures(ui);
    loadPicture(ui);

    int catalogVersion = -1;
    uint64_t captionVersion = 0;
    std::set<uint64_t> convoIds;  // Listen captions already added to the conversation
    std::map<std::string, int> heardCount;  // languages others spoke this session
    std::deque<std::string> theirLangs;     // the last few clear caption languages, oldest first
    std::string autoTarget, handPicked;     // Answer in their language: what it set, and what you overruled
    std::map<uint64_t, std::string> romanized;  // learning mode: how to read a conversation line
    std::future<std::pair<uint64_t, std::string>> romanJob;
    std::future<std::vector<std::string>> tryJob;
    unsigned screenTex = 0;  // screen translation preview
    bool vrScreen = false;   // "Translate screen" pressed in the SteamVR menu
    std::vector<std::string> vrMenuLangs;  // the language buttons in the SteamVR menu, in order (their codes)
    std::string lastTarget;  // to keep "recent languages" up to date
    bool talkWasDown = false;
    std::string voiceKey, voiceKeyVoice;  // language/dialect the voice was last used with
    uint64_t vrSentConvo = ~0ull, vrVersion = 0;  // what the headset panel was last given
    uint64_t chatConvo = ~0ull;                   // what the Text page was last given
    std::string usageEleven, usageDeepl, usageClaude, usageGemini, usageOpenai;  // keys the usage line was set up for
    uint64_t usageConvo = 0;
    bool wasBusy = false;
    double lastUsageLine = 0;
    int lastUpdateStage = -1;
    std::string lastCatalogMsg, lastVrStatus, lastListenStatus;
    long long spendStartUsed = -1, spendBaseCredits = 0;  // ElevenLabs "used" when this session began
    std::string spendStartKey;
    std::future<std::pair<bool, std::string>> keyJobs[kGuideCount];
    double spendBaseDollars = 0;
    std::string lastErrorSeen, lastListenErrorSeen;  // a new problem: look at the status pages again
    bool vrSentShowMe = true;
    unsigned previewTex = 0;
    uint64_t previewConvo = ~0ull;
    VrSettings previewCfg;
    bool previewValid = false;
    PanelImage previewImage;
    std::string previewTag, previewTagShown;  // "Talking to: a girl" on the preview, as in the headset
    OscOut chatbox;  // My voice -> VRChat chatbox
    auto lastChat = Clock::now() - std::chrono::seconds(10);
    int fullW = winW, fullH = winH;  // window size to return to from the captions-only view
    while (!glfwWindowShouldClose(win)) {
        // Light on resources: minimized, nothing is drawn and the loop wakes ~10 times a second (for
        // captions, the chatbox and VR); in the background it draws at most 20 frames a second.
        const bool hidden = glfwGetWindowAttrib(win, GLFW_ICONIFIED) != 0;
        if (hidden) glfwWaitEventsTimeout(0.1);
        else if (!glfwGetWindowAttrib(win, GLFW_FOCUSED)) glfwWaitEventsTimeout(1.0 / 20.0);
        else glfwPollEvents();

        {  // pull shared state
            std::lock_guard<std::mutex> lk(g.mtx);
            if (catalogVersion != g.catalogVersion) {
                ui.voices = g.voices;
                ui.models = g.models;
                catalogVersion = g.catalogVersion;
            }
            ui.catalogMsg = g.catalogMsg;
            ui.lastError = g.lastError;
            ui.heardText = g.heardText;
            ui.sentText = g.sentText;
            ui.meaningText = !g.sentText.empty() && g.meaningFor == withoutTags(g.sentText) ? g.meaningText : std::string();
            ui.delivery = g.delivery;
            ui.qualityNote = g.qualityNote;
            ui.translateVia = g.translateVia;
            ui.cfg.baseline = g.baseline;
            // VRChat mutes the chatbox when it is written to too often: at most one message per 1.6 s.
            if (!g.chatText.empty() && msSince(lastChat) >= 1600) {
                chatbox.chatbox(g.chatText);
                g.chatText.clear();
                lastChat = Clock::now();
            }
            ui.queued = int(g.jobs.size());
        }
        ui.loading = g.loading;
        ui.recording = g.recording;
        ui.busy = g.busy;
        ui.holding = g.holding;
        ui.levelDb = g.levelDb;
        ui.sttMs = g.sttMs;
        ui.sttLive = g.sttLive;
        ui.earlyUsed = g.earlyUsed;
        ui.translateMs = g.translateMs;
        ui.voiceMs = g.voiceMs;
        ui.totalMs = g.totalMs;
        // Hotkeys. On Windows a small thread watches them, so push-to-talk stays instant while this
        // window is in the background (it then only redraws 20 times a second).
        g.pttKey = ui.cfg.pttKey;
        g.talkKey = ui.cfg.talkKey;
        g.binding = ui.binding;
        g.pttTap = ui.cfg.pttTap;
        g.pttActive = ui.cfg.activation == kPushToTalk && ui.cfg.enabled;
        g.vrButton = ui.cfg.vr.pttButton;
        if (!kGlobalHotkey) {
            static PttInput ptt;
            g.pttDown = ptt.update(!ui.binding && (keyDown(ui.cfg.pttKey) || (g.vrButton && gVr.buttonDown())), g.pttTap, g.pttActive);
            const bool t = !ui.binding && keyDown(ui.cfg.talkKey);
            if (t && !talkWasDown) ++g.talkPresses;
            talkWasDown = t;
        }
        static const char* const kTalkNotice[] = {"Auto (their voice)", "Don't know", "A guy", "A girl", "A group"};
        auto notice = [&ui](const char* text, const char* arg = "") {
            ui.notice = text;
            ui.noticeArg = arg;
            ++ui.noticeSerial;
        };
        if (int n = g.talkPresses.exchange(0); n > 0) {
            ui.cfg.talkingTo = (ui.cfg.talkingTo + n) % kTalkCount;
            notice("Talking to: %s", kTalkNotice[ui.cfg.talkingTo % kTalkCount]);
        }
        if (const uint64_t v = gListen.version(); v != captionVersion) {
            captionVersion = v;
            gListen.captions(ui.captions);
            // Finished captions join the conversation (AI context, VR captions), each once.
            std::lock_guard<std::mutex> lk(g.mtx);
            for (const Caption& c : ui.captions) {
                if (c.pending || c.screen || !convoIds.insert(c.id).second) continue;  // screen lines are in the conversation already
                bool spoke = false;
                for (const CaptionLine& l : c.lines) {
                    if (l.voiceId >= 0 && std::find(ui.mutedVoices.begin(), ui.mutedVoices.end(), l.voiceId) != ui.mutedVoices.end())
                        continue;  // muted (Features page): not shown, not spoken, not context
                    ConvoLine them;
                    them.original = l.original.empty() ? l.text : l.original;
                    them.shown = l.text;
                    them.language = c.language;
                    them.code = c.code;
                    them.speaker = l.speaker;
                    them.voice = l.voice;
                    them.voiceId = ui.cfg.fx.voiceIds ? l.voiceId : -1;
                    if (ui.cfg.fx.hearThem && !l.text.empty()) {  // read it in your ear
                        g.earJobs.emplace_back(withoutTags(l.text), ui.cfg.listen.toLang);
                        while (g.earJobs.size() > 3) g.earJobs.pop_front();  // stay current rather than fall behind
                        g.earCv.notify_one();
                    }
                    addConvo(std::move(them));
                    spoke = true;
                }
                // Languages heard this session, and "Answer in their language".
                if (spoke && !c.code.empty()) {
                    ++heardCount[c.code];
                    if (c.sure) theirLangs.push_back(c.code);
                    while (theirLangs.size() > 4) theirLangs.pop_front();
                }
            }
            if (convoIds.size() > 500) convoIds.clear();  // old ids are long gone from the feed
            ui.heardLanguages.clear();
            for (const auto& kv : heardCount) ui.heardLanguages.push_back(kv.first);
            std::sort(ui.heardLanguages.begin(), ui.heardLanguages.end(),
                      [&](const std::string& a, const std::string& b) { return heardCount[a] > heardCount[b]; });
            if (ui.cfg.fx.answerTheirs && theirLangs.size() >= 2) {
                // Two clear captions in a row in the same language, other than yours and other than what
                // you picked by hand over it: translate into that.
                const std::string l = theirLangs.back();
                const bool twice = theirLangs[theirLangs.size() - 2] == l;
                const bool mine = !ui.cfg.spokenLang.empty() && ui.cfg.spokenLang == l;
                if (twice && !mine && l != ui.cfg.targetLang && l != handPicked && findLanguage(l)) {
                    ui.cfg.targetLang = l;
                    autoTarget = l;
                    char b[200];
                    std::snprintf(b, sizeof(b), TF("Answering in %s: they spoke it."), findLanguage(l)->name);
                    ui.autoTargetNote = b;
                    logLine(std::string("Answer in their language: switched to ") + l);
                }
            }
        }
        // Picked by hand while "Answer in their language" is on: stay there until another language comes.
        if (ui.cfg.targetLang != autoTarget) {
            if (ui.cfg.fx.answerTheirs && !autoTarget.empty()) {
                handPicked = theirLangs.empty() ? std::string() : theirLangs.back();
                ui.autoTargetNote.clear();
            }
            autoTarget = ui.cfg.targetLang;
        }
        if (!ui.cfg.fx.answerTheirs) ui.autoTargetNote.clear();
        // ---- Features page: keys and the moment to cancel
        g.realVoiceKey = ui.cfg.fx.realVoiceKey;
        g.stopKey = ui.cfg.fx.stopKey;
        g.cancelKey = ui.cfg.fx.previewMs > 0 ? ui.cfg.fx.cancelKey : 0;
        g.overlayKey = ui.cfg.fx.overlayKey;
        g.screenKey = ui.cfg.screen.on ? ui.cfg.screen.key : 0;
        g.keepHistory = ui.cfg.fx.keepHistory;
        g.earGain = ui.cfg.fx.earVolume;
        if (!kGlobalHotkey) {
            static bool stopWas = false, cancelWas = false, overlayWas = false, screenWas = false;
            g.realVoice = !ui.binding && keyDown(ui.cfg.fx.realVoiceKey);
            auto press = [&ui](int key, bool& was, std::atomic<int>& count) {
                const bool down = !ui.binding && keyDown(key);
                if (down && !was) ++count;
                was = down;
            };
            press(ui.cfg.fx.stopKey, stopWas, g.stopPresses);
            press(g.cancelKey, cancelWas, g.cancelPresses);
            press(ui.cfg.fx.overlayKey, overlayWas, g.overlayPresses);
            press(g.screenKey, screenWas, g.screenPresses);
        }
        if (g.stopPresses.exchange(0) > 0) { stopSpeaking(); notice("Stopped speaking"); }
        if (g.cancelPresses.exchange(0) > 0) {
            g.cancelPending = true;
            notice(ui.pendingSay.empty() ? "Nothing to cancel" : "Cancelled");
        }
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            ui.pendingSay = g.pendingSay;
            ui.pendingLeft = g.pendingSay.empty() ? 0.f : float(std::max(0.0, g.pendingUntil - nowSeconds()));
        }
        ui.listen = gListen.state();

        // ---- who you're talking to: chosen, or Auto = the last person who spoke, by their voice
        {
            int id;
            while (gVr.takeMenuPress(id)) {  // a button in the SteamVR menu
                Settings& c = ui.cfg;
                if (id >= kMenuTalk && id < kMenuTalk + kTalkCount) {
                    c.talkingTo = id - kMenuTalk;
                    notice("Talking to: %s", kTalkNotice[c.talkingTo % kTalkCount]);
                } else if (id == kMenuScreen) {
                    vrScreen = true;
                } else if (id == kMenuOnOff) {
                    c.enabled = !c.enabled;
                    notice(c.enabled ? "Voice changer on" : "Voice changer off");
                } else if (id >= kMenuEngine && id < kMenuEngine + 3) {
                    c.engine = id - kMenuEngine;
                    static const char* const kModes[] = {"Changer", "Re-speak", "Translate"};
                    notice("Mode: %s", kModes[c.engine]);
                } else if (id >= kMenuLang && id < kMenuLang + int(vrMenuLangs.size())) {
                    c.targetLang = vrMenuLangs[size_t(id - kMenuLang)];
                    const Language* l = findLanguage(c.targetLang);
                    notice("They hear: %s", l ? l->name : c.targetLang.c_str());
                } else if (id == kMenuSwap && !c.spokenLang.empty() && !c.targetLang.empty()) {
                    std::swap(c.spokenLang, c.targetLang);
                    const Language* l = findLanguage(c.targetLang);
                    notice("They hear: %s", l ? l->name : c.targetLang.c_str());
                } else if (id == kMenuListen) {
                    c.listen.on = !c.listen.on;
                    notice(c.listen.on ? "Listen on" : "Listen off");
                } else if (id == kMenuEar) {
                    c.fx.hearThem = !c.fx.hearThem;
                    if (c.fx.hearThem) c.listen.on = true;  // it reads out the captions
                    notice(c.fx.hearThem ? "Hear them in my ear: on" : "Hear them in my ear: off");
                } else if (id == kMenuSmaller || id == kMenuBigger) {
                    c.vr.style.textPx = std::clamp(c.vr.style.textPx + (id == kMenuBigger ? 4.f : -4.f), 24.f, 64.f);
                    notice(id == kMenuBigger ? "Captions bigger" : "Captions smaller");
                } else if (id == kMenuPlace) {
                    gVr.placeHere();
                    notice("Captions moved in front of you");
                } else if (id == kMenuResetVr) {
                    gVr.reset();
                    notice("Resetting VR captions");
                } else if (id == kMenuStop) {
                    stopSpeaking();
                    notice("Stopped speaking");
                } else if (id == kMenuCancel) {
                    g.cancelPending = true;
                    notice("Cancelled");
                }
            }
            // Recent languages (for the SteamVR menu): the last few you had people hear.
            if (ui.cfg.targetLang != lastTarget) {
                lastTarget = ui.cfg.targetLang;
                auto& r = ui.cfg.recentTargets;
                if (!lastTarget.empty()) {
                    r.erase(std::remove(r.begin(), r.end(), lastTarget), r.end());
                    r.insert(r.begin(), lastTarget);
                    if (r.size() > 6) r.resize(6);
                }
            }
            int resolved = kListenerUnknown;
            bool guessed = false;
            switch (ui.cfg.talkingTo) {
                case kTalkMan: resolved = kListenerMan; break;
                case kTalkWoman: resolved = kListenerWoman; break;
                case kTalkGroup: resolved = kListenerGroup; break;
                case kTalkAuto: {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    const double now = nowSeconds();
                    for (auto it = g.convo.rbegin(); it != g.convo.rend(); ++it) {
                        if (it->me) continue;
                        if (now - it->at < 60.0) {
                            resolved = it->voice == kGenderMan ? kListenerMan : it->voice == kGenderWoman ? kListenerWoman
                                                                                                         : kListenerUnknown;
                            guessed = resolved != kListenerUnknown;
                        }
                        break;
                    }
                    break;
                }
                default: break;
            }
            g.listener = resolved;
            ui.talkingResolved = resolved;
            ui.talkingGuessed = guessed;
            // The SteamVR menu tab and the note on the caption panel, in the app's language.
            // The SteamVR menu: the things you'd otherwise take the headset off for.
            const Settings& c = ui.cfg;
            VrMenu vt;
            {
                const char* state = !c.enabled ? "Off" : ui.recording ? "Recording" : ui.busy ? "Converting" : "Listening";
                char t[160];
                std::snprintf(t, sizeof(t), "CIMA Box  \xC2\xB7  %s", T(state));
                vt.title = t;
            }
            auto button = [](const char* label, int id, bool on) { return MenuButton{T(label), id, on}; };
            {
                MenuRow r{T("Voice changer"), {}};
                r.buttons.push_back(button(c.enabled ? "On" : "Off", kMenuOnOff, c.enabled));
                const char* const modes[] = {"Changer", "Re-speak", "Translate"};
                for (int i = 0; i < 3; ++i) r.buttons.push_back(button(modes[i], kMenuEngine + i, c.engine == i));
                vt.rows.push_back(std::move(r));
            }
            vrMenuLangs.clear();
            if (c.engine == kTranslate) {
                MenuRow r{T("They hear"), {}};
                std::vector<std::string> langs;
                if (!c.targetLang.empty()) langs.push_back(c.targetLang);
                for (const std::string& code : c.recentTargets)
                    if (langs.size() < 4 && std::find(langs.begin(), langs.end(), code) == langs.end() && findLanguage(code)) langs.push_back(code);
                for (size_t i = 0; i < langs.size(); ++i) {
                    const Language* l = findLanguage(langs[i]);
                    r.buttons.push_back(MenuButton{l ? T(l->name) : langs[i], kMenuLang + int(i), langs[i] == c.targetLang});
                    vrMenuLangs.push_back(langs[i]);
                }
                if (!c.spokenLang.empty() && !c.targetLang.empty()) r.buttons.push_back(button("Swap", kMenuSwap, false));
                if (!r.buttons.empty()) vt.rows.push_back(std::move(r));
            }
            {
                MenuRow r{T("Talking to"), {}};
                const char* const talk[] = {"Auto", "Don't know", "A guy", "A girl", "A group"};
                for (int i = 0; i < kTalkCount; ++i) r.buttons.push_back(button(talk[i], kMenuTalk + i, c.talkingTo == i));
                vt.rows.push_back(std::move(r));
            }
            {
                MenuRow r{T("Captions"), {}};
                r.buttons.push_back(button(c.listen.on ? "Listen: on" : "Listen: off", kMenuListen, c.listen.on));
                r.buttons.push_back(button(c.fx.hearThem ? "In my ear: on" : "In my ear: off", kMenuEar, c.fx.hearThem));
                r.buttons.push_back(button("Smaller", kMenuSmaller, false));
                r.buttons.push_back(button("Bigger", kMenuBigger, false));
                if (c.vr.placement == kVrFixed) r.buttons.push_back(button("In front of me", kMenuPlace, false));
                r.buttons.push_back(button("Reset", kMenuResetVr, false));
                vt.rows.push_back(std::move(r));
            }
            {
                MenuRow r{T("Right now"), {}};
                r.buttons.push_back(button("Stop speaking", kMenuStop, false));
                if (!ui.pendingSay.empty()) r.buttons.push_back(button("Cancel", kMenuCancel, false));
                if (c.screen.on) r.buttons.push_back(button("Translate screen", kMenuScreen, false));
                vt.rows.push_back(std::move(r));
            }
            const char* who = resolved == kListenerMan ? T("a guy") : resolved == kListenerWoman ? T("a girl")
                            : resolved == kListenerGroup ? T("a group") : nullptr;
            char line[256];
            if (ui.cfg.talkingTo == kTalkAuto)
            {
                if (guessed) std::snprintf(line, sizeof(line), TF("Auto: %s, from their voice"), who);
                else std::snprintf(line, sizeof(line), "%s", T("Auto: can't tell yet (it listens to who spoke last)"));
            }
            else
                std::snprintf(line, sizeof(line), "%s", T("Translations use this until you change it."));
            vt.footer = line;
            if (who) {
                std::snprintf(line, sizeof(line), TF("Talking to: %s"), who);
                vt.tag = line;
            }
            gVr.setMenu(vt);
            previewTag = vt.tag;
        }

        // ---- VR captions: new lines only when the conversation (or what to show) changed
        gVr.configure(ui.cfg.vr);
        {
            float pose[12];
            if (gVr.takePlacedPose(pose)) std::copy(pose, pose + 12, ui.cfg.vr.fixedPose);
        }
        ui.vr = gVr.state();
        ui.vrSupported = VrOverlay::supported();
        uint64_t convoNow;
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            convoNow = g.convoVersion;
            if (convoNow != chatConvo) {  // the Text page's conversation
                chatConvo = convoNow;
                ui.chat.clear();
                for (const ConvoLine& c : g.convo) {
                    if (c.voiceId >= 0 && std::find(ui.mutedVoices.begin(), ui.mutedVoices.end(), c.voiceId) != ui.mutedVoices.end())
                        continue;
                    ChatLine l;
                    l.me = c.me;
                    l.typed = c.typed;
                    l.text = c.shown;
                    l.original = c.original != c.shown ? c.original : std::string();
                    l.language = c.language;
                    l.speaker = c.speaker;
                    l.voiceId = c.voiceId;
                    l.key = c.id;
                    l.screen = c.screen;
                    l.code = c.code;
                    if (const auto r = romanized.find(c.id); r != romanized.end()) l.roman = r->second;
                    ui.chat.push_back(std::move(l));
                }
            }
            if (ui.cfg.vr.on && (convoNow != vrSentConvo || ui.cfg.vr.showMe != vrSentShowMe)) {
                gVr.setLines(vrLines(ui.cfg.vr, ui.cfg.screen.toVr), ++vrVersion);
                vrSentConvo = convoNow;
                vrSentShowMe = ui.cfg.vr.showMe;
            }
        }
        // ---- the preview on the VR page, redrawn only when something it shows changed
        if (!hidden && ui.cfg.page == kPageVR) {
            const VrSettings& v = ui.cfg.vr;
            const bool same = previewValid && previewConvo == convoNow && v.showMe == previewCfg.showMe &&
                              v.showOriginal == previewCfg.showOriginal && v.style.textPx == previewCfg.style.textPx &&
                              v.style.maxLines == previewCfg.style.maxLines && v.style.textColor == previewCfg.style.textColor &&
                              v.style.accentColor == previewCfg.style.accentColor && v.style.meColor == previewCfg.style.meColor &&
                              v.style.panelOpacity == previewCfg.style.panelOpacity && v.style.glow == previewCfg.style.glow &&
                              v.showTalkingTo == previewCfg.showTalkingTo && previewTag == previewTagShown;
            if (!same) {
                std::vector<PanelLine> lines;
                {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    lines = vrLines(v, ui.cfg.screen.toVr);
                }
                if (lines.empty()) lines = sampleLines();
                PanelStyle style = v.style;
                style.showSub = v.showOriginal;
                style.tag = v.showTalkingTo ? previewTag : std::string();
                sharedPainter().paint(lines, style, previewImage);
                if (!previewTex) glGenTextures(1, &previewTex);
                glBindTexture(GL_TEXTURE_2D, previewTex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, previewImage.width, previewImage.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             previewImage.rgba.data());
                ui.previewTexture = previewTex;
                ui.previewW = previewImage.width;
                ui.previewH = previewImage.height;
                ui.previewEmpty = previewImage.empty;
                previewConvo = convoNow;
                previewCfg = v;
                previewTagShown = previewTag;
                previewValid = true;
            }
        }

        UiActions act;
        if (!hidden) {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            act = uiDraw(ui);
        }
        if (act.placeVrHere) gVr.placeHere();
        if (act.openVrBindings) gVr.openBindings();
        if (act.resetVr) { gVr.reset(); notice("Resetting VR captions"); }
        if (!ui.previewWanted.empty()) {
            const std::string want = ui.previewWanted;
            ui.previewWanted.clear();
            std::string url;
            for (const Voice& v : ui.voices)
                if (v.id == want) url = v.previewUrl;
            if (want == "stop" || url.empty()) gPreview.halt();
            else gPreview.play(want, url, ui.cfg.monDevice);
        }
        ui.previewing = gPreview.shown();
        if (act.startCheck) gCheck.start(ui);
        if (act.stopCheck) { gCheck.stop(ui); ui.checkOpen = false; }
        gCheck.update(ui);
#ifdef _WIN32
        if (act.openSoundPanel) ShellExecuteW(nullptr, L"open", L"control.exe", L"mmsys.cpl,,0", nullptr, SW_SHOWNORMAL);
#endif

        if (act.rescanDevices) refreshDeviceList(ui);
        keepDevicesAlive(ui);
        if (act.reopenInput) openCapture(ui.cfg.inDevice);
        if (act.reopenOutput) openSink(g.out, ui.cfg.outDevice, true);
        if (act.reopenMonitor) openSink(g.mon, ui.cfg.monDevice, ui.cfg.monitor);
        if (act.loadCatalog) refreshCatalog(ui.cfg.apiKey);
        if (act.stopSpeaking) stopSpeaking();
        if (act.refreshApps) ui.apps = listApps();
        if (act.clearCaptions) gListen.clear();
        if (act.openCaptionWindow) act.setCaptionsOnly = 1;
        if (act.setCaptionsOnly >= 0 && (act.setCaptionsOnly == 1) != ui.captionsOnly) {
            ui.captionsOnly = act.setCaptionsOnly == 1;
            ui.overlayClickThrough = ui.captionsOnly && ui.cfg.fx.clickThrough && ui.cfg.fx.overlayKey != 0;
            if (ui.captionsOnly) {  // small, and above the game
                glfwRestoreWindow(win);
                glfwGetWindowSize(win, &fullW, &fullH);
                glfwSetWindowSizeLimits(win, int(340 * scale), int(130 * scale), GLFW_DONT_CARE, GLFW_DONT_CARE);
                glfwSetWindowSize(win, int(720 * scale), int(300 * scale));
            } else {
                glfwSetWindowSize(win, fullW, fullH);
                glfwSetWindowSizeLimits(win, minW, minH, GLFW_DONT_CARE, GLFW_DONT_CARE);
            }
            glfwSetWindowAttrib(win, GLFW_FLOATING, ui.captionsOnly ? GLFW_TRUE : GLFW_FALSE);
        }
        // The mini window: My voice at a glance, small and on top of the game.
        if (act.setMini >= 0 && (act.setMini == 1) != ui.mini && !ui.captionsOnly) {
            ui.mini = act.setMini == 1;
            if (ui.mini) {
                glfwRestoreWindow(win);
                glfwGetWindowSize(win, &fullW, &fullH);
                glfwSetWindowSizeLimits(win, int(380 * scale), int(170 * scale), GLFW_DONT_CARE, GLFW_DONT_CARE);
                glfwSetWindowSize(win, int(480 * scale), int((ui.cfg.listen.on ? 360 : 250) * scale));
            } else {
                glfwSetWindowSize(win, fullW, fullH);
                glfwSetWindowSizeLimits(win, minW, minH, GLFW_DONT_CARE, GLFW_DONT_CARE);
            }
            glfwSetWindowAttrib(win, GLFW_FLOATING, ui.mini ? GLFW_TRUE : GLFW_FALSE);
        }
        {   // The mini window makes room for what others say when Listen goes on, and gives it back when it goes off.
            static bool miniTall = false;
            if (ui.mini && ui.cfg.listen.on != miniTall) {
                int w = 0, h = 0;
                glfwGetWindowSize(win, &w, &h);
                glfwSetWindowSize(win, w, int((ui.cfg.listen.on ? 360 : 250) * scale));
            }
            miniTall = ui.mini && ui.cfg.listen.on;
        }
        ui.passing = g.passing;
        ui.matchProgress = (g.match.micSeconds() + g.match.voiceSeconds()) / (2.f * MicMatch::kNeedSeconds);
        if (act.resetMatch) g.match.forgetMic();
        if (act.resetBaseline) {
            std::lock_guard<std::mutex> lk(g.mtx);
            g.baseline = VoiceBaseline();
            g.delivery.clear();
            ui.cfg.baseline = g.baseline;
        }
        if (act.save) saveSettings(ui.cfg);

        // ---- the voice you last used with each language / dialect comes back with it
        {
            const auto vit = ui.cfg.variety.find(ui.cfg.targetLang);
            const std::string key = ui.cfg.targetLang + "/" + (vit == ui.cfg.variety.end() ? std::string() : vit->second);
            const bool translating = ui.cfg.engine == kTranslate || (ui.cfg.page == kPageText && ui.cfg.textTranslate);
            if (ui.cfg.voicePerLanguage && translating && !voiceKey.empty() && key != voiceKey) {
                if (!voiceKeyVoice.empty()) ui.cfg.voiceFor[voiceKey] = voiceKeyVoice;
                const auto it = ui.cfg.voiceFor.find(key);
                if (it != ui.cfg.voiceFor.end()) {
                    bool exists = ui.voices.empty();  // not loaded yet: trust the saved id
                    for (const Voice& v : ui.voices) exists = exists || v.id == it->second;
                    if (exists) ui.cfg.voiceId = it->second;
                }
            }
            voiceKey = key;
            voiceKeyVoice = ui.cfg.voiceId;
        }
        // ---- updates: one quiet check a few seconds after starting, then only when asked
        if (ui.updatesConfigured) {
            if (!updateChecked && ui.cfg.checkUpdates && nowSeconds() > 4.0) {
                updateChecked = true;
                updater.check();
            }
            if (act.checkUpdates) {
                updateChecked = true;
                updater.check();
            }
            if (act.installUpdate) {
                saveSettings(ui.cfg);
                updater.install();
            }
            if (act.listOlder) updater.listOlder();
            if (!act.installOlder.empty()) {
                // Don't offer the version being left again (a newer one still shows), and keep what an
                // earlier version would drop, for when you come forward again.
                const std::string latest = ui.update.latest;
                ui.cfg.skipUpdate = !latest.empty() && compareVersions(latest, CIMA_FULL_VERSION) > 0 ? latest : CIMA_FULL_VERSION;
                saveSettings(ui.cfg);
                keepSettingsForLater(settingsJson(ui.cfg));
                logLine("Going back to CIMA Box " + act.installOlder + ".");
                updater.installOlder(act.installOlder);
            }
            ui.update = updater.state();
            if (updater.restartWanted()) glfwSetWindowShouldClose(win, GLFW_TRUE);  // relaunched after cleanup
        }
        {   // Models on this PC: downloads and deletes asked for in the app, and which one should be loaded.
            if (!act.localDownload.empty()) localAi.download(act.localDownload);
            if (!act.localCancel.empty()) localAi.cancel(act.localCancel);
            if (!act.localRemove.empty()) {
                localAi.remove(act.localRemove);
                if (ui.cfg.localModel == act.localRemove) ui.cfg.localModel.clear();
            }
            ui.local = localAi.state();
            if (act.voicePack == 1) voiceId.download();
            if (act.voicePack == 2) voiceId.cancel();
            if (act.voicePack == 3) voiceId.remove();
            ui.voicePack = voiceId.state();
            // Nothing chosen (or the chosen one was deleted): the first one that's downloaded.
            auto has = [&](const std::string& id) {
                for (const LocalModelState& ms : ui.local.models)
                    if (ms.id == id) return ms.stage == kModelInstalled;
                return false;
            };
            if (!has(ui.cfg.localModel))
                for (const LocalModelState& ms : ui.local.models)
                    if (ms.stage == kModelInstalled) { ui.cfg.localModel = ms.id; break; }
            // Loaded only while Local is the translation service: otherwise its memory is the game's.
            // ...or while it stands by for lines an online AI declines (the person's choice).
            const bool standBy = ui.cfg.localOnDecline && isOnlineAi(ui.cfg.translateService);
            const bool wanted = (ui.cfg.translateService == kLocal || standBy) && has(ui.cfg.localModel);
            localAi.want(wanted ? ui.cfg.localModel : std::string(), ui.cfg.localGpu);
            LocalNow now;
            if (ui.local.engine == kEngineReady) {
                now.url = ui.local.url;
                now.key = ui.local.key;
                now.model = ui.local.running;
            } else {
                now.model = ui.cfg.localModel;
                now.note = !wanted                               ? "No local model is downloaded"
                           : ui.local.engine == kEngineFailed    ? ui.local.engineMessage
                           : ui.local.engine == kEngineGetting   ? "The local engine is still downloading"
                                                                 : "The local model is still loading";
            }
            std::lock_guard<std::mutex> lk(gLocalMx);
            gLocalNow = now;
        }
        {   // "Which means" just switched on: the line already showing gets its meaning too, not only the next ones.
            static bool hadMeaning = ui.cfg.showMeaning;
            if (ui.cfg.showMeaning && !hadMeaning) {
                std::string line, toCode, mineCode;
                bool have = false;
                {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    line = withoutTags(g.sentText);
                    toCode = g.sentTo;
                    mineCode = g.sentMine;
                    have = g.meaningFor == line;
                }
                const Language* to = findLanguage(toCode);
                const Language* mine = findLanguage(mineCode);
                if (!have && !line.empty() && to && mine && to != mine) checkMeaning(ui.cfg, line, *to, *mine);
            }
            hadMeaning = ui.cfg.showMeaning;
        }
        // Settings save themselves a moment after any change (only when something actually changed).
        if (nowSeconds() - lastAutosave > 2.0) {
            lastAutosave = nowSeconds();
            saveSettings(ui.cfg);
        }
        if (act.setSettingsPlace >= 0) {
            moveSettings(ui.cfg, act.setSettingsPlace, ui.settingsError);
            ui.settingsPlace = gPlace;
            ui.settingsPath = settingsFile().u8string();
            startLog();
            ui.logPath = (settingsDir() / "cimabox.log").u8string();
            if (!ui.settingsError.empty()) logLine("Moving settings: " + ui.settingsError);
        }
        if (!act.testSound.empty()) {  // the setup's "Play a test"
            Phrase t;
            t.text = act.testSound;
            t.test = true;
            std::lock_guard<std::mutex> lk(g.mtx);
            g.jobs.push_back(std::move(t));
            g.jobCv.notify_one();
        }
        if (!act.sendText.empty() && ui.spendStopped) {  // stopped at the spending limit until it's acknowledged
            std::lock_guard<std::mutex> lk(g.mtx);
            g.lastError = "Stopped at your spending limit. Press OK on the notice at the top to keep going.";
            act.sendText.clear();
        }
        if (!act.sendText.empty()) {  // typed on the Text page: same pipeline, without the microphone
            Phrase typed;
            typed.text = act.sendText;
            std::lock_guard<std::mutex> lk(g.mtx);
            g.jobs.push_back(std::move(typed));
            g.jobCv.notify_one();
        }
        // ---- service status: ask again soon after something fails, in case it's on their side
        ui.services = gStatus.state();
        if (ui.lastError != lastErrorSeen || ui.listen.error != lastListenErrorSeen) {
            if ((!ui.lastError.empty() && ui.lastError != "cancelled") || !ui.listen.error.empty()) gStatus.checkSoon();
            if (!ui.lastError.empty() && ui.lastError != lastErrorSeen && ui.lastError != "cancelled") logLine("My voice: " + ui.lastError);
            if (!ui.listen.error.empty() && ui.listen.error != lastListenErrorSeen) logLine("Listen: " + ui.listen.error);
            if (ui.lastError.empty() && ui.listen.error.empty()) gUsage.refreshSoon();
            lastErrorSeen = ui.lastError;
            lastListenErrorSeen = ui.listen.error;
        }
        if (!act.openUrl.empty()) openPath(fs::u8path(act.openUrl));
        if (ui.setupStep >= 0) {  // remember the walkthrough's place, in case the PC restarts (VB-CABLE asks for that)
            ui.cfg.setupAt = ui.setupStep;
            ui.cfg.setupUse = ui.setupUse;
            ui.cfg.setupRoute = ui.setupRoute;
            ui.cfg.setupAi = ui.setupAi;
        }
        // "Check key" for DeepL, Gemini, Claude and OpenAI: a tiny request in the background.
        if (act.checkKey >= kGuideDeepL && act.checkKey < kGuideCount && ui.keyCheck[act.checkKey] != kChecking) {
            const int g = act.checkKey;
            const int service = g == kGuideDeepL ? kDeepL : g == kGuideClaude ? kClaude : g == kGuideOpenAI ? kOpenAI : kGemini;
            const std::string key = g == kGuideDeepL    ? ui.cfg.deeplKey
                                    : g == kGuideClaude ? ui.cfg.claudeKey
                                    : g == kGuideOpenAI ? ui.cfg.openaiKey
                                                        : ui.cfg.geminiKey;
            ui.keyCheck[g] = kChecking;
            ui.keyCheckMsg[g].clear();
            keyJobs[g] = std::async(std::launch::async, [service, key] {
                std::string msg;
                const bool ok = checkServiceKey(service, key, msg);
                return std::make_pair(ok, msg);
            });
        }
        for (int g = 0; g < kGuideCount; ++g)
            if (keyJobs[g].valid() && keyJobs[g].wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const auto r = keyJobs[g].get();
                ui.keyCheck[g] = r.first ? kCheckGood : kCheckBad;
                ui.keyCheckMsg[g] = r.second;
                logLine(std::string(g == kGuideDeepL ? "DeepL" : g == kGuideClaude ? "Claude" : g == kGuideOpenAI ? "OpenAI" : "Gemini") +
                        " key check: " + r.second);
            }
        if (act.openLog) {
            logLine("Log opened from Settings.");  // makes sure the file exists
            openPath(fs::u8path(ui.logPath));
        }
        // ---- Features page: profiles, shared setups, problem report
        if (!act.saveProfile.empty()) {
            std::string name = act.saveProfile;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            if (!name.empty()) ui.cfg.fx.profiles[name] = profileJson(ui.cfg);
        }
        if (!act.deleteProfile.empty()) ui.cfg.fx.profiles.erase(act.deleteProfile);
        if (!act.loadProfile.empty()) {
            const auto it = ui.cfg.fx.profiles.find(act.loadProfile);
            if (it != ui.cfg.fx.profiles.end()) {
                const Settings before = ui.cfg;
                applyProfile(ui.cfg, it->second);
                if (ui.cfg.inDevice != before.inDevice) openCapture(ui.cfg.inDevice);
                if (ui.cfg.outDevice != before.outDevice) openSink(g.out, ui.cfg.outDevice, true);
                if (ui.cfg.monDevice != before.monDevice || ui.cfg.monitor != before.monitor)
                    openSink(g.mon, ui.cfg.monDevice, ui.cfg.monitor);
                if (ui.cfg.uiLanguage != before.uiLanguage) setUiLanguage(ui.cfg.uiLanguage);
                logLine("Profile loaded. " + usingLine(ui.cfg));
            }
        }
        if (act.copySetupCode) {
            ImGui::SetClipboardText(setupToCode(profileJson(ui.cfg, true)).c_str());
            ui.setupCodeMsg = T("Copied. Paste it anywhere: it has no keys in it.");
        }
        if (act.pasteSetupCode) {
            const char* clip = ImGui::GetClipboardText();
            std::string js;
            if (clip && codeToSetup(clip, js) && json::parse(js, nullptr, false).is_object()) {
                std::string name = T("Shared setup");
                for (int n = 2; ui.cfg.fx.profiles.count(name); ++n) name = std::string(T("Shared setup")) + " " + std::to_string(n);
                ui.cfg.fx.profiles[name] = js;
                char b[300];
                std::snprintf(b, sizeof(b), TF("Added as the profile \"%s\". Load it to use it."), name.c_str());
                ui.setupCodeMsg = b;
            } else {
                ui.setupCodeMsg = T("No setup code on the clipboard. Copy the whole code first.");
            }
        }
        if (act.reportProblem) {
            // A file with what's needed to find the problem (no keys, nothing anyone said), and a new
            // report on the project's GitHub page with the basics filled in. Reports there are public,
            // so the Windows user name is taken out of any folder paths too.
            auto withoutName = [](std::string t) {
                const char* home = std::getenv("USERPROFILE");
                const char* user = std::getenv("USERNAME");
                auto swap = [&t](const std::string& what, const std::string& with) {
                    if (what.size() < 3) return;
                    for (size_t at = t.find(what); at != std::string::npos; at = t.find(what, at + with.size())) t.replace(at, what.size(), with);
                };
                if (home) swap(home, "%USERPROFILE%");
                if (user) swap(std::string("\\Users\\") + user, "\\Users\\(you)");
                return t;
            };
            std::string basics = std::string("CIMA Box ") + CIMA_FULL_VERSION + " on " + windowsVersion() + "\n" + usingLine(ui.cfg) + "\n";
            if (!ui.lastError.empty()) basics += "My voice problem: " + ui.lastError + "\n";
            if (!ui.listen.error.empty()) basics += "Listen problem: " + ui.listen.error + "\n";
            basics = withoutName(logRedact(basics));
            std::string text = basics + "\n---- cimabox.log (the end) ----\n";
            std::ifstream lf(logPath(), std::ios::binary);
            std::string all((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
            if (all.size() > 60000) all = all.substr(all.size() - 60000);
            text += withoutName(logRedact(all));  // keys were never written, but just in case
            const fs::path file = desktopDir() / "CIMA Box problem report.txt";
            std::ofstream rf(file, std::ios::binary);
            rf << text;
            rf.close();
            ui.reportPath = file.u8string();
            // The web address has to stay short (Windows opens at most about 2,000 characters), so the
            // page gets the basics and the file carries the rest.
            if (basics.size() > 450) basics = clipUtf8(basics, 450) + "...";
            const std::string body = "**What happened?**\n(write here: what you did, and what went wrong)\n\n"
                                     "**Report file**\nDrag \"CIMA Box problem report.txt\" from your Desktop into this box.\n\n"
                                     "**Basics**\n```\n" + basics + "```\n";
            openPath(fs::u8path("https://github.com/Aye7even/CIMA-Box/issues/new?title=" +
                                Http::urlEncode(std::string("Problem report (") + CIMA_FULL_VERSION + ")") + "&body=" + Http::urlEncode(body)));
        }
        // ---- Features page: typed lines, trying styles, learning mode, history, the caption window
        if (!act.sendText.empty()) countSentLine(ui.cfg.fx, act.sendText);
        if (act.cancelPending) g.cancelPending = true;
        if (!act.tryLine.empty() && !tryJob.valid()) {
            ui.tryBusy = true;
            for (std::string& r : ui.tryResults) r.clear();
            const Settings c = ui.cfg;
            const std::string line = act.tryLine;
            tryJob = std::async(std::launch::async, [c, line] {
                std::vector<std::string> out(4);
                const Language* to = findLanguage(c.targetLang);
                const Language* from = findLanguage(c.spokenLang);
                if (!to) return out;
                std::vector<std::future<std::string>> jobs(4);
                for (int lv = 0; lv < 4; ++lv) {
                    if (lv == kLevelCustom && c.customStyle.find_first_not_of(' ') == std::string::npos) continue;
                    jobs[size_t(lv)] = std::async(std::launch::async, [&c, &line, lv, from, to] {
                        TranslateSetup t = translateSetup(c);
                        t.level = lv;
                        t.timeoutMs = std::max(t.timeoutMs, 8000);
                        Translator tr;
                        std::string o, e;
                        const bool ok = tr.translate(t, line, from, *to, o, e);
                        return ok && tr.via.find('(') == std::string::npos ? o : "(" + (ok ? tr.via : e) + ")";
                    });
                }
                for (size_t i = 0; i < 4; ++i)
                    if (jobs[i].valid()) out[i] = jobs[i].get();
                return out;
            });
        }
        if (tryJob.valid() && tryJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const std::vector<std::string> r = tryJob.get();
            for (size_t i = 0; i < 4 && i < r.size(); ++i) ui.tryResults[i] = r[i];
            ui.tryBusy = false;
        }
        if (act.romanize && !romanJob.valid() && !romanized.count(act.romanize)) {
            std::string text, lang;
            {
                std::lock_guard<std::mutex> lk(g.mtx);
                for (const ConvoLine& c : g.convo)
                    if (c.id == act.romanize) {
                        const Language* tl = findLanguage(ui.cfg.targetLang);
                        text = c.me ? c.shown : c.original;
                        lang = c.me ? (tl ? tl->name : "") : c.language;
                    }
            }
            const TranslateSetup t = translateSetup(ui.cfg, false);
            const uint64_t id = act.romanize;
            romanized[id] = "...";
            chatConvo = ~0ull;
            romanJob = std::async(std::launch::async, [t, text, lang, id] {
                Translator tr;
                std::string o, e;
                return std::make_pair(id, tr.romanize(t, text, lang, o, e) ? o : "(" + e + ")");
            });
        }
        if (romanJob.valid() && romanJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto r = romanJob.get();
            romanized[r.first] = r.second;
            chatConvo = ~0ull;  // show it
        }
        if (!act.replayText.empty()) {
            std::lock_guard<std::mutex> lk(g.mtx);
            g.earJobs.emplace_back(act.replayText, act.replayLang);
            g.earCv.notify_one();
        }
        if (!act.historySearch.empty()) ui.historyResults = searchHistory(act.historySearch);
        if (act.openHistory) {
            std::error_code ec;
            fs::create_directories(historyDir(), ec);
            openPath(historyDir());
        }
        {   // the ear device: open while something may be read into it
            static std::string earName;
            static bool earWanted = false;
            const bool want = ui.cfg.fx.hearThem || ui.cfg.fx.learning;
            const std::string name = ui.cfg.fx.earDevice.empty() ? ui.cfg.monDevice : ui.cfg.fx.earDevice;
            if (want != earWanted || name != earName) {
                openSink(g.ear, name, want);
                earWanted = want;
                earName = name;
            }
        }
        {   // the caption window: see-through, and clicks go to the game while click-through is on
            if (g.overlayPresses.exchange(0) > 0 && ui.captionsOnly) {
                ui.overlayClickThrough = !ui.overlayClickThrough;
                notice(ui.overlayClickThrough ? "Caption window: clicks go through to the game" : "Caption window: you can move it now");
            }
            static bool through = false;
            static float opacity = 1.f;
            const bool wantThrough = ui.captionsOnly && ui.overlayClickThrough && ui.cfg.fx.overlayKey != 0;
            if (wantThrough != through) {
                glfwSetWindowAttrib(win, GLFW_MOUSE_PASSTHROUGH, wantThrough ? GLFW_TRUE : GLFW_FALSE);
                through = wantThrough;
            }
            const float wantOpacity = ui.captionsOnly ? ui.cfg.fx.overlayOpacity : 1.f;
            if (std::fabs(wantOpacity - opacity) > 0.005f) {
                glfwSetWindowOpacity(win, wantOpacity);
                opacity = wantOpacity;
            }
        }
        // ---- screen translation: requests to its thread, its pictures and results to the UI
        {
            const bool snap = act.screenSnapshot || g.screenPresses.exchange(0) > 0 || vrScreen;
            vrScreen = false;
            std::lock_guard<std::mutex> lk(gScreen.mtx);
            if (snap && ui.cfg.screen.on) {
                if (!act.screenSnapshot) notice("Translating the screen");
                gScreen.snapshot = true;
                // From a key or VR you're in the game: no preview to click.
                gScreen.direct = ui.cfg.screen.skipPreview || !act.screenSnapshot;
                gScreen.cv.notify_one();
            }
            if (act.screenTranslate) {
                std::copy(ui.shot.crop, ui.shot.crop + 4, gScreen.crop);
                std::copy(ui.shot.crop, ui.shot.crop + 4, ui.cfg.screen.area);  // the key, VR and Watch use the same part next time
                gScreen.translate = true;
                gScreen.cv.notify_one();
            }
            if (act.screenUseArea) {
                std::copy(ui.shot.crop, ui.shot.crop + 4, ui.cfg.screen.area);
                ui.cfg.screen.watch = true;
                ui.shot.show = false;
            }
            if (act.screenClose) ui.shot.show = false;
            if (gScreen.newPicture) {
                gScreen.newPicture = false;
                const Picture shown = scaledDown(gScreen.picture, 1600);
                if (!screenTex) glGenTextures(1, &screenTex);
                glBindTexture(GL_TEXTURE_2D, screenTex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, shown.width, shown.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, shown.rgba.data());
                ui.shot.texture = screenTex;
                ui.shot.width = gScreen.picture.width;
                ui.shot.height = gScreen.picture.height;
                ui.shot.app = gScreen.app;
                ui.shot.tokens = gScreen.tokens;
                std::copy(ui.cfg.screen.area, ui.cfg.screen.area + 4, ui.shot.crop);
                ui.shot.show = true;
            }
            ui.shot.busy = gScreen.busy;
            ui.shot.lines = gScreen.lines;
            ui.screenStatus = gScreen.status;
            ui.readerLanguages = gScreen.readerLangs;
            ui.readerKnown = gScreen.readerKnown;
        }
        ui.capturing = ui.cfg.screen.on && ui.cfg.screen.watch ? ui.cfg.screen.app : std::string();

        // ---- what's left: ElevenLabs and DeepL from their accounts, the AI services counted here
        if (ui.cfg.apiKey != usageEleven || ui.cfg.deeplKey != usageDeepl || ui.cfg.claudeKey != usageClaude ||
            ui.cfg.geminiKey != usageGemini || ui.cfg.openaiKey != usageOpenai) {
            usageEleven = ui.cfg.apiKey;
            usageDeepl = ui.cfg.deeplKey;
            usageClaude = ui.cfg.claudeKey;
            usageGemini = ui.cfg.geminiKey;
            usageOpenai = ui.cfg.openaiKey;
            for (const std::string* k : {&usageEleven, &usageDeepl, &usageClaude, &usageGemini, &usageOpenai}) logSecret(*k);
            gUsage.setKeys(usageEleven, usageDeepl);
        }
        if ((wasBusy && !ui.busy) || convoNow != usageConvo) gUsage.refreshSoon();  // something was just used
        wasBusy = ui.busy;
        usageConvo = convoNow;
        if (nowSeconds() - lastUsageLine > 1.0) {
            lastUsageLine = nowSeconds();
            ui.usage.clear();
            if (!ui.cfg.apiKey.empty()) {
                ui.usage.push_back(quotaItem("ElevenLabs", gUsage.eleven(), "%s credits left", "credits"));
                if (ui.sessionCredits >= 0) {
                    char b[160];
                    std::snprintf(b, sizeof(b), TF("This session: %s credits."), withCommas(ui.sessionCredits).c_str());
                    ui.usage.back().detail += std::string(" ") + b;
                }
            }
            const int sv = ui.cfg.translateService;
            if (sv == kDeepL && !ui.cfg.deeplKey.empty()) ui.usage.push_back(quotaItem("DeepL", gUsage.deepl(), "%s characters left", "characters"));
            if (sv == kClaude && !ui.cfg.claudeKey.empty()) ui.usage.push_back(aiItem(kClaude, ui.cfg.claudeModel));
            if (sv == kGemini && !ui.cfg.geminiKey.empty()) ui.usage.push_back(aiItem(kGemini, ui.cfg.geminiModel));
            if (sv == kOpenAI && !ui.cfg.openaiKey.empty()) ui.usage.push_back(aiItem(kOpenAI, ui.cfg.openaiModel));
        }
        // ---- spending limit: per session, counted from when CIMA Box started
        {
            const Quota el = gUsage.eleven();
            if (el.known) {
                if (spendStartUsed < 0 || spendStartKey != usageEleven || el.used < spendStartUsed) {  // new key, or a new month
                    spendStartUsed = el.used;
                    spendStartKey = usageEleven;
                    spendBaseCredits = 0;
                }
                ui.sessionCredits = el.used - spendStartUsed;
            } else {
                ui.sessionCredits = -1;
            }
            const AiSpend cl = aiSpend(kClaude, ui.cfg.claudeModel), ge = aiSpend(kGemini, ui.cfg.geminiModel),
                          oa = aiSpend(kOpenAI, ui.cfg.openaiModel);
            ui.sessionDollars = std::max(0.0, cl.dollars) + std::max(0.0, ge.dollars) + std::max(0.0, oa.dollars);
            if (act.spendOk) {  // the next notice comes after another full limit from here
                spendBaseCredits = std::max<long long>(0, ui.sessionCredits);
                spendBaseDollars = ui.sessionDollars;
                ui.spendAlert.clear();
                ui.spendStopped = false;
            }
            const bool creditsOver = ui.sessionCredits >= 0 && ui.sessionCredits - spendBaseCredits >= ui.cfg.spendCredits;
            const bool dollarsOver = ui.sessionDollars - spendBaseDollars >= double(ui.cfg.spendDollars);
            if (ui.cfg.spendWarn && ui.spendAlert.empty() && (creditsOver || dollarsOver)) {
                char b[300];
                if (creditsOver)
                    std::snprintf(b, sizeof(b), TF("This session has used %s ElevenLabs credits, your limit."),
                                  withCommas(ui.sessionCredits).c_str());
                else
                    std::snprintf(b, sizeof(b), TF("This session has spent $%.2f on translations, your limit."), ui.sessionDollars);
                ui.spendAlert = b;
                logLine(std::string("Spending limit reached: ") + (creditsOver ? withCommas(ui.sessionCredits) + " credits" : "$" + std::to_string(ui.sessionDollars)) +
                        (ui.cfg.spendStop ? ", switched off talking and listening." : "."));
                if (ui.cfg.spendStop) {
                    ui.cfg.enabled = false;
                    ui.cfg.listen.on = false;
                    stopSpeaking();
                    ui.spendStopped = true;
                }
            }
        }
        if (ui.update.stage != lastUpdateStage) {
            lastUpdateStage = ui.update.stage;
            if (ui.update.stage == kUpdateFailed || ui.update.stage == kUpdateAvailable || ui.update.stage == kUpdateReady)
                logLine("Update: " + ui.update.message);
        }
        // VR and Listen: each change of state (connected, waiting for SteamVR, listening to ...).
        if (!ui.cfg.vr.on) lastVrStatus.clear();
        else if (ui.vr.status != lastVrStatus) {
            lastVrStatus = ui.vr.status;
            if (!lastVrStatus.empty()) logLine("VR: " + lastVrStatus);
        }
        if (!ui.cfg.listen.on) lastListenStatus.clear();
        else if (ui.listen.status != lastListenStatus) {
            lastListenStatus = ui.listen.status;
            if (!lastListenStatus.empty()) logLine("Listen: " + lastListenStatus);
        }
        if (ui.catalogMsg != lastCatalogMsg) {
            lastCatalogMsg = ui.catalogMsg;
            if (!ui.catalogMsg.empty()) logLine("Voices: " + ui.catalogMsg);
        }
        if (act.pickPicture) pickPicture(win, ui);
        if (act.clearPicture) {
            std::error_code ec;
            for (const char* old : {"background.jpg", "background.jpeg", "background.png"}) fs::remove(settingsDir() / old, ec);
            ui.cfg.look.picture.clear();
            ui.pictureMsg.clear();
            loadPicture(ui);
        }
        if (act.openSettingsFolder) {
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", settingsDir().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        }
        g.outGain = std::max(0.f, std::min(2.f, ui.cfg.outVolume));
        g.monGain = std::max(0.f, std::min(2.f, ui.cfg.monVolume));
        ui.firstRun = ui.cfg.apiKey.empty();
        {  // publish settings to the pipelines
            std::lock_guard<std::mutex> lk(g.mtx);
            if (g.cfg.quality != ui.cfg.quality) {  // a new choice: try it again, even if it was refused before
                g.qualityCap = kQualityBest;
                g.mp3Step = 0;
                g.qualityNote.clear();
            }
            g.cfg = ui.cfg;
        }
        {
            ListenConfig lc;
            lc.s = ui.cfg.listen;
            lc.apiKey = ui.cfg.apiKey;
            lc.translate = translateSetup(ui.cfg, false);
            lc.words = ui.cfg.fx.words;
            lc.around = ui.cfg.fx.aroundLanguages;
            lc.soundLabels = ui.cfg.fx.soundLabels;
            lc.ignoreNoise = ui.cfg.fx.ignoreNoise;
            lc.liveCaptions = ui.cfg.fx.liveCaptions;
            lc.liveTranslate = ui.cfg.fx.liveTranslate;
            lc.voiceIds = ui.cfg.fx.voiceIds;
            lc.voiceModel = ui.voicePack.stage == kVoiceIdInstalled;
            lc.keepVoices = ui.mutedVoices;
            lc.maskSwears = ui.cfg.fx.maskCaptions;
            gListen.configure(lc);
        }

        if (hidden) continue;  // nothing to draw while minimized
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.086f, 0.102f, 0.137f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    saveSettings(ui.cfg);
    g.running = false;
    g.cancel = true;
    g.jobCv.notify_all();
    g.earCv.notify_all();
    gScreen.cv.notify_all();
    capThread.join();
    workThread.join();
    earThread.join();
    screenThread.join();
    keyThread.join();
    gPreview.halt();
    openSink(g.preview, "", false);
    gVr.stop();
    gListen.stop();
    gStatus.stop();
    gUsage.stop();
    logLine("Closed.");
    if (g.capOn) ma_device_uninit(&g.capDev);
    openSink(g.out, "", false);
    openSink(g.mon, "", false);
    openSink(g.ear, "", false);
    ma_context_uninit(&g.ctx);

    if (gLogoTexture) glDeleteTextures(1, &gLogoTexture);
    if (previewTex) glDeleteTextures(1, &previewTex);
    if (screenTex) glDeleteTextures(1, &screenTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    if (updater.restartWanted()) Updater::relaunch();  // the new version, now that this one let go of everything
    return 0;
}
