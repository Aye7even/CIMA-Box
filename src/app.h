// Settings and the data the interface shows. Shared by main.cpp and ui.cpp.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "appaudio.h"
#include "eleven.h"
#include "extras.h"
#include "listen.h"
#include "servicestatus.h"
#include "voiceout.h"
#include "vroverlay.h"
#include "translate.h"
#include "localai.h"
#include "soundid.h"
#include "voiceid.h"
#include "pcspecs.h"
#include "updater.h"

enum Engine { kVoiceChanger = 0, kRespeak = 1, kTranslate = 2 };
enum Activation { kVoiceActivity = 0, kPushToTalk = 1 };
enum Page { kPageVoice = 0, kPageListen = 1, kPageVR = 2, kPageSettings = 3, kPageText = 4, kPageFeatures = 5, kPageHome = 6 };
// Who you are talking to, as chosen. Auto = the last person who spoke (guessed from their voice).
enum TalkingTo { kTalkAuto = 0, kTalkUnknown = 1, kTalkMan = 2, kTalkWoman = 3, kTalkGroup = 4, kTalkCount = 5 };
// Where cimabox.json is kept (Settings > About).
enum SettingsPlace { kPlaceSettingsFolder = 0, kPlaceDocuments = 1, kPlaceProgram = 2 };
enum ChatboxShow { kShowSaid = 0, kShowSpoken = 1, kShowBoth = 2 };

// The one-time notice about costs. Raise it when the notice says something new: everyone sees it once more.
const int kUsageNotice = 1;

// How the app looks (Settings > Look). The defaults are the original look; the dog logo can't be changed.
struct LookSettings {
    float textScale = 1.f;          // 0.85 .. 1.3
    int preset = 0;                 // kLookDefault... ; -1 = colors picked by hand
    uint32_t accent = 0xE8B05E, background = 0x14171E, panel = 0x1C212B, text = 0xDDE2EB;
    std::string picture;            // file name of the background picture in the settings folder ("" = none)
    float pictureShow = 0.35f;      // how strongly the picture shows through, 0.1 .. 0.8
};
enum LookPreset { kLookDefault = 0, kLookMidnight = 1, kLookForest = 2, kLookRose = 3, kLookCount = 4 };
void lookPreset(LookSettings& l, int preset);  // sets the four colors of a preset

