// The settings of the optional features (the Features page), and small helpers they share.
// Everything here is off or neutral by default, so someone who never opens the Features page gets
// CIMA Box exactly as before.
#pragma once
#include <map>
#include <string>
#include <vector>

// How swearing in what YOU say comes out of the voice.
enum SwearMode { kSwearKeep = 0, kSwearSoften = 1, kSwearBleep = 2 };
// Screen translation: who reads the words in the picture.
enum ScreenReader { kReadWindows = 0, kReadAi = 1 };

// A word the speech-to-text keeps getting wrong, or a name to always write one way.
struct WordFix {
    std::string heard;  // what it writes (may be empty: only "write" matters, e.g. a name)
    std::string write;  // how it should be written
};

struct ScreenSettings {
    bool on = false;
    bool agreed = false;           // read the screen translation notice and ticked the box
    std::string app = "VRChat.exe";
    int reader = kReadWindows;     // ScreenReader
    std::string readLang;          // the language in the picture, for the Windows reader ("" = from Windows' settings)
    std::string toLang;            // translate into ("" = the Listen page's language)
    bool skipPreview = false;      // translate right away, without the preview
    int key = 0;                   // hotkey: take a snapshot
    bool watch = false;            // keep reading a chat box and translate new lines
    float watchSeconds = 3.f;
    float area[4] = {0, 0, 1, 1};  // the part of the window to read (0..1: left, top, right, bottom)
    bool toVr = true;              // results also on the VR panel
};

struct FeatureSettings {
    // ---- words
    int voiceSwear = kSwearKeep;   // SwearMode
    bool maskCaptions = false;     // others' swearing shown as f***
    std::vector<WordFix> words;    // personal word list
    bool deeplWordsOn = false;     // words translated by DeepL instead of the AI
    std::vector<std::string> deeplWords;
    std::vector<std::string> quickPhrases;
    std::map<std::string, int> sentCounts;  // typed lines and how often, for suggestions (top ones kept)

    // ---- conversation
    bool hearThem = false;         // say others' translated captions in your headphones
    std::string earVoice;          // voice for that ("" = the voice you use)
    std::string earDevice;         // where ("" = the "Hear it yourself" device)
    float earVolume = 0.9f;
    bool liveCaptions = false;     // show words while they're still talking (extra speech-to-text)
    bool answerTheirs = false;     // "Translate to" follows the language people speak to you
    std::vector<std::string> myLanguages;      // languages you speak: limits "Detect automatically" for you
    std::vector<std::string> aroundLanguages;  // languages around you: limits it for Listen
    int previewMs = 0;             // a moment to cancel before the voice speaks (0 = off)
    int cancelKey = 0;
    bool best = false;             // translations: bigger models, more time
    bool genderFromWords = true;   // "I'm tired" in their language says more than their voice

    // ---- reading and learning
    bool learning = false;         // romanization, replay and save on conversation lines
    std::vector<std::string> savedLines;
    bool keepHistory = false;      // write the conversation to a file per day
    bool soundLabels = false;      // [laughs], [music] in captions
    bool ignoreNoise = true;       // drop captions that are only music or noise
    bool highContrast = false;     // captions: white on black, bold
    bool voiceIds = true;          // tell voices apart across captions: colours, mute

    // ---- the always-on-top caption window
    bool clickThrough = false;     // clicks go to the game underneath
    float overlayOpacity = 0.85f;
    int overlayKey = 0;            // switches click-through on and off (needed to get the window back)

    // ---- keys
    int realVoiceKey = 0;          // hold: your real voice goes out instead
    int stopKey = 0;               // stop the voice right away

    // ---- profiles: name -> saved settings (no keys)
    std::map<std::string, std::string> profiles;
};

// Personal word list: fixes applied to what speech-to-text wrote (whole words, any case).
std::string applyWordFixes(const std::string& text, const std::vector<WordFix>& words);
// For the AI's instructions: "Keep these names and words exactly as written: ..." ("" when none).
std::string wordListNote(const std::vector<WordFix>& words);

// Swearing (a short list of the common words in the main languages). Matching ignores case.
bool hasSwear(const std::string& text);
std::string maskSwears(const std::string& text);              // f***
std::string replaceSwears(const std::string& text, const std::string& with);  // "beep", or "" to drop them
// The swear words in a text, in order (for filling them in after the AI declined the line).
std::vector<std::string> swearWords(const std::string& text);
// The text with each swear word replaced by a marker [[1]], [[2]]...; `words` gets them in order.
std::string markSwears(const std::string& text, std::vector<std::string>& words);
// The same with your own list ("Words for DeepL"): every word or phrase from `list` in `text` (any
// letter case, whole words; anywhere in Chinese, Japanese and Korean text) becomes [[1]], [[2]]...
std::string markListed(const std::string& text, const std::vector<std::string>& list, std::vector<std::string>& found);

// Sentence-aware phrases (My voice, Translate and Re-speak). A pause can come in the middle of a
// sentence; translated on its own, half a sentence comes out with broken grammar.
// True when `text` (a transcript) sounds cut off: it ends on a comma, dash or "...", on a word a
// sentence doesn't end on ("and", "because", "the", "y", "und", "و", "けど"...), or it's a longer
// stretch with no closing punctuation at all. `lang`: the spoken language's code ("" = unknown).
bool sentenceUnfinished(const std::string& text, const std::string& lang);
// Where the last finished sentence inside `text` ends: the index where the next sentence starts
// (after its . ? ! and the space). npos when there's no finished sentence before the end.
size_t lastSentenceEnd(const std::string& text);
// A best guess at the language `text` is written in, as a language code ("ru", "fil", "en"...), for
// when the spoken language is set to automatic and nothing reported it. Non-Latin scripts decide it
// on their own; Latin-script text is matched against each language's most common little words.
// Short or mixed text may guess wrong; "" when there's nothing to go on.
std::string guessLanguage(const std::string& text);
// About how many words `text` is (Chinese and Japanese, written without spaces: 2 characters a word).
int roughWords(const std::string& text);
// Two pieces of one sentence, heard either side of a pause, as one: the closing "." the transcript
// put on the first piece is taken off ("I went to the store and." + "Then I..." -> "... and then I...").
std::string joinHeard(const std::string& first, const std::string& rest, const std::string& lang);

// A typed line was sent: count it (keeps the 40 most used).
void countSentLine(FeatureSettings& f, const std::string& line);
// The most used typed lines that aren't quick phrases already (at most `n`).
std::vector<std::string> commonLines(const FeatureSettings& f, size_t n);

// Share a setup: settings JSON (without keys) <-> a text code to paste in Discord.
std::string setupToCode(const std::string& settingsJson);
bool codeToSetup(const std::string& code, std::string& settingsJson);
