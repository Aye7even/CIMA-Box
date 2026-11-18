// "Carry my emotion": works out how a phrase was said and writes that into the text the voice
// reads, using the word timings the transcriber returns:
//   - which words you stressed (louder, higher or longer than the words around them) -> CAPITALS
//   - words you drew out ("nooo")
//   - pauses -> "," or "..."
//   - the delivery of each part of the phrase, separately -> [excited], [whispers], [flatly], ...
//   - questions from your voice rising at the end -> "?"
//   - laughs, sighs and other sounds where they happened -> [laughs]
// Example: "no way. you actually did that" said with a stressed "actually", a drawn-out "no" and
// a flat second half becomes  "[excited] Nooo way! [flatly] You ACTUALLY did that."
//
// Loudness alone is a poor guide (it changes with how close you are to the mic), so every
// judgement also needs pitch, vocal effort (how bright the voice is) or timing to agree.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------- measuring

// Frame-by-frame measurements of a phrase, 10 ms apart. Computed while the phrase is being
// transcribed, so it adds no wait.
struct VoiceTrack {
    std::vector<float> db;     // loudness, dB full scale
    std::vector<float> pitch;  // semitones above 100 Hz; NaN where there is no voice (silence, breath, whisper)
    std::vector<float> tilt;   // vocal effort: brightness of the voice in dB (higher = pushed harder)
    float topDb = -100, floorDb = -100;
};
VoiceTrack trackVoice(const std::vector<int16_t>& pcm16k);

// A guess whether a voice sounds like a man or a woman, from its typical pitch between `startS` and
// `endS` (seconds; end < 0 = to the end). kGenderUnknown (0) when it's in between or there's too
// little voiced sound. Voice changers and avatars' voices fool it, so it's only ever a hint.
int guessVoiceGender(const std::vector<int16_t>& pcm16k, float startS, float endS, float* medianHz = nullptr);

// The speaker's usual voice, learned from ordinary phrases and saved with the settings.
struct VoiceBaseline {
    float loudDb = -30, pitch = 6, range = 4, tilt = -12, rate = 3;
    int phrases = 0;
    static constexpr int kReady = 3;
    bool ready() const { return phrases >= kReady; }
};

enum EmotionStrength { kEmotionSubtle = 0, kEmotionStrong = 1 };

// ---------------------------------------------------------------- the transcript

enum TokenKind { kTokWord = 0, kTokSpace = 1, kTokSound = 2 };
struct HeardToken {
    std::string text;
    int kind = kTokWord;
    float start = -1, end = -1;  // seconds from the start of the recording; -1 = unknown
};

// ---------------------------------------------------------------- deciding

// One part of a phrase: split where you paused or a sentence ended.
struct SpokenPart {
    std::string plain;     // the words as heard, for translating
    std::string marked;    // the words with stressed / drawn-out words and sounds marked (same language only)
    std::string tag;       // "excited", "shouts", "whispers", "softly", "flatly", or empty
    bool question = false, exclaim = false;
    float pauseBefore = 0; // seconds of silence before this part
    std::vector<std::string> leadSounds, trailSounds;  // sounds at its edges, for after translation
};

struct Direction {
    std::vector<SpokenPart> parts;
    std::vector<std::string> stressed, drawnOut;  // for showing in the app
    std::string summary;                           // e.g. "Stressed: actually. Drawn out: no. Parts: excited, then flat."
};

// `tagsAllowed`: the model follows [tags] and CAPITALS (Eleven v3 / v4). Without it, only
// punctuation and drawn-out spellings are used, because other models read brackets out loud and
// spell capitals as letters.
Direction directSpeech(const VoiceTrack& track, const std::vector<HeardToken>& tokens, VoiceBaseline& base, int strength,
                       bool tagsAllowed);

// The line for the voice when the words stay as they were (Re-speak).
std::string renderLine(const Direction& d, bool tagsAllowed);

// One part after translation: its tag, sounds and end punctuation around the translated text.
std::string renderTranslatedPart(const SpokenPart& part, const std::string& translated, bool tagsAllowed, bool first);

// True when the parts sound different enough that each is worth translating on its own.
bool partsDiffer(const Direction& d);

// Maps a transcribed sound ("(laughter)") to an audio tag ("laughs"); empty = not a voice sound.
std::string eventTag(const std::string& event);

// True for models that follow audio tags (Eleven v3 and v4).
bool modelTakesTags(const std::string& modelId);
