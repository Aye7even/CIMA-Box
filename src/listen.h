// Listen: captures what other people say (one application, a playback device or an input
// device), cuts it into phrases, transcribes and translates them, and keeps a caption feed.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "extras.h"
#include "translate.h"

enum ListenSource { kSrcApp = 0, kSrcSpeakers = 1, kSrcInput = 2, kSrcExternal = 3 /* pushPcm only */ };
enum ListenPace { kPaceQuick = 0, kPaceBalanced = 1, kPaceFull = 2 };
// Who gets captions in a crowd (see focus.h): everyone, the loudest (closest) voices, or the voice in front.
enum ListenFocus { kFocusAll = 0, kFocusNear = 1, kFocusFront = 2 };

struct ListenSettings {
    bool on = false;
#ifdef _WIN32
    int source = kSrcApp;
#else
    int source = kSrcInput;
#endif
    std::string app = "VRChat.exe";  // program file name
    std::string speakers;            // playback device to listen to, empty = system default
    std::string input;               // capture device, empty = system default
    std::string fromLang;            // ISO 639-1, empty = detect automatically
    std::string toLang = "en";       // ISO 639-1
    float thresholdDb = -45.f;
    int pace = kPaceBalanced;
    int focus = kFocusAll;
    bool tellApart = true;           // label different speakers inside one caption
    bool showOriginal = true;
    bool onlyOtherLanguages = false; // hide speech that is already in toLang
    bool chatbox = false;            // also send captions to VRChat's chatbox over OSC
    float textScale = 1.25f;
};

struct CaptionLine {
    int speaker = -1;      // 0, 1, ... when a caption holds several speakers, -1 otherwise
    std::string text;      // what to read: the translation, or the words themselves
    std::string original;  // the words as spoken, when `text` is a translation
    int voice = 0;         // Gender guessed from the voice, or shown by their words (0 = can't tell)
    int voiceId = -1;      // the same voice across captions (a best guess), -1 = unknown
};

struct Caption {
    uint64_t id = 0;
    bool pending = true;   // heard, still being transcribed
    std::string time;      // "21:04"
    std::string language;  // spoken language, e.g. "Japanese"
    std::string code;      // its code, e.g. "ja" ("" = unknown)
    bool sure = false;     // the language is clear (not a guess from a few words, and on the "around me" list)
    bool live = false;     // still being spoken: `lines` hold the words so far, not translated yet
    bool screen = false;   // read from the screen (screen translation), not heard
    std::vector<CaptionLine> lines;
};

struct ListenState {
    std::string status;    // e.g. "Listening to VRChat.exe"
    std::string error;     // last problem, empty when things work
    bool hearing = false;  // someone is talking right now
    bool busy = false;     // a phrase is being transcribed / translated
    float levelDb = -100.f;
    int lastMs = 0;        // end of the last phrase -> caption on screen
    int sttMs = 0, translateMs = 0;  // ...of which: writing down the words, translating them
    bool streaming = false;          // their words are streamed while they talk (captions while they talk)
};

struct ListenConfig {
    ListenSettings s;
    std::string apiKey;        // ElevenLabs
    TranslateSetup translate;  // service, keys, AI settings (Listen always translates faithfully)
    // Features page
    std::vector<WordFix> words;          // personal word list
    std::vector<std::string> around;     // languages around me: limits "Detect automatically"
    bool soundLabels = false;            // [laughter] in captions
    bool ignoreNoise = true;             // drop captions that are only music or noise
    bool liveCaptions = false;           // words while they're still talking
    bool liveTranslate = true;           // ...and each finished sentence translated right away
    bool voiceIds = true;                // tell voices apart across captions
    bool voiceModel = false;             // ...by fingerprint, with the recognition model (voiceid.h), when it's installed
    std::vector<int> keepVoices;         // voice numbers that must stay as they are (muted ones)
    bool maskSwears = false;             // f***
};

class Listener {
public:
    Listener();
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    void start();
    void stop();

    void configure(const ListenConfig& c);  // cheap; call every frame
    void clear();                           // empties the feed and anything waiting

    uint64_t version() const;               // changes whenever the feed changes
    void captions(std::vector<Caption>& out) const;
    // A line read from the screen joins the feed, so it shows wherever captions do.
    void addScreenCaption(const std::string& text, const std::string& original);
    ListenState state() const;
    // Something you just said (in your own words): the next captions are translated knowing what
    // they are answering.
    void noteMine(const std::string& text);

    // Audio entry for sources that deliver their own format (application capture, tests).
    // One thread at a time.
    void pushPcm(const int16_t* interleaved, size_t frames, int channels, int rate);
    // For tests: a stand-in for the live speech service ("ws://127.0.0.1:port"). Before start().
    void useLiveHost(const std::string& host);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
