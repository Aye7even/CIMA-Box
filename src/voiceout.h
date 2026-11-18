// Turns the audio ElevenLabs streams back (raw PCM or MP3, at its own sample rate) into 16-bit
// mono at the app's playback rate, as it arrives. MP3 is decoded frame by frame, so it starts
// playing as soon as the first frame (about 26 ms) is in.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Picked under Output -> Audio quality. Each needs a higher ElevenLabs plan than the last.
enum OutputQuality { kQualityStandard = 0, kQualityHigh = 1, kQualityBest = 2 };

// The ElevenLabs output_format for a quality: pcm_24000 (any plan), mp3_44100_192 (Creator and
// up), pcm_44100 (Pro and up). `fallbackStep` 1 asks for mp3_44100_128 instead of 192 kbps.
std::string outputFormatFor(int quality, int fallbackStep = 0);

class VoiceOut {
public:
    using Emit = std::function<void(int16_t* samples, size_t count)>;

    explicit VoiceOut(int playbackRate);
    ~VoiceOut();
    VoiceOut(const VoiceOut&) = delete;
    VoiceOut& operator=(const VoiceOut&) = delete;

    void begin(const std::string& outputFormat);  // "pcm_24000", "mp3_44100_192", ...
    void push(const char* bytes, size_t count, const Emit& emit);
    void finish(const Emit& emit);                 // whatever is still inside the decoder / resampler

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
