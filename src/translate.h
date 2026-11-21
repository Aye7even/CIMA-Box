// Text translation between the transcription and the voice.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "http.h"

enum TranslateService { kMyMemory = 0, kDeepL = 1, kClaude = 2, kGemini = 3, kOpenAI = 4, kLocal = 5, kMicrosoft = 6 };
// kLocal: a model on this PC (localai.h). kMicrosoft: Microsoft Translator (Azure), with your own key.
// Claude, Gemini or OpenAI: the services that word things like a person (styles, dialects, slang).
inline bool isAiService(int s) { return s == kClaude || s == kGemini || s == kOpenAI || s == kLocal; }
inline bool isOnlineAi(int s) { return s == kClaude || s == kGemini || s == kOpenAI; }  // can also read pictures
// How the AI services word things on My voice. (Listen always translates faithfully.)
// kLevelCustom: the person describes it in their own words (TranslateSetup::customStyle).
enum SpeechLevel { kLevelPolite = 0, kLevelCasual = 1, kLevelVeryCasual = 2, kLevelCustom = 3 };

// Gender, for languages whose words change with it. Who is speaking:
enum Gender { kGenderUnknown = 0, kGenderMan = 1, kGenderWoman = 2, kGenderNonbinary = 3 };
// Who they are talking to (resolved; "Auto" is decided by the app before translating):
enum Addressee { kListenerUnknown = 0, kListenerMan = 1, kListenerWoman = 2, kListenerGroup = 3 };

// A regional way of speaking a language: Mexican Spanish, Kuwaiti Arabic, Kansai Japanese...
// The AI services write in it; DeepL only knows a few regional spellings (deepl).
struct Variety {
    const char* lang;   // language code it belongs to, e.g. "es"
    const char* id;     // e.g. "es-MX", saved in the settings
    const char* name;   // e.g. "Mexico"
    const char* ai;     // how the AI is told, e.g. "Mexican Spanish, as spoken in Mexico"
    const char* deepl;  // DeepL target for it, or "" when DeepL has none (the standard language is used)
};
// The varieties offered for a language (empty when there's no list; a custom one can still be typed).
std::vector<const Variety*> varietiesOf(const std::string& langCode);
const Variety* findVariety(const std::string& id);

// Everything a translation needs besides the text itself.
struct TranslateSetup {
    int service = kMyMemory;
    std::string deeplKey, claudeKey, geminiKey, openaiKey;
    std::string msKey, msRegion;  // Microsoft Translator (optional backup); region only for regional resources
    // A model on this PC (kLocal): where it answers and the key it wants, while it's loaded
    // (empty otherwise; then localNote says why: "Loading the model", "Download a model first"...).
    std::string localUrl, localKey, localModel, localNote;
    bool aiBackup = true;         // another AI with a key steps in on lines this one misses
    // A line the online AI declines goes to the model on this PC first (when one is loaded), before
    // DeepL, Microsoft or Free: it still words it like a person would.
    bool localOnDecline = false;
    std::string claudeModel = "claude-haiku-5-5";
    std::string geminiModel = "gemini-3.5-flash-lite";
    std::string openaiModel = "gpt-6-luna";
    int level = kLevelCasual;
    std::string customStyle;    // kLevelCustom: e.g. "laid back, a bit sarcastic, like a gamer friend"
    std::string persona;        // optional, e.g. "a guy in his 20s from Seoul"
    bool faithful = false;      // Listen: for understanding what others say, not for speaking
    int timeoutMs = 2000;       // AI services: after this, use DeepL (or Free) for this phrase instead
                                // (a dialect gets a little longer: its answers take more work)
    std::string context;        // recent lines of the conversation, oldest first ("Them: ...", "Me: ...")
    // What the speaker said just before, in the same stretch of talking (oldest first): what they said,
    // and how it was said for them. This line goes on from it.
    std::vector<std::pair<std::string, std::string>> before;
    std::string variety;        // the dialect to speak (AI services), e.g. "Kuwaiti Arabic, as spoken in Kuwait"
    std::string varietyName;    // short, for the status line: "Kuwaiti"
    std::string deeplVariant;   // DeepL's regional target for it ("ES-419"), empty = standard
    int swear = 0;              // SwearMode (extras.h): what to do with your swearing (My voice)
    std::string wordNote;       // "- Keep these names and words exactly as written: ..." (personal word list)
    std::vector<std::string> deeplWords;  // "Words for DeepL": translated by DeepL (or Free), never by the AI
    bool readGender = false;    // Listen: ask whether the words show the speaker's gender (-> Translator::wordsGender)
    int speaker = kGenderUnknown;     // My voice: you. Listen: the guess from the speaker's voice.
    int listener = kListenerUnknown;  // Addressee. My voice: who you are talking to
    int multiLines = 0;         // translateLines: the text is this many numbered lines, answered one tag each
    std::string backupOnly;     // translateLines: the AI already ran out of time on these lines (this says
                                // so): straight to DeepL / Microsoft / Free, not the AI again
};

