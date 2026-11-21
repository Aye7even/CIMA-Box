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
#include <utility>
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

// A rough fingerprint of a voice, to tell the same person apart across captions: typical pitch,
// how much the pitch moves, and how bright the voice sounds. False when there's too little voice.
struct VoicePrint { float pitch = 0, spread = 0, bright = 0, voiced = 0; };  // voiced: seconds of clear voice it came from
bool voicePrint(const std::vector<int16_t>& pcm16k, float startS, float endS, VoicePrint& out);
float voiceDistance(const VoicePrint& a, const VoicePrint& b);  // about 1 = probably not the same person

// Keeps track of who is who among the voices heard (Listen's "Same voice, same color"). A voice
// print from a few seconds of speech is a rough thing, so this is careful on purpose: it would
// rather leave a line unnamed than give the same person three numbers.
//  - A voice only gets a number once it has been heard three times.
//  - A line that could be one of two people, or is too short to tell, stays unnamed (-1).
//  - Whoever spoke a moment ago is the likeliest to be speaking now.
//  - At most kMax numbers; one that has been quiet for a while is given up and can be used again.
class VoiceBook {
public:
    static constexpr int kMax = 6;
    // Which voice this is: 0..kMax-1; 100 or more = remembered but without a number yet (show it as
    // unnamed); -1 = can't say. `now` in seconds (any steady clock).
    // `keep`: numbers that must not be given up or reused (muted voices).
    // `renamed`: (from, to) pairs for lines already shown; "from" >= 100 is a voice that had no number yet.
    int who(const VoicePrint& p, double now, const std::vector<int>& keep, std::vector<std::pair<int, int>>& renamed);
    void clear() { voices_.clear(); nextTemp_ = 100; last_ = -1; }

private:
    struct Known { VoicePrint mean; int count = 0; double heard = 0; int id = -1; };  // id >= 100: no number yet
    std::vector<Known> voices_;
    int nextTemp_ = 100, last_ = -1;
};

// The speaker's usual voice and how much it normally varies, learned from their speech and saved
// with the settings. It keeps learning (slowly, and less from parts that sounded unusual), so a few
// odd phrases at the start correct themselves.
struct VoiceBaseline {
    float loudDb = -30, pitch = 6, range = 4, tilt = -12, rate = 3;
    // How much each normally moves from part to part (variance): someone expressive swings more,
    // so a part has to stand out by more than their usual swing to count.
    float pitchVar = 2.25f, loudVar = 6.25f, rateVar = 0.6f;
    int phrases = 0;                      // parts learned from
    // Loudness and brightness depend on the mic (and how close you are); pitch, its swing and your
    // pace are yours. After a mic change only the mic's part is learned again.
    int micParts = 0;                     // parts learned from on this mic
    int loudStreak = 0;                   // parts in a row that came out "softly" (-) or "shouts" (+)
    static constexpr int kReady = 8;
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
    std::string tag;       // "excited", "shouts", "whispers", "softly", "flatly", "hesitant", "quickly",
                           // "slowly", "sad", or empty
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