struct Settings {
    std::string apiKey;
    int page = kPageHome;
    std::vector<std::string> homeTiles;
    std::vector<std::string> recentTargets;  // languages you had people hear, newest first (SteamVR menu)  // the quick toggles on Home, in order (empty = the usual ones)
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
    float spendDollars = 1.0f;         // Claude, Gemini and OpenAI together, US$
    bool spendStop = false;            // also switch off talking and listening when reached             // Text page: say it with the voice (else only the chatbox and the feed)
    int chatboxShow = 0;               // ChatboxShow
    int translateService = 0;          // TranslateService, shared by both pages
    std::string deeplKey;
    std::string claudeKey, geminiKey, openaiKey;  // AI translation (natural, slang-aware)
    std::string msKey, msRegion;  // Microsoft Translator: an optional free backup (2 million characters a month)
    bool aiBackup = true;         // with keys for two AIs, the other one covers lines the chosen one misses
    std::string claudeModel = "claude-haiku-5-5";
    std::string geminiModel = "gemini-3.5-flash-lite";
    std::string openaiModel = "gpt-6-luna";
    int speechLevel = kLevelCasual;    // SpeechLevel: how the AI words what you say
    std::string customStyle;           // kLevelCustom: how to sound, in the person's own words
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
    std::string localModel;            // translation service Local: the model to use (LocalModel::id)
    bool localOnDecline = false;       // a line the online AI declines or is too slow for goes to the local model first (keeps it loaded)
    bool aiStream = true;              // read the AI's answer as it arrives: one that has begun by the time limit may finish
    bool aiPriority = false;           // Gemini and OpenAI: their faster service tier (costs more per token)
    bool shortToPlain = false;         // lines of one to three words go straight to DeepL or Microsoft (no dialect, no speech level)
    bool rememberLines = false;        // a short line the AI already translated the same way is answered from memory
    bool speakSentences = false;       // several sentences without a pause: each is said as soon as it's finished
    bool earlyTranslate = false;       // translate during the pause that ends a phrase; used only if it really was the end
    bool showMeaning = false;          // under "They heard": what the translated line says, back in your language
    bool localGpu = true;              // ...on the graphics card (off: the processor only)
    bool localNvidia = false;          // ...with NVIDIA's own engine (an extra download; goes back to the standard one if it can't be used)
    std::string skipUpdate;            // went back from this version: don't offer it again (a newer one, yes)
    int usageNotice = 0;               // which "your keys, your costs" notice was read (kUsageNotice = the current one)
    std::string inDevice, outDevice, monDevice;  // empty = system default
    bool monitor = false;
    bool enabled = true;
    int activation = kVoiceActivity;
    bool pttRealVoice = false;         // push to talk: while the key is up, your own mic goes out (off = silence)
    bool pttTap = false;               // push to talk: tap to start, tap again to stop (instead of holding)
    float thresholdDb = -40.f;
    int holdMs = 350;
    int sentenceWaitMs = 900;          // a pause mid-sentence: wait this long for the rest (0 = off)
    bool liveStt = false;              // live transcription: your voice goes to ElevenLabs while you talk
    int maxSeconds = 12;
    int pttKey = 0;
    ListenSettings listen;             // the Listen page: captions for what other people say
    VrSettings vr;                     // the VR page: captions inside the headset
    LookSettings look;
    FeatureSettings fx;                // the Features page
    ScreenSettings screen;             // screen translation (Features page)
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
    int voiceId = -1;      // the same voice across captions (a best guess), -1 = unknown
    uint64_t key = 0;      // stays the same for this line (romanization is kept per line)
    std::string roman;     // learning mode: how to read it (romaji, pinyin...), when fetched
    bool screen = false;   // read from the screen (screen translation)
    std::string code;      // their language's code, for replaying it ("" = unknown)
};

// Screen translation: one line found in the picture, and its translation.
struct ScreenLine {
    std::string text, translated;
};
// What the screen translation preview shows.
struct ScreenShot {
    uint64_t texture = 0;      // the picture
    int width = 0, height = 0;
    bool busy = false;         // reading / translating
    std::string error;
    std::string app;           // the window it came from
    std::vector<ScreenLine> lines;
    int tokens = 0;            // AI reading: about how many tokens the picture costs
    float crop[4] = {0, 0, 1, 1};
    bool show = false;         // the preview window is open
};

// The "how do I get this key?" guides, and their pictures (in this order in guide_data.h).
enum KeyGuide { kGuideEleven = 0, kGuideDeepL, kGuideGemini, kGuideClaude, kGuideOpenAI, kGuideCount };
constexpr int kSetupLocal = 100;  // setupAi: no key, a model on this PC instead
enum GuidePicture {
    kPicEleven1 = 0, kPicEleven2, kPicEleven3, kPicDeepL, kPicGemini, kPicClaude,
    kPicOpenAI1, kPicOpenAI2, kPicOpenAI3, kPicOpenAI4, kPicOpenAI5, kPicCount
};
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

// One line of "Check my setup". Texts are English (translated when shown), with up to two
// values put in for %s (device names, sample rates).
enum CheckState { kCheckWaiting = 0, kCheckNow = 1, kCheckOk = 2, kCheckProblem = 3, kCheckNote = 4 };
struct CheckItem {
    std::string title, detail, arg1, arg2;
    int state = kCheckWaiting;
    std::string fix, fixJump;   // a button: the setting to go to ("sound" = Windows Sound settings)
};

struct UiModel {
    // A short notice when a hotkey (or the SteamVR menu) changed something, shown for a moment at
    // the bottom of whichever window is up: "Talking to: A girl", "Stopped speaking".
    std::string notice, noticeArg;     // English text (with %s for the argument), argument translated too
    int noticeSerial = 0;              // goes up with every new notice
    // Voice preview: the voice's free sample, in your headphones
    std::string previewWanted;         // set by the UI (a voice id), taken by the app
    std::string previewing;            // the voice playing (or loading) right now
    // Check my setup
    bool checkOpen = false, checkRunning = false;
    std::string checkPrompt;           // what to do right now ("Say something"), English
    std::vector<CheckItem> check;
    Settings cfg;  // edited in place by the interface

