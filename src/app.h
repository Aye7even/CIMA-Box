// Settings and the data the interface shows. Shared by main.cpp and ui.cpp.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "appaudio.h"
#include "eleven.h"
#include "listen.h"
#include "servicestatus.h"
#include "voiceout.h"
#include "vroverlay.h"
#include "translate.h"
#include "updater.h"

enum Engine { kVoiceChanger = 0, kRespeak = 1, kTranslate = 2 };
enum Activation { kVoiceActivity = 0, kPushToTalk = 1 };
enum Page { kPageVoice = 0, kPageListen = 1, kPageVR = 2, kPageSettings = 3, kPageText = 4 };
// Who you are talking to, as chosen. Auto = the last person who spoke (guessed from their voice).
enum TalkingTo { kTalkAuto = 0, kTalkUnknown = 1, kTalkMan = 2, kTalkWoman = 3, kTalkGroup = 4, kTalkCount = 5 };
// Where cimabox.json is kept (Settings > About).
enum SettingsPlace { kPlaceSettingsFolder = 0, kPlaceDocuments = 1, kPlaceProgram = 2 };
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
    bool chatbox = false;              // My voice and Text: show what you say in VRChat's chatbox
    bool textTranslate = true;         // Text page: translate what you type (else read it as typed)
    bool textSpeak = true;
    bool setupDone = false;            // went through (or skipped) the setup walkthrough
    int setupAt = 0, setupUse = 0, setupRoute = -1, setupAi = 1;  // where the walkthrough was (it continues after a restart)
    // Spending limit, per session (from when CIMA Box starts).
    bool spendWarn = true;
    int spendCredits = 20000;          // ElevenLabs credits
    float spendDollars = 1.0f;         // Claude and Gemini together, US$
    bool spendStop = false;            // also switch off talking and listening when reached             // Text page: say it with the voice (else only the chatbox and the feed)
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
    std::string lastSeenVersion;       // "What's new" shows what changed since this one
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

// One line of the conversation on the Text page: yours (spoken or typed) or someone else's (Listen).
struct ChatLine {
    bool me = false, typed = false;
    std::string text;      // translated, or as said
    std::string original;  // as said, when it differs
    std::string language;  // theirs
    int speaker = -1;      // "Voice 1/2"
};

// The "how do I get this key?" guides, and their pictures (in this order in guide_data.h).
enum KeyGuide { kGuideEleven = 0, kGuideDeepL, kGuideGemini, kGuideClaude, kGuideCount };
enum GuidePicture { kPicEleven1 = 0, kPicEleven2, kPicEleven3, kPicDeepL, kPicGemini, kPicClaude, kPicCount };
struct GuidePic {
    uint64_t texture = 0;
    int width = 0, height = 0;
};
enum KeyCheck { kCheckNone = 0, kChecking, kCheckGood, kCheckBad };

// One entry of the small usage line at the bottom of the window.
enum UsageLevel { kUsageOk = 0, kUsageLow, kUsageOut, kUsageUnknown };
struct UsageItem {
    std::string name;    // "ElevenLabs"
    std::string text;    // "12.4k credits left"
    std::string detail;  // the tooltip
    int level = kUsageOk;
};

// The setup walkthrough. What the person picked, so later steps can follow from it.
enum SetupUse { kUseVoice = 1, kUseText = 2, kUseRead = 4 };
enum SetupRoute { kRouteNone = -1, kRouteMe = 0, kRouteApps = 1, kRouteOwn = 2 };

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
    std::string settingsPath;                // where cimabox.json is saved
    int settingsPlace = kPlaceSettingsFolder;  // SettingsPlace
    std::string settingsError;               // moving the settings didn't work
    int showChanges = 0;                     // 1: open "What's new" (since changesSince), 2: the whole history
    std::string changesSince;                // versions newer than this
    std::vector<ChatLine> chat;              // Text page, oldest first
    std::vector<ServiceStatus> services;     // ElevenLabs, Claude, Gemini, DeepL status pages
    std::vector<UsageItem> usage;            // what's left, for the services in use
    std::string logPath;                     // cimabox.log
    long long sessionCredits = -1;           // ElevenLabs credits used since start (-1 = can't tell)
    double sessionDollars = 0;               // Claude + Gemini since start
    std::string spendAlert;                  // the spending limit was reached: what to say
    bool spendStopped = false;               // ... and talking and listening were switched off
    bool scrollToSpend = false;              // Settings: show the spending limit part
    int setupStep = -1;                      // the walkthrough's screen, -1 = not showing
    int setupUse = 0;                        // SetupUse bits
    int setupRoute = kRouteNone;
    int setupAi = kGuideGemini;              // the AI picked in setup (kGuideGemini or kGuideClaude)
    GuidePic guidePics[kPicCount];
    int bigPicture = -1;                     // a guide picture shown large
    int guideOpen = -1;                      // Settings: a key guide shown in a window (KeyGuide)
    int keyCheck[kGuideCount] = {};          // KeyCheck, for DeepL / Gemini / Claude ("Check key")
    std::string keyCheckMsg[kGuideCount];

    // interface-only state
    std::string voiceFilter;
    bool binding = false, bindArmed = false, showKey = false;
    int bindingWhich = 0;              // 0 = push-to-talk key, 1 = "talking to" key
    float meterDb = -100.f, listenMeterDb = -100.f;
    std::string textDraft;             // Text page: what's being typed
};

struct UiActions {
    bool loadCatalog = false, reopenInput = false, reopenOutput = false, reopenMonitor = false;
    bool rescanDevices = false, stopSpeaking = false, save = false;
    bool resetBaseline = false, resetMatch = false, placeVrHere = false;
    bool refreshApps = false, clearCaptions = false;
    int setCaptionsOnly = -1;  // 0 / 1 when the user switches the compact view
    bool checkUpdates = false, installUpdate = false, openSettingsFolder = false;
    int setSettingsPlace = -1;  // SettingsPlace picked in Settings > About
    std::string sendText;       // Text page: say this
    std::string testSound;      // setup: say this once, nowhere but the output
    bool openLog = false;
    int checkKey = -1;          // KeyGuide whose key to try (DeepL, Gemini, Claude)
    bool spendOk = false;       // the spending notice was read: count the next limit from now
    std::string openUrl;        // a status page
};
