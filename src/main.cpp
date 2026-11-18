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
#include <shlobj.h>    // SHGetKnownFolderPath: the Documents folder
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdio>
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
#include "listen.h"
#include "micmatch.h"
#include "voiceout.h"
#include "osc.h"
#include "vroverlay.h"
#include "translate.h"
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
    if (j.is_object()) {
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
            s.lastSeenVersion = j.value("last_seen_version", s.lastSeenVersion);
            s.inDevice = j.value("input_device", s.inDevice);
            s.outDevice = j.value("output_device", s.outDevice);
            s.monDevice = j.value("monitor_device", s.monDevice);
            s.monitor = j.value("monitor", s.monitor);
            s.enabled = j.value("enabled", s.enabled);
            s.activation = j.value("activation", s.activation);
            s.thresholdDb = j.value("threshold_db", s.thresholdDb);
            s.holdMs = j.value("hold_ms", s.holdMs);
            s.maxSeconds = j.value("max_seconds", s.maxSeconds);
            s.pttKey = j.value("ptt_key", s.pttKey);
            s.page = j.value("page", s.page);
            s.emotion = j.value("emotion", s.emotion);
            s.quality = j.value("audio_quality", s.quality);
            s.matchMic = j.value("match_mic", s.matchMic);
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
            }
            s.matchAmount = j.value("match_amount", s.matchAmount);
            s.passthrough = j.value("passthrough", s.passthrough);
            s.chatbox = j.value("voice_chatbox", s.chatbox);
            s.chatboxShow = j.value("voice_chatbox_show", s.chatboxShow);
            s.vs.setCloneStrength = j.value("set_clone_strength", s.vs.setCloneStrength);
            s.vs.cloneStrength = j.value("clone_strength", s.vs.cloneStrength);
            s.emotionStrength = j.value("emotion_strength", s.emotionStrength);
            if (j.contains("usual_voice") && j["usual_voice"].is_object()) {
                const json& b = j["usual_voice"];
                s.baseline.loudDb = b.value("loud_db", s.baseline.loudDb);
                s.baseline.pitch = b.value("pitch", s.baseline.pitch);
                s.baseline.range = b.value("range", s.baseline.range);
                s.baseline.rate = b.value("rate", s.baseline.rate);
                s.baseline.phrases = b.value("phrases", s.baseline.phrases);
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
                ls.tellApart = l.value("tell_speakers_apart", ls.tellApart);
                ls.showOriginal = l.value("show_original", ls.showOriginal);
                ls.onlyOtherLanguages = l.value("only_other_languages", ls.onlyOtherLanguages);
                ls.chatbox = l.value("chatbox", ls.chatbox);
                ls.textScale = l.value("text_scale", ls.textScale);
            }
        } catch (const json::exception&) { /* wrong type somewhere: keep defaults for the rest */ }
    }
    if (s.engine < kVoiceChanger || s.engine > kTranslate) s.engine = kVoiceChanger;
    if (const Language* l = findLanguage(s.spokenLang)) s.spokenLang = l->code; else s.spokenLang.clear();
    if (!findLanguage(s.targetLang)) s.targetLang = "ja";
    if (s.page != kPageListen && s.page != kPageVR) s.page = kPageVoice;
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
    if (s.translateService < kMyMemory || s.translateService > kGemini) s.translateService = kMyMemory;
    if (s.speechLevel < kLevelPolite || s.speechLevel > kLevelVeryCasual) s.speechLevel = kLevelCasual;
    s.aiTimeoutMs = std::max(800, std::min(6000, s.aiTimeoutMs));
    if (s.myGender < kGenderUnknown || s.myGender > kGenderNonbinary) s.myGender = kGenderUnknown;
    if (s.talkingTo < 0 || s.talkingTo >= kTalkCount) s.talkingTo = kTalkAuto;
    s.outVolume = std::max(0.f, std::min(2.f, s.outVolume));
    s.monVolume = std::max(0.f, std::min(2.f, s.monVolume));
    if (s.claudeModel.empty()) s.claudeModel = "claude-haiku-5-5";
    if (s.geminiModel.empty()) s.geminiModel = "gemini-3.5-flash-lite";
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
    if (s.apiKey.empty())
        if (const char* env = std::getenv("ELEVENLABS_API_KEY")) s.apiKey = env;
}