    // filled in by main.cpp every frame
    std::vector<Voice> voices;
    std::vector<Model> models;
    std::vector<std::string> inputDevices, outputDevices;
    std::string catalogMsg, lastError, heardText, sentText;
    std::string meaningText;  // what sentText says, back in your language (empty until the check answers)
    std::string delivery;              // how the last phrase sounded, when emotion is on
    std::string qualityNote;
    std::string translateVia;          // which service translated the last phrase           // set when the plan refused a quality and a lower one is used
    float matchProgress = 0;           // 0..1: how much of your mic and the new voice Match my mic has heard
    bool passing = false;              // your own mic is going to the output right now
    bool loading = false, recording = false, busy = false;
    bool holding = false;              // a sentence was cut off by a pause: waiting a moment for the rest
    int queued = 0;
    float levelDb = -100.f;
    int sttMs = 0, translateMs = 0, voiceMs = 0, totalMs = 0;  // last phrase; 0 = stage not used
    bool earlyUsed = false;            // ...and it was translated ahead of time (early translation)
    bool sttLive = false;              // the last phrase's words came from live transcription

    std::vector<AppInfo> apps;        // programs with a window, for the Listen source picker
    std::vector<Caption> captions;    // oldest first
    ListenState listen;
    uint64_t logoTexture = 0;         // the app picture, 0 = not loaded
    float logoSize = 0;               // pixels
    uint64_t pictureTexture = 0;      // Settings > Look: the background picture, 0 = none
    float pictureW = 0, pictureH = 0;
    std::string pictureMsg;           // why a picture couldn't be used
    bool captionsOnly = false;        // compact always-on-top view
    bool mini = false;                // the mini window: My voice at a glance, always on top
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
    bool olderOpen = false;                  // Settings: the list of earlier versions is showing
    PcSpecs pc;                              // this PC's graphics card and memory, to rate the local models
    SoundIdState soundPack;                  // the laugh and cough recogniser: downloaded? downloading?
    VoiceIdState voicePack;                  // the voice fingerprint pack: downloaded? downloading?
    bool voicePackAsk = false;               // "Delete it?" is showing
    LocalState local;                        // models on this PC: what's downloaded, downloading, loaded
    std::string localDeletePick;             // the model picked to delete, waiting for the second press
    std::string localView;                   // the local model showing in the Model dropdown
    std::string olderPick;                   // the earlier version picked, waiting for "Go back"
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
    double sessionDollars = 0;               // Claude + Gemini + OpenAI since start
    std::string spendAlert;                  // the spending limit was reached: what to say
    bool spendStopped = false;               // ... and talking and listening were switched off
    bool scrollToSpend = false;              // Settings: show the spending limit part
    bool showUsageNotice = false;            // Settings > About: read the notice about costs again
    bool findOpen = false;                   // the "Find a setting" window
    bool findKeep = false;                   // keep the last search when it opens (pictures for the README)
    std::string findQuery;
    int setupStep = -1;                      // the walkthrough's screen, -1 = not showing
    int setupUse = 0;                        // SetupUse bits
    int setupRoute = kRouteNone;
    int setupAi = kGuideGemini;              // the AI picked in setup (kGuideGemini, kGuideClaude or kGuideOpenAI)
    GuidePic guidePics[kPicCount];
    int bigPicture = -1;                     // a guide picture shown large
    int guideOpen = -1;                      // Settings: a key guide shown in a window (KeyGuide)
    int keyCheck[kGuideCount] = {};          // KeyCheck, for DeepL / Gemini / Claude / OpenAI ("Check key")
    std::string keyCheckMsg[kGuideCount];

    // interface-only state
    std::string voiceFilter;
    bool binding = false, bindArmed = false, showKey = false;
    int bindingWhich = 0;              // 0 = push-to-talk key, 1 = "talking to" key
    float meterDb = -100.f, listenMeterDb = -100.f;
    std::string textDraft;             // Text page: what's being typed

