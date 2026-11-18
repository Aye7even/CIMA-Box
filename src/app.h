// Settings and the data the interface shows. Shared by main.cpp and ui.cpp.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "appaudio.h"
#include "eleven.h"
#include "listen.h"
#include "voiceout.h"
#include "vroverlay.h"
#include "translate.h"
#include "updater.h"

enum Engine { kVoiceChanger = 0, kRespeak = 1, kTranslate = 2 };
enum Activation { kVoiceActivity = 0, kPushToTalk = 1 };
enum Page { kPageVoice = 0, kPageListen = 1, kPageVR = 2, kPageSettings = 3 };
// Who you are talking to, as chosen. Auto = the last person who spoke (guessed from their voice).
enum TalkingTo { kTalkAuto = 0, kTalkUnknown = 1, kTalkMan = 2, kTalkWoman = 3, kTalkGroup = 4, kTalkCount = 5 };
enum ChatboxShow { kShowSaid = 0, kShowSpoken = 1, kShowBoth = 2 };

struct Settings {
    std::string apiKey;
    int page = kPageVoice;
    int engine = kVoiceChanger;
    std::string voiceId;
    std::string stsModel = "eleven_multilingual_sts_v2";
    std::string ttsModel = "eleven_v4_turbo";
    VoiceSettings vs;
    bool removeNoise = false;
    std::string spokenLang;            // ISO 639-1, empty = detect automatically
    std::string targetLang = "ja";     // ISO 639-1
    bool emotion = false;              // Re-speak / Translate: carry how you said it (audio tags, punctuation)
    int emotionStrength = kEmotionSubtle;
    VoiceBaseline baseline;            // your usual voice, learned while emotion is on
    int quality = kQualityHigh;        // OutputQuality: what ElevenLabs sends back, and what Changer uploads
    bool matchMic = false;             // give the new voice the tone and level of your actual mic
    float matchAmount = 0.8f;
    bool passthrough = false;          // voice changer off: send your own mic to the output instead
    bool chatbox = false;              // My voice: show what you say in VRChat's chatbox
    int chatboxShow = 0;               // ChatboxShow
    int translateService = 0;          // TranslateService, shared by both pages
    std::string deeplKey;
    std::string claudeKey, geminiKey;  // AI translation (natural, slang-aware)
    std::string claudeModel = "claude-haiku-5-5";
    std::string geminiModel = "gemini-3.5-flash-lite";
    int speechLevel = kLevelCasual;    // SpeechLevel: how the AI words what you say
    std::string persona;               // optional: "a guy in his 20s from Seoul"
    int aiTimeoutMs = 2000;            // AI slower than this: that phrase uses DeepL / Free instead
    bool mirror = true;                // match how the people you talk to speak (from the Listen captions)
    std::map<std::string, std::string> variety;        // language code -> Variety id, or "custom"
    std::map<std::string, std::string> customVariety;  // language code -> typed dialect ("ar-KW", "Rioplatense")
    int myGender = kGenderUnknown;     // Gender: words about yourself in gendered languages
    int talkingTo = kTalkAuto;         // TalkingTo
    int talkKey = 0;                   // hotkey: next "talking to" choice
    bool voicePerLanguage = true;      // remember the voice used with each language / dialect
    std::map<std::string, std::string> voiceFor;       // "ar/ar-KW" -> voice id
    float outVolume = 1.f, monVolume = 1.f;            // new voice: to the output, and in your headphones
    std::string uiLanguage = "en";     // the app's own language
    bool checkUpdates = true;          // look for a new version when the app starts
    std::string inDevice, outDevice, monDevice;  // empty = system default
    bool monitor = false;
    bool enabled = true;
    int activation = kVoiceActivity;
    float thresholdDb = -40.f;
    int holdMs = 350;
    int maxSeconds = 12;
    int pttKey = 0;
    ListenSettings listen;             // the Listen page: captions for what other people say
    VrSettings vr;                     // the VR page: captions inside the headset
};

// The translator settings for one language and dialect (the dialect part only).
void applyVariety(const Settings& s, const std::string& langCode, TranslateSetup& t);

struct UiModel {
    Settings cfg;  // edited in place by the interface

    // filled in by main.cpp every frame
    std::vector<Voice> voices;
    std::vector<Model> models;
    std::vector<std::string> inputDevices, outputDevices;
    std::string catalogMsg, lastError, heardText, sentText;
    std::string delivery;              // how the last phrase sounded, when emotion is on
    std::string qualityNote;
    std::string translateVia;          // which service translated the last phrase           // set when the plan refused a quality and a lower one is used
    float matchProgress = 0;           // 0..1: how much of your mic and the new voice Match my mic has heard
    bool passing = false;              // your own mic is going to the output right now
    bool loading = false, recording = false, busy = false;
    int queued = 0;
    float levelDb = -100.f;
    int sttMs = 0, translateMs = 0, voiceMs = 0, totalMs = 0;  // last phrase; 0 = stage not used

    std::vector<AppInfo> apps;        // programs with a window, for the Listen source picker
    std::vector<Caption> captions;    // oldest first
    ListenState listen;
    uint64_t logoTexture = 0;         // the app picture, 0 = not loaded
    float logoSize = 0;               // pixels
    bool captionsOnly = false;        // compact always-on-top view
    VrState vr;                       // SteamVR overlay status
    bool vrSupported = false;
    uint64_t previewTexture = 0;      // VR page: the panel as it looks in the headset
    int previewW = 0, previewH = 0;
    bool previewEmpty = true;
    int talkingResolved = kListenerUnknown;  // Addressee the translation uses right now
    bool talkingGuessed = false;             // ...guessed from the last speaker's voice (Auto)
    bool firstRun = false;                   // no ElevenLabs key yet: show the getting-started steps
    UpdateState update;                      // GitHub updates
    bool updatesConfigured = false;          // this build knows where updates come from

    // interface-only state
    std::string voiceFilter;
    bool binding = false, bindArmed = false, showKey = false;
    int bindingWhich = 0;              // 0 = push-to-talk key, 1 = "talking to" key
    float meterDb = -100.f, listenMeterDb = -100.f;
};

struct UiActions {
    bool loadCatalog = false, reopenInput = false, reopenOutput = false, reopenMonitor = false;
    bool rescanDevices = false, stopSpeaking = false, save = false;
    bool resetBaseline = false, resetMatch = false, placeVrHere = false;
    bool refreshApps = false, clearCaptions = false;
    int setCaptionsOnly = -1;  // 0 / 1 when the user switches the compact view
    bool checkUpdates = false, installUpdate = false;
};
