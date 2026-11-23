// A closer look at a phrase before it is sent anywhere: is there a voice in it, and how noisy is it?
//
// Listen cuts the sound into phrases by loudness alone, so a door, footsteps, a game's music or a
// fan that got loud for a moment is a "phrase" too. Each one was sent to the speech service (paid
// for, and one more phrase in the queue ahead of the people actually talking). A small
// voice-detection model (Silero VAD) settles it on this PC in a few thousandths of a second: a
// phrase with no voice in it goes no further.
//
// The same pack carries a small noise-removal model (GTCRN). It takes a hiss, a fan or a hum out
// from under a voice. That is offered as a trial for the words only ("Clean noisy phrases"): tried
// on sample recordings it made voices easier to hear and voice fingerprints less able to tell people
// apart, so fingerprints always get the sound as it was.
//
// Like voice fingerprints and sound recognition it is a pack in the app's data folder: the two
// models and the library that runs them (sherpa-onnx with ONNX Runtime, both open source), fetched
// by itself the first time it's needed. Without it, or if it can't be loaded, everything works as
// before.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum CleanSoundStage { kCleanSoundNone = 0, kCleanSoundDownloading = 1, kCleanSoundInstalled = 2 };

struct CleanSoundState {
    int stage = kCleanSoundNone;
    uint64_t gotBytes = 0;    // while downloading
    std::string error;        // why the last download or load failed ("" = fine)
    bool loaded = false;      // the models are in memory and answering
};

class CleanSound {
public:
    // `dir`: where the pack is kept. `log`: one line at a time, for the app log.
    CleanSound(const std::filesystem::path& dir, std::function<void(const std::string&)> log);
    ~CleanSound();

    void download();  // in the background; progress in state()
    void warm();      // loads the models in the background, so the first phrase doesn't wait for them

    CleanSoundState state() const;
    bool installed() const;

    // How many seconds of `pcm` (16 kHz) are someone's voice. A quiet phrase is brought up to an
    // ordinary level first, so a far-away voice counts like a near one. False when the pack isn't
    // there or can't be loaded (then nothing is known, and the phrase goes on as before).
    // Safe to call from several threads.
    bool speechSeconds(const std::vector<int16_t>& pcm, float& seconds);

    // The same phrase with the steady noise under the voice taken out, at the same length (word
    // times still fit). False, and `pcm` untouched, when it can't be done.
    bool clean(std::vector<int16_t>& pcm);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// How far the voice stands above what is under it, in dB: the loud tenth of the phrase against its
// quiet tenth, 20 ms at a time. A voice in a quiet room is 25 dB and more (the gaps between words
// are near silent); under a fan or a hiss it is 20 and less. Needs no pack.
float standsOutDb(const std::vector<int16_t>& pcm);

// The one the app uses (set once at start, by main; null in tests that don't need it).
void setCleanSoundEngine(CleanSound* c);
CleanSound* cleanSoundEngine();

// ---- for tests
void setCleanSoundPackUrl(const std::string& url);