static std::string settingsJson(const Settings& s) {
    json j = {
        {"api_key", s.apiKey}, {"engine", s.engine}, {"voice_id", s.voiceId}, {"sts_model", s.stsModel},
        {"tts_model", s.ttsModel}, {"stability", s.vs.stability}, {"similarity", s.vs.similarity},
        {"style", s.vs.style}, {"speed", s.vs.speed}, {"speaker_boost", s.vs.speakerBoost},
        {"remove_noise", s.removeNoise}, {"language", s.spokenLang}, {"target_language", s.targetLang},
        {"translate_service", s.translateService}, {"deepl_key", s.deeplKey},
        {"claude_key", s.claudeKey}, {"gemini_key", s.geminiKey}, {"claude_model", s.claudeModel},
        {"gemini_model", s.geminiModel}, {"speech_level", s.speechLevel}, {"persona", s.persona},
        {"ai_timeout_ms", s.aiTimeoutMs}, {"mirror", s.mirror}, {"input_device", s.inDevice},
        {"output_device", s.outDevice}, {"monitor_device", s.monDevice}, {"monitor", s.monitor},
        {"enabled", s.enabled}, {"activation", s.activation}, {"threshold_db", s.thresholdDb},
        {"hold_ms", s.holdMs}, {"max_seconds", s.maxSeconds}, {"ptt_key", s.pttKey},
        {"page", s.page},
        {"emotion", s.emotion}, {"emotion_strength", s.emotionStrength},
        {"audio_quality", s.quality}, {"match_mic", s.matchMic}, {"match_amount", s.matchAmount}, {"passthrough", s.passthrough},
        {"voice_chatbox", s.chatbox}, {"voice_chatbox_show", s.chatboxShow},
        {"set_clone_strength", s.vs.setCloneStrength}, {"clone_strength", s.vs.cloneStrength},
        {"dialects", s.variety}, {"custom_dialects", s.customVariety}, {"voice_for_language", s.voiceFor},
        {"my_gender", s.myGender}, {"talking_to", s.talkingTo}, {"talking_to_key", s.talkKey},
        {"voice_per_language", s.voicePerLanguage}, {"output_volume", s.outVolume}, {"monitor_volume", s.monVolume},
        {"app_language", s.uiLanguage}, {"check_updates", s.checkUpdates}, {"last_seen_version", s.lastSeenVersion},
        {"usual_voice", {{"loud_db", s.baseline.loudDb}, {"pitch", s.baseline.pitch}, {"range", s.baseline.range},
                         {"rate", s.baseline.rate}, {"phrases", s.baseline.phrases}}},
    };
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
        };
    }
    const ListenSettings& ls = s.listen;
    j["listen"] = {
        {"source", ls.source}, {"app", ls.app}, {"speakers", ls.speakers}, {"input", ls.input},
        {"from", ls.fromLang}, {"to", ls.toLang}, {"threshold_db", ls.thresholdDb}, {"pace", ls.pace},
        {"tell_speakers_apart", ls.tellApart}, {"show_original", ls.showOriginal},
        {"only_other_languages", ls.onlyOtherLanguages}, {"chatbox", ls.chatbox}, {"text_scale", ls.textScale},
    };
    return j.dump(2);
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
};

struct Sink {  // one playback device fed by a ring buffer
    SpscRing<int16_t> ring{size_t(kDeviceRate) * 120};
    std::atomic<bool> flush{false};
    ma_device dev;
    std::atomic<bool> on{false};
};

// The conversation so far, newest last: what you said and what the others said (from Listen).
// Gives the AI translator its context, and feeds the VR captions.
struct ConvoLine {
    bool me = false;
    std::string original;  // as spoken
    std::string shown;     // translation, or the same as original
    std::string language;  // spoken language name, for the others' lines
    int speaker = -1;      // "Voice 1/2" inside one caption
    int voice = 0;         // Gender guessed from their voice (others only)
    double at = 0;         // glfwGetTime()-like seconds since start, for fading
};

struct App {
    std::mutex mtx;  // guards: cfg, voices, models, catalogMsg, lastError, heardText, sentText, jobs, warmWanted
    Settings cfg;
    std::vector<Voice> voices;
    std::vector<Model> models;
    std::string catalogMsg, lastError, heardText, sentText, delivery;
    VoiceBaseline baseline;  // learned in the worker; copied into the settings for saving
    int catalogVersion = 0;
    std::deque<Phrase> jobs;
    bool warmWanted = false;
    std::condition_variable jobCv;