struct Language {
    const char* name;
    const char* code;         // ISO 639-1, used in settings and for ElevenLabs
    const char* iso3;         // ISO 639-3 codes the transcriber may report (space separated)
    const char* myMemory;
    const char* deeplTarget;
    const char* deeplSource;
    bool everyModel;          // also spoken by Multilingual v2 / Flash v2.5 / Turbo v2.5, not only v3 and v4
    const char* otherNames;   // other English names, for searching ("Farsi", "Tagalog")
};

// True when `query` matches the language's name, other names or code (case-insensitive).
bool languageMatches(const Language& l, const std::string& query);

const std::vector<Language>& languages();
// Accepts a 2- or 3-letter code in any case; nullptr if the language isn't in the list.
const Language* findLanguage(const std::string& code);

// Use one Translator per thread.
// Tries a key with the smallest possible request. msg says what happened, in plain words.
// service: kDeepL, kClaude, kGemini or kOpenAI. Blocks for up to ~20 s, so run it off the UI thread.
bool checkServiceKey(int service, const std::string& key, std::string& msg);

// DeepL's server for this key (free keys end in ":fx").
std::string deeplHost(const std::string& key);
// Tokens an AI service (kClaude, kGemini, kOpenAI) used so far in this run of the program.
void aiTokens(int service, long long& in, long long& out);

class Translator {
public:
    Http http;

    std::string via;  // after translate(): which service did it, e.g. "Claude" or "DeepL (Claude too slow)"
    int wordsGender = 0;  // after translate() with readGender: Gender the speaker's own words showed (0 = they didn't)

    void warm(const TranslateSetup& setup);

    // `from` may be null when the spoken language is unknown (DeepL and the AI services detect it;
    // the free service can't).
    bool translate(const TranslateSetup& setup, const std::string& text, const Language* from, const Language& to,
                   std::string& out, std::string& err);

    // Several lines at once (one caption, maybe several people): with an AI service, one request for
    // all of them (the instructions and context are sent once, and it sees the whole exchange); if
    // that fails, each line on its own, with the usual backups. `froms`: each line's language (or
    // null); `speakers`: each line's Gender guess from the voice. `outs[i]` stays empty for a line
    // that couldn't be translated (`err` says why); `genders[i]`: what the words showed (see
    // wordsGender). False only when cancelled.
    bool translateLines(const TranslateSetup& setup, const std::vector<std::string>& texts,
                        const std::vector<const Language*>& froms, const std::vector<int>& speakers, const Language& to,
                        std::vector<std::string>& outs, std::vector<int>& genders, std::string& err);

    // "Which means": a translated line put back into the speaker's language, by a translator that
    // takes words literally (Free, then Microsoft, then DeepL), never an AI: it shows what the words
    // really say, so a wrong or refused line is caught. No AI tokens.
    bool checkBack(const TranslateSetup& setup, const std::string& text, const Language& from, const Language& to,
                   std::string& out, std::string& err);

    // Learning mode: how to read `text` (in `language`, e.g. "Japanese") in Latin letters: romaji,
    // pinyin with tone marks, Revised Romanization... Needs an AI service.
    bool romanize(const TranslateSetup& setup, const std::string& text, const std::string& language, std::string& out,
                  std::string& err);

    // Screen translation with AI reading: every line of text in the picture (a PNG file, as base64)
    // and its translation into `to`, in one request.
    bool readPicture(const TranslateSetup& setup, const std::string& pngBase64, const Language& to,
                     std::vector<std::pair<std::string, std::string>>& lines, std::string& err);

    // The instructions an AI service gets (exposed for testing).
    static std::string aiInstructions(const TranslateSetup& setup, const Language* from, const Language& to);

private:
    // Kept open between lines, so none of them has to connect first:
    Http spare_;       // a second connection to the AI, for a second copy of a slow request
    Http wordsHttp_;   // DeepL (or Free) for listed and swear words, alongside the AI
    Http backupHttp_;  // DeepL (or Free) backup, ready in case the AI runs out of time
    Http otherHttp_;   // a second AI (when there's a key for one), for lines the chosen AI fails on
    Http localHttp_;   // the model on this PC, for lines the online AI declines (localOnDecline)
    std::string typicalKey_;  // the service and model typicalMs_ is for
    int typicalMs_ = 0;       // how long that AI usually takes to answer a line (0 = not known yet)
    // Extra time a language needs on top of the time limit, per AI model and language (and dialect).
    // Some languages simply take longer to write out (Filipino sentences run long, and every word
    // costs the AI more): with one limit for all, they'd miss it on nearly every line. It grows a
    // little each time that language runs out of time and shrinks when it answers with room to
    // spare, never past double the limit.
    std::map<std::string, int> extraMs_;
};
