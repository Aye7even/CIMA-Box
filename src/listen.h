// Listen: captures what other people say (one application, a playback device or an input
// device), cuts it into phrases, transcribes and translates them, and keeps a caption feed.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "translate.h"

enum ListenSource { kSrcApp = 0, kSrcSpeakers = 1, kSrcInput = 2, kSrcExternal = 3 /* pushPcm only */ };
enum ListenPace { kPaceQuick = 0, kPaceBalanced = 1, kPaceFull = 2 };

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
    int voice = 0;         // Gender guessed from the voice (0 = can't tell)
};

struct Caption {
    uint64_t id = 0;
    bool pending = true;   // heard, still being transcribed
    std::string time;      // "21:04"
    std::string language;  // spoken language, e.g. "Japanese"
    std::vector<CaptionLine> lines;
};

struct ListenState {
    std::string status;    // e.g. "Listening to VRChat.exe"
    std::string error;     // last problem, empty when things work
    bool hearing = false;  // someone is talking right now
    bool busy = false;     // a phrase is being transcribed / translated
    float levelDb = -100.f;
    int lastMs = 0;        // end of the last phrase -> caption on screen
};

struct ListenConfig {
    ListenSettings s;
    std::string apiKey;        // ElevenLabs
    TranslateSetup translate;  // service, keys, AI settings (Listen always translates faithfully)
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
    ListenState state() const;

    // Audio entry for sources that deliver their own format (application capture, tests).
    // One thread at a time.
    void pushPcm(const int16_t* interleaved, size_t frames, int channels, int rate);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