    std::atomic<bool> running{true}, loading{false}, busy{false}, cancel{false};
    std::atomic<bool> pttDown{false}, recording{false};
    std::atomic<int> listener{0};               // Listener the translation uses (Talking to, resolved)
    std::atomic<float> outGain{1.f}, monGain{1.f};
    std::atomic<int> pttKey{0}, talkKey{0}, talkPresses{0};  // hotkeys, read by the key thread
    std::atomic<bool> binding{false};
    std::atomic<float> levelDb{-100.f};
    std::atomic<int> sttMs{0}, translateMs{0}, voiceMs{0}, totalMs{0};

    ma_context ctx;
    std::vector<ma_device_info> capInfos, playInfos;
    ma_device capDev;
    bool capOn = false;
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
};
static App g;
static Listener gListen;  // the Listen page's own pipeline; runs next to the voice changer
static VrOverlay gVr;     // captions inside the headset (idle unless switched on)
static const auto gStart = std::chrono::steady_clock::now();
static double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count(); }

// mtx held
static void addConvo(ConvoLine line) {
    line.at = nowSeconds();
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
    t.level = s.speechLevel;
    t.persona = s.persona;
    t.timeoutMs = s.aiTimeoutMs;
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

static void playCb(ma_device* d, void* out, const void*, ma_uint32 frames) {
    auto* s = static_cast<Sink*>(d->pUserData);
    if (s->flush.exchange(false)) s->ring.clear();
    s->ring.pop(static_cast<int16_t*>(out), frames);  // miniaudio pre-fills the rest with silence
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

static void openCapture(const std::string& name) {
    if (g.capOn) { ma_device_uninit(&g.capDev); g.capOn = false; }
    ma_device_config c = ma_device_config_init(ma_device_type_capture);
    c.capture.pDeviceID = findDevice(g.capInfos, name);
    c.capture.format = ma_format_f32;
    c.capture.channels = 1;
    c.sampleRate = kDeviceRate;  // miniaudio resamples from the device's native rate
    c.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
    c.dataCallback = captureCb;
    g.match.forgetMic();  // a different mic sounds different
    if (ma_device_init(&g.ctx, &c, &g.capDev) == MA_SUCCESS && ma_device_start(&g.capDev) == MA_SUCCESS) {
        g.capOn = true;
    } else {
        std::lock_guard<std::mutex> lk(g.mtx);
        g.lastError = "Couldn't open the microphone. Pick another one under Input.";
    }
}

static void openSink(Sink& s, const std::string& name, bool wanted) {
    if (s.on.exchange(false)) ma_device_uninit(&s.dev);
    if (!wanted) return;
    ma_device_config c = ma_device_config_init(ma_device_type_playback);
    c.playback.pDeviceID = findDevice(g.playInfos, name);
    c.playback.format = ma_format_s16;
    c.playback.channels = 1;
    c.sampleRate = kDeviceRate;
    c.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;  // cleanest conversion if the device runs at another rate
    c.dataCallback = playCb;
    c.pUserData = &s;
    if (ma_device_init(&g.ctx, &c, &s.dev) == MA_SUCCESS && ma_device_start(&s.dev) == MA_SUCCESS) {
        s.on = true;
    } else {
        std::lock_guard<std::mutex> lk(g.mtx);
        g.lastError = "Couldn't open that output device. Pick another one under Output.";
    }
}

// ---------------------------------------------------------------- pipeline threads

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
    while (g.running) {
        if (!convOk || g.capRing.size() < size_t(kIn)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        g.capRing.pop(in, kIn);

        GateParams p;
        bool enabled, passthrough, matchMic;
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            enabled = g.cfg.enabled;
            passthrough = g.cfg.passthrough;
            matchMic = g.cfg.matchMic;
            p.pushToTalk = g.cfg.activation == kPushToTalk;
            p.thresholdDb = g.cfg.thresholdDb;
            p.holdMs = g.cfg.holdMs;
            p.maxSeconds = g.cfg.maxSeconds;
        }
        p.pttDown = g.pttDown;

        // Your real voice to the output, kept short so it stays in sync (drops 10 ms when the two
        // devices' clocks have drifted 150 ms apart).
        const bool passing = !enabled && passthrough && g.out.on;
        for (int i = 0; i < kIn; ++i) pass[i] = int16_t(std::max(-1.f, std::min(1.f, in[i])) * 32767.f);
        fullQ.insert(fullQ.end(), pass, pass + kIn);
        if (passing) {
            if (g.out.ring.size() < size_t(kDeviceRate) * 15 / 100) g.out.ring.push(pass, kIn);
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
            const bool done = seg.feed(low.data(), p, phrase.pcm, fullBlock, kFullPerBlock, &phrase.full);
            low.erase(low.begin(), low.begin() + Segmenter::kBlock);
            g.levelDb = seg.levelDb();
            if (!enabled) {
                seg.reset();
                g.recording = false;
                wasOpen = false;
                speaking = speaking || g.levelDb > p.thresholdDb;
                continue;
            }
            const bool open = seg.open();
            speaking = speaking || open;
            g.recording = open;
            if (done || (open && !wasOpen)) {
                std::lock_guard<std::mutex> lk(g.mtx);
                if (done) g.jobs.push_back(std::move(phrase));
                else g.warmWanted = true;  // speech just started: connect while the user is still talking
                g.jobCv.notify_one();
            }
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

// Phrases -> ElevenLabs -> playback rings. One at a time so the audio stays in order.
static void workerLoop() {
    VoiceOut voiceOut(kDeviceRate);  // ElevenLabs audio (PCM or MP3, any rate) -> playback rate
    Eleven api;             // both keep their connection open between phrases
    Translator translator;
    Eleven captionApi;      // Changer mode + chatbox: transcribes alongside the voice conversion
    api.http.cancel = &g.cancel;
    translator.http.cancel = &g.cancel;
    captionApi.http.cancel = &g.cancel;

    while (g.running) {
        std::vector<int16_t> pcm, full;
        Settings s;
        bool haveJob = false, professional = false;
        {
            std::unique_lock<std::mutex> lk(g.mtx);
            g.jobCv.wait(lk, [] { return !g.jobs.empty() || g.warmWanted || !g.running; });
            if (!g.running) break;
            s = g.cfg;
            g.warmWanted = false;
            if (!g.jobs.empty()) {
                pcm = std::move(g.jobs.front().pcm);
                full = std::move(g.jobs.front().full);
                g.jobs.pop_front();
                haveJob = true;
            }
            for (const Voice& v : g.voices) professional = professional || (v.id == s.voiceId && v.category == "professional");
        }
        g.cancel = false;
        if (!g.running) break;  // quitting: don't start anything that shutdown could no longer cancel
        api.apiKey = captionApi.apiKey = s.apiKey;
        if (!haveJob) {
            api.warm();
            if (s.engine == kTranslate) translator.warm(translateSetup(s));
            if (s.engine == kVoiceChanger && s.chatbox) captionApi.warm();
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
        bool ok = true;
        if (s.voiceId.empty()) {
            ok = false;
            err = "Pick a voice first";
        } else if (s.engine == kVoiceChanger) {
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
            const bool emotion = s.emotion;
            const bool tags = modelTakesTags(s.ttsModel);
            VoiceTrack track;
            std::vector<HeardToken> tokens;
            // Measured on this computer while the upload and transcription run, so it adds no wait.
            std::future<VoiceTrack> measuring;
            if (emotion) measuring = std::async(std::launch::async, [&pcm] { return trackVoice(pcm); });
            ok = api.transcribe(pcm, s.spokenLang, heard, detected, err, emotion ? &tokens : nullptr);
            if (emotion) track = measuring.get();
            sttMs = msSince(t0);
            const bool blank = heard.find_first_not_of(" \t\r\n") == std::string::npos;

            Direction dir;
            if (ok && !blank && emotion) {
                VoiceBaseline base;
                {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    base = g.baseline;
                }
                dir = directSpeech(track, tokens, base, s.emotionStrength, tags);
                std::lock_guard<std::mutex> lk(g.mtx);
                g.baseline = base;
                delivery = dir.summary;
                if (!tags) delivery += " (this model takes no tags or capitals, so only punctuation is used)";
            }

            say = heard;
            const Language* to = findLanguage(s.targetLang);
            const Language* from = findLanguage(s.spokenLang.empty() ? detected : s.spokenLang);
            const bool translating = s.engine == kTranslate && to && from != to;
            if (ok && !blank && s.engine == kTranslate && !to) {
                ok = false;
                err = "Pick a language to translate to";
            } else if (ok && !blank && translating) {
                const auto tTr = Clock::now();
                TranslateSetup setup = translateSetup(s);
                if (s.mirror) {
                    std::lock_guard<std::mutex> lk(g.mtx);
                    setup.context = recentConversation(6);
                }
                const bool ai = s.translateService == kClaude || s.translateService == kGemini;
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
                    ok = translator.translate(setup, heard, from, *to, say, err);
                    if (ok) {
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
            if (ok && !blank) {
                tVoice = Clock::now();
                ok = speak([&](const std::string& format, std::string& e) {
                    return api.textToSpeech(s.voiceId, s.ttsModel, s.vs, say, sayLang, sink, e, format);
                }, err);
            }
        }

        // VRChat chatbox: what you said, what the voice said, or both.
        if (ok && s.chatbox) {
            const std::string said = withoutTags(heard), voiced = withoutTags(spoken.empty() ? heard : spoken);
            std::string text;
            if (s.chatboxShow == kShowSaid) text = said;
            else if (s.chatboxShow == kShowSpoken) text = voiced;
            else text = (said == voiced || voiced.empty()) ? said : said + "\n" + voiced;
            if (!text.empty()) {
                std::lock_guard<std::mutex> lk(g.mtx);
                g.chatText = text;
            }
        }
        {
            std::lock_guard<std::mutex> lk(g.mtx);
            if (!heard.empty()) g.heardText = heard;
            g.sentText = spoken;
            if (ok && !heard.empty()) {
                ConvoLine me;
                me.me = true;
                me.original = heard;
                me.shown = withoutTags(spoken.empty() ? heard : spoken);
                addConvo(std::move(me));
            }
            if (!delivery.empty()) g.delivery = delivery;
            if (ok) g.lastError.clear();
            else if (err != "cancelled") g.lastError = err;
        }
        if (totalMs > 0) {
            g.sttMs = sttMs;
            g.translateMs = translateMs;
            g.voiceMs = voiceMs;
            g.totalMs = totalMs;
        }
        g.busy = false;
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
static std::vector<PanelLine> vrLines(const VrSettings& v) {
    std::vector<PanelLine> out;
    const size_t from = g.convo.size() > 12 ? g.convo.size() - 12 : 0;
    for (size_t i = from; i < g.convo.size(); ++i) {
        const ConvoLine& c = g.convo[i];
        if (c.me && !v.showMe) continue;
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

// ---------------------------------------------------------------- main

int main() {
    UiModel ui;  // the UI thread's copy of everything; ui.cfg is published to g.cfg every frame
    loadSettings(ui.cfg);
    setUiLanguage(ui.cfg.uiLanguage);
    ui.settingsPath = settingsFile().u8string();
    ui.settingsPlace = gPlace;
    // After an update: show what changed since the version this person last used.
    if (gSettingsExisted && compareVersions(CIMA_FULL_VERSION, ui.cfg.lastSeenVersion.empty() ? "0" : ui.cfg.lastSeenVersion) > 0) {
        ui.showChanges = 1;
        ui.changesSince = ui.cfg.lastSeenVersion.empty() ? "2.5.99" : ui.cfg.lastSeenVersion;  // from before 2.6: just this one
    }
    ui.cfg.lastSeenVersion = CIMA_FULL_VERSION;
    double lastAutosave = 0;
    Updater updater(CIMA_FULL_VERSION);
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
    openCapture(ui.cfg.inDevice);
    openSink(g.out, ui.cfg.outDevice, true);
    openSink(g.mon, ui.cfg.monDevice, ui.cfg.monitor);

    std::thread capThread(captureLoop), workThread(workerLoop);
    std::thread keyThread([] {  // global hotkeys (Windows): checked every 8 ms whatever the window is doing
        bool talkWas = false;
        while (g.running) {
            if (kGlobalHotkey) {
                const bool binding = g.binding;
                g.pttDown = !binding && keyDown(g.pttKey);
                const bool t = !binding && keyDown(g.talkKey);
                if (t && !talkWas) ++g.talkPresses;
                talkWas = t;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    });
    gListen.start();
    gVr.setThumbnail(kLogos[0].rgba, kLogos[0].size);  // the SteamVR menu tab's icon
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
    int winW = int(1120 * scale), winH = int(800 * scale), minW = int(940 * scale), minH = int(600 * scale);
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
    ImGui::GetIO().IniFilename = nullptr;
    uiSetup(scale);
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);
    setupLogo(win, ui, scale);

    int catalogVersion = -1;
    uint64_t captionVersion = 0;
    std::set<uint64_t> convoIds;  // Listen captions already added to the conversation
    bool talkWasDown = false;
    std::string voiceKey, voiceKeyVoice;  // language/dialect the voice was last used with
    uint64_t vrSentConvo = ~0ull, vrVersion = 0;  // what the headset panel was last given
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
        ui.levelDb = g.levelDb;
        ui.sttMs = g.sttMs;
        ui.translateMs = g.translateMs;
        ui.voiceMs = g.voiceMs;
        ui.totalMs = g.totalMs;
        // Hotkeys. On Windows a small thread watches them, so push-to-talk stays instant while this
        // window is in the background (it then only redraws 20 times a second).
        g.pttKey = ui.cfg.pttKey;
        g.talkKey = ui.cfg.talkKey;
        g.binding = ui.binding;
        if (!kGlobalHotkey) {
            g.pttDown = !ui.binding && keyDown(ui.cfg.pttKey);
            const bool t = !ui.binding && keyDown(ui.cfg.talkKey);
            if (t && !talkWasDown) ++g.talkPresses;
            talkWasDown = t;
        }
        for (int n = g.talkPresses.exchange(0); n > 0; --n) ui.cfg.talkingTo = (ui.cfg.talkingTo + 1) % kTalkCount;
        if (const uint64_t v = gListen.version(); v != captionVersion) {
            captionVersion = v;
            gListen.captions(ui.captions);
            // Finished captions join the conversation (AI context, VR captions), each once.
            std::lock_guard<std::mutex> lk(g.mtx);
            for (const Caption& c : ui.captions) {
                if (c.pending || !convoIds.insert(c.id).second) continue;
                for (const CaptionLine& l : c.lines) {
                    ConvoLine them;
                    them.original = l.original.empty() ? l.text : l.original;
                    them.shown = l.text;
                    them.language = c.language;
                    them.speaker = l.speaker;
                    them.voice = l.voice;
                    addConvo(std::move(them));
                }
            }
            if (convoIds.size() > 500) convoIds.clear();  // old ids are long gone from the feed
        }
        ui.listen = gListen.state();

        // ---- who you're talking to: chosen, or Auto = the last person who spoke, by their voice
        {
            int vrChoice;
            if (gVr.takeTalkingTo(vrChoice)) ui.cfg.talkingTo = vrChoice;  // picked in the SteamVR dashboard
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
            VrTalking vt;
            vt.choice = ui.cfg.talkingTo;
            vt.title = T("CIMA Box: who are you talking to?");
            for (const char* b : {"Auto", "Don't know", "A guy", "A girl", "A group"}) vt.buttons.push_back(T(b));
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
            gVr.setTalking(vt);
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
            if (ui.cfg.vr.on && (convoNow != vrSentConvo || ui.cfg.vr.showMe != vrSentShowMe)) {
                gVr.setLines(vrLines(ui.cfg.vr), ++vrVersion);
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
                    lines = vrLines(v);
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

        if (act.rescanDevices) refreshDeviceList(ui);
        if (act.reopenInput) openCapture(ui.cfg.inDevice);
        if (act.reopenOutput) openSink(g.out, ui.cfg.outDevice, true);
        if (act.reopenMonitor) openSink(g.mon, ui.cfg.monDevice, ui.cfg.monitor);
        if (act.loadCatalog) refreshCatalog(ui.cfg.apiKey);
        if (act.stopSpeaking) stopSpeaking();
        if (act.refreshApps) ui.apps = listApps();
        if (act.clearCaptions) gListen.clear();
        if (act.setCaptionsOnly >= 0 && (act.setCaptionsOnly == 1) != ui.captionsOnly) {
            ui.captionsOnly = act.setCaptionsOnly == 1;
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
            if (ui.cfg.voicePerLanguage && ui.cfg.engine == kTranslate && !voiceKey.empty() && key != voiceKey) {
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
            ui.update = updater.state();
            if (updater.restartWanted()) glfwSetWindowShouldClose(win, GLFW_TRUE);  // relaunched after cleanup
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
    capThread.join();
    workThread.join();
    keyThread.join();
    gVr.stop();
    gListen.stop();
    if (g.capOn) ma_device_uninit(&g.capDev);
    openSink(g.out, "", false);
    openSink(g.mon, "", false);
    ma_context_uninit(&g.ctx);

    if (gLogoTexture) glDeleteTextures(1, &gLogoTexture);
    if (previewTex) glDeleteTextures(1, &previewTex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    if (updater.restartWanted()) Updater::relaunch();  // the new version, now that this one let go of everything
    return 0;
}
