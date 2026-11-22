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
struct VoicePrint {
    float pitch = 0, spread = 0, bright = 0, voiced = 0;  // voiced: seconds of clear voice it came from
    float pan = 0;        // where it came from (stereo games): -1 left .. +1 right
    bool hasPan = false;  // only with stereo sound that really moves between left and right
    // Its fingerprint from the recognition model (voiceid.h), when that pack is installed: then the
    // voices are compared by this, and the measurements above only fill in where it's missing.
    std::vector<float> fp;
    // The clip turned out to hold more than one voice (its two halves have different fingerprints:
    // two people the speech service wrote down as one). It is nobody's voice: never named, never learned from.
    bool mixed = false;
};
bool voicePrint(const std::vector<int16_t>& pcm16k, float startS, float endS, VoicePrint& out);
// The same from only some stretches of the audio (seconds), joined: one person's own moments.
bool voicePrintSpans(const std::vector<int16_t>& pcm16k, const std::vector<std::pair<float, float>>& spans, VoicePrint& out);

// ---- Where a voice comes from. In VRChat (and other games with voices placed around you) someone
// on your left is louder in your left ear, so two people talking at once can be told apart even when
// their voices sound alike. `pan`: one value per 10 ms of the phrase, -32767 left .. 32767 right.
// True when the sound really moves between left and right (not mono, not everything in the middle).
bool panUseful(const std::vector<int16_t>& pan);
// Where the sound between two times came from, the louder moments counting most. False when it was
// (nearly) silent then.
bool panOver(const std::vector<int16_t>& pcm16k, const std::vector<int16_t>& pan, float startS, float endS, float& out);
// One speaker's words that clearly come from two different places (two people talking over each
// other, put together by the speech service): which of the two each word belongs to, 0 or 1.
// Empty when it shouldn't be split (one place, or too few words on one side to be sure).
std::vector<int> splitByPlace(const std::vector<float>& wordPan, const std::vector<float>& wordSeconds);
// The stretches that are only this person's: their words' times, minus the times anyone else was
// talking and, with `useDir`, minus the moments the sound came from somewhere else than `dir`.
std::vector<std::pair<float, float>> soloSpans(const std::vector<std::pair<float, float>>& mine,
                                               const std::vector<std::pair<float, float>>& others,
                                               const std::vector<int16_t>& pan, float dir, bool useDir);
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
    // How many numbers there are to give out. More with fingerprints (they can keep a dozen voices
    // apart); the built-in way stays at kMax.
    void setMax(int n) { max_ = n < 1 ? 1 : n; }
    // The last who(): how alike the voice was to the nearest and the next nearest one known
    // (1 = the same, 0 = unrelated), when both were compared by fingerprint. -1 otherwise. For the log.
    float lastNearest() const { return lastNearest_; }
    float lastNext() const { return lastNext_; }
    // The last who(), in a few words: why the line got the answer it got. For the log.
    const char* lastWhy() const { return lastWhy_; }

private:
    struct Known {
        VoicePrint mean;
        int count = 0;
        double heard = 0;
        int id = -1;          // >= 100: no number yet
        float pan = 0;        // where they were last heard from, and when (people move, so only briefly useful)
        double panAt = -1e9;
    };
    std::vector<Known> voices_;
    int nextTemp_ = 100, last_ = -1;
    int max_ = kMax;
    float lastNearest_ = -1, lastNext_ = -1;
    const char* lastWhy_ = "";
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
// The same for a name from the on-PC sound recogniser ("Laughter", "Cough"): only sounds a person's
// voice makes, and only the clear ones. "" for everything else (speech, music, a dog, a breath).
std::string voiceSoundTag(const std::string& name);

// True for models that follow audio tags (Eleven v3 and v4).
bool modelTakesTags(const std::string& modelId);