    // ---- Features page
    std::string profileName;           // typed name for "Save as profile"
    std::string setupCodeMsg;          // after copying or pasting a setup code
    std::string wordHeard, wordWrite;  // new word list entry
    std::string phraseDraft;           // new quick phrase
    std::string deeplWordDraft;        // new word for "Words for DeepL"
    std::string tryText;               // "Try a line"
    std::string tryResults[4];         // Polite, Casual, Very casual, My own
    bool tryBusy = false;
    std::string historyQuery;
    std::vector<std::string> historyResults;
    std::string historyPath;           // the history folder
    std::vector<std::string> heardLanguages;  // languages others spoke this session, most heard first
    std::string autoTargetNote;        // "Answering in Japanese: they spoke it"
    std::string pendingSay;            // about to be spoken (a moment to cancel)
    float pendingLeft = 0;             // seconds left to cancel
    std::vector<int> mutedVoices;      // voices hidden this session (ChatLine::voiceId)
    ScreenShot shot;                   // screen translation preview
    bool screenNotice = false;         // show the screen translation notice
    bool screenNoticeTicked = false;
    std::string capturing;             // "VRChat.exe" while Watch chat runs
    std::string screenStatus;          // what screen translation is doing, or what went wrong
    std::vector<std::string> readerLanguages;  // what Windows' text reader can read on this PC ("ja", "en-US"...)
    bool readerKnown = false;          // that list has been looked up
    bool overlayClickThrough = false;  // the caption window lets clicks through right now
    std::string reportPath;            // the problem report, once written
};

struct UiActions {
    bool loadCatalog = false, reopenInput = false, reopenOutput = false, reopenMonitor = false;
    bool rescanDevices = false, stopSpeaking = false, save = false;
    bool resetBaseline = false, resetMatch = false, placeVrHere = false;
    bool openVrBindings = false;       // SteamVR's screen for picking the push-to-talk button
    bool startCheck = false, stopCheck = false, openSoundPanel = false;
    bool resetVr = false;              // start the headset captions over (they stopped updating)
    bool refreshApps = false, clearCaptions = false;
    int setCaptionsOnly = -1;  // 0 / 1 when the user switches the compact view
    int setMini = -1;          // 0 / 1 when the user switches the mini window
    bool checkUpdates = false, installUpdate = false, openSettingsFolder = false;
    bool listOlder = false;                  // look up the earlier versions
    int voicePack = 0;  // the voice fingerprint pack: 1 download, 2 stop, 3 delete
    std::string localDownload, localCancel, localRemove;  // a local model to download, stop downloading, delete
    bool localRemoveRetired = false;                      // delete every downloaded model that's no longer offered
    std::string installOlder;                // go back to this version and restart
    bool pickPicture = false, clearPicture = false;  // Settings > Look
    int setSettingsPlace = -1;  // SettingsPlace picked in Settings > About
    std::string sendText;       // Text page: say this
    std::string testSound;      // setup: say this once, nowhere but the output
    bool openLog = false;
    int checkKey = -1;          // KeyGuide whose key to try (DeepL, Gemini, Claude, OpenAI)
    // ---- Features page
    std::string loadProfile, saveProfile, deleteProfile;
    bool copySetupCode = false, pasteSetupCode = false;
    std::string tryLine;        // translate this in every style
    std::string historySearch;  // look through the history files
    bool openHistory = false;
    bool reportProblem = false;
    bool cancelPending = false; // don't say what's about to be spoken
    std::string replayText, replayLang;  // learning mode: say this line again in your headphones
    uint64_t romanize = 0;      // learning mode: fetch how to read this line (ChatLine::key)
    bool screenSnapshot = false;   // take a picture of the chosen window
    bool screenTranslate = false;  // read and translate the preview (with its crop)
    bool screenUseArea = false;    // the preview's crop becomes the Watch chat area, and Watch starts
    bool screenClose = false;
    bool openCaptionWindow = false;
    bool spendOk = false;       // the spending notice was read: count the next limit from now
    std::string openUrl;        // a status page
};
