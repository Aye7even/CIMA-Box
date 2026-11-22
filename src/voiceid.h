// Voice fingerprints: telling who is who by the voice itself.
//
// Listen's built-in way of following a voice across captions compares a few simple measurements
// (pitch, how much it moves, how bright it sounds). That is enough for two or three clearly
// different voices and not for a busy room. A speaker-recognition model does it properly: it
// turns a second or two of speech into a list of numbers (the fingerprint) that stays close for
// the same person whatever they say, and far for someone else.
//
// It is an extra download, on request: a small pack with the model and the library that runs it
// (sherpa-onnx with ONNX Runtime, both open source), about 45 MB, kept in the app's data folder.
// It runs on this PC, on the processor, off to the side of the captions: no audio goes anywhere
// new and nothing is paid per caption. Without the pack, or if it can't be loaded, everything
// works as before.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum VoiceIdStage { kVoiceIdNone = 0, kVoiceIdDownloading = 1, kVoiceIdInstalled = 2 };

struct VoiceIdState {
    int stage = kVoiceIdNone;
    uint64_t gotBytes = 0;    // while downloading
    std::string error;        // why the last download or load failed ("" = fine)
    bool loaded = false;      // the model is in memory and answering
    uint64_t diskBytes = 0;
};

class VoiceId {
public:
    // `dir`: where the pack is kept. `log`: one line at a time, for the app log.
    VoiceId(const std::filesystem::path& dir, std::function<void(const std::string&)> log);
    ~VoiceId();

    void download();  // in the background; progress in state()
    void cancel();
    void remove();    // unloads it and deletes the files

    VoiceIdState state() const;
    bool installed() const;

    // The fingerprint of `n` samples of one person's speech (16 kHz, -1..1): a list of numbers of
    // length 1 (compare two with likeness()). False when the pack isn't there or can't be loaded,
    // or there's under a second of sound. Safe to call from several threads; loads the model the
    // first time (a moment).
    bool fingerprint(const float* samples, size_t n, std::vector<float>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// How alike two fingerprints are: 1 = the same voice, around 0 = unrelated. 0 when either is empty
// or they come from different models.
float likeness(const std::vector<float>& a, const std::vector<float>& b);

// The one the app uses (set once at start, by main; null in tests that don't need it).
void setVoiceIdEngine(VoiceId* v);
VoiceId* voiceIdEngine();

// ---- for tests
void setVoiceIdPackUrl(const std::string& url);
