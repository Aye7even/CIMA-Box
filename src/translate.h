// Text translation between the transcription and the voice.
#pragma once
#include <string>
#include <vector>

#include "http.h"

enum TranslateService { kMyMemory = 0, kDeepL = 1, kClaude = 2, kGemini = 3 };
// How the AI services word things on My voice. (Listen always translates faithfully.)
enum SpeechLevel { kLevelPolite = 0, kLevelCasual = 1, kLevelVeryCasual = 2 };

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
    std::string deeplKey, claudeKey, geminiKey;
    std::string claudeModel = "claude-haiku-5-5";
    std::string geminiModel = "gemini-3.5-flash-lite";
    int level = kLevelCasual;
    std::string persona;        // optional, e.g. "a guy in his 20s from Seoul"
    bool faithful = false;      // Listen: for understanding what others say, not for speaking
    int timeoutMs = 2000;       // AI services: after this, use DeepL (or Free) for this phrase instead
    std::string context;        // recent lines of the conversation, oldest first ("Them: ...", "Me: ...")
    std::string variety;        // the dialect to speak (AI services), e.g. "Kuwaiti Arabic, as spoken in Kuwait"
    std::string varietyName;    // short, for the status line: "Kuwaiti"
    std::string deeplVariant;   // DeepL's regional target for it ("ES-419"), empty = standard
    int speaker = kGenderUnknown;     // My voice: you. Listen: the guess from the speaker's voice.
    int listener = kListenerUnknown;  // Addressee. My voice: who you are talking to
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
class Translator {
public:
    Http http;

    std::string via;  // after translate(): which service did it, e.g. "Claude" or "DeepL (Claude too slow)"

    void warm(const TranslateSetup& setup);

    // `from` may be null when the spoken language is unknown (DeepL and the AI services detect it;
    // the free service can't).
    bool translate(const TranslateSetup& setup, const std::string& text, const Language* from, const Language& to,
                   std::string& out, std::string& err);

    // The instructions an AI service gets (exposed for testing).
    static std::string aiInstructions(const TranslateSetup& setup, const Language* from, const Language& to);

private:
    bool claudeNoEffort_ = false;    // the chosen Claude model refused the effort setting
    bool geminiNoThinking_ = false;  // the chosen Gemini model refused the thinking setting
};
