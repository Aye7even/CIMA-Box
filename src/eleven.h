// Minimal ElevenLabs REST client.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "emotion.h"

#include "http.h"

struct VoiceSettings {
    float stability = 0.5f;
    float similarity = 0.75f;
    float style = 0.0f;
    float speed = 1.0f;       // text-to-speech models only
    bool speakerBoost = true;
    // Professional voice clones trained for Eleven v4 only: how closely the output follows the
    // clone's training recordings (0..1). Unset = the value saved with the voice on ElevenLabs.
    bool setCloneStrength = false;
    float cloneStrength = 0.0f;
};

struct Voice { std::string id, name, category, previewUrl; };  // previewUrl: ElevenLabs' free sample (MP3)
struct Model { std::string id, name; bool voiceConversion = false, textToSpeech = false; };

struct TurnWord {
    std::string text;            // the word and the space after it, as written
    float start = -1, end = -1;  // seconds into the audio; -1 = unknown
};
struct SpeakerTurn {
    int speaker = 0;
    std::string text;
    float start = -1, end = -1;  // seconds into the audio; -1 = unknown (the whole phrase)
    std::vector<TurnWord> words;  // with speakers told apart: each word and when it was said
};
struct Transcript {
    std::string text;      // everything that was said
    std::string language;  // code the service reports (2 or 3 letters)
    float languageSure = 0;  // how sure it is of the language, 0..1 (0 = not reported)
    int speakers = 1;      // how many different voices it told apart
    std::vector<std::string> events;  // with `events`: sounds it heard ("laughter", "music"), in order
    int words = 0;         // how many words were said
    std::vector<SpeakerTurn> turns;  // in spoken order; empty when nothing was said
};

// Receives the generated audio as it streams in, in the requested output_format (see voiceout.h).
using AudioSink = ByteSink;

// Holds one connection that is reused across requests. Use one Eleven per thread.
class Eleven {
public:
    static constexpr int kOutputRate = 24000;  // Listen / tests; My voice picks its format per request (needs a Pro plan)

    std::string apiKey;
    Http http;  // set http.cancel to abort a running request

    // Opens the HTTPS connection ahead of time so the next request skips the handshake.
    void warm();

    bool listVoices(std::vector<Voice>& out, std::string& err);
    bool listModels(std::vector<Model>& out, std::string& err);

    // True voice changer: keeps your timing and intonation. pcm = 16 kHz mono.
    // `pcm` is the phrase at 16 kHz. With `fullPcm` (same phrase at `fullRate`), that is uploaded
    // instead, so the voice changer hears your whole voice, not just what fits in 16 kHz.
    bool speechToSpeech(const std::string& voiceId, const std::string& modelId, const VoiceSettings& vs,
                        bool removeNoise, const std::vector<int16_t>& pcm, const AudioSink& sink, std::string& err,
                        const std::string& outputFormat = "pcm_24000", const std::vector<int16_t>* fullPcm = nullptr,
                        int fullRate = 0);

    // Re-speak path: transcribe, then synthesize. languageCode is an optional ISO 639-1 hint;
    // detectedLanguage is whatever code the service reports (2 or 3 letters).
    // With `tokens`, it also listens for laughs, sighs and other sounds and returns the transcript
    // as words and sounds in order; `text` stays words only.
    bool transcribe(const std::vector<int16_t>& pcm, const std::string& languageCode, std::string& text,
                    std::string& detectedLanguage, std::string& err, std::vector<HeardToken>* tokens = nullptr);
    // Listen path: like transcribe(), but split into who said what when `tellSpeakersApart` is set.
    // `events`: also listen for sounds (laughs, music); with `labels` they're written into the turns as [laughter].
    bool transcribeTurns(const std::vector<int16_t>& pcm, const std::string& languageCode, bool tellSpeakersApart,
                         Transcript& out, std::string& err, bool events = false, bool labels = false);
    bool textToSpeech(const std::string& voiceId, const std::string& modelId, const VoiceSettings& vs,
                      const std::string& text, const std::string& languageCode, const AudioSink& sink,
                      std::string& err, const std::string& outputFormat = "pcm_24000");

private:
    bool rawPcmUpload_ = true;  // send raw PCM (quicker, per ElevenLabs) until the service refuses it
    Http spare_;                // a second connection, for a second copy of a transcription that stalls
    int sttWaitMs_ = 0;         // how long transcription usually takes once the audio is sent (0 = not known yet)
};
