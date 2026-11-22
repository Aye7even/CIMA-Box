// Sound recognition on this PC: is that stretch of audio a laugh, a cough, a sigh?
//
// Live transcription reports words only, so with it on, Carry my emotion would never hear a laugh.
// The parts of a phrase that aren't words go to a small audio-tagging model instead (CED, trained on
// everyday sounds), which names what it hears. It runs on the processor in a few hundredths of a
// second: nothing is uploaded and nothing is paid for.
//
// Like voice fingerprints it is a pack kept in the app's data folder: the model, its list of sound
// names, and the library that runs it (sherpa-onnx with ONNX Runtime, both open source). It is
// fetched by itself the first time it's needed. Without it, or if it can't be loaded, everything
// works as before, only without those sounds.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum SoundIdStage { kSoundIdNone = 0, kSoundIdDownloading = 1, kSoundIdInstalled = 2 };

struct SoundIdState {
    int stage = kSoundIdNone;
    uint64_t gotBytes = 0;    // while downloading
    std::string error;        // why the last download or load failed ("" = fine)
    bool loaded = false;      // the model is in memory and answering
};

struct HeardSound {
    std::string name;  // as the model calls it: "Laughter", "Cough", "Speech"
    float sure = 0;    // 0..1
};

class SoundId {
public:
    // `dir`: where the pack is kept. `log`: one line at a time, for the app log.
    SoundId(const std::filesystem::path& dir, std::function<void(const std::string&)> log);
    ~SoundId();

    void download();  // in the background; progress in state()
    void warm();      // loads the model in the background, so the first phrase doesn't wait for it

    SoundIdState state() const;
    bool installed() const;

    // What `n` samples (16 kHz, -1..1) sound like: the model's most likely names, surest first.
    // False when the pack isn't there or can't be loaded, or there's under a fifth of a second.
    // Safe to call from several threads; loads the model the first time (a moment).
    bool listen(const float* samples, size_t n, std::vector<HeardSound>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// The one the app uses (set once at start, by main; null in tests that don't need it).
void setSoundIdEngine(SoundId* s);
SoundId* soundIdEngine();

// ---- for tests
void setSoundIdPackUrl(const std::string& url);
