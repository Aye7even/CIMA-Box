// "Match my mic": makes the generated voice take on the tone and loudness of the microphone you
// are actually using, so switching between your real voice (passthrough) and the new voice, or
// between two microphones, doesn't change how you come across.
//
// It keeps a long-running average of the frequency balance and speech level of your mic and of
// the generated voice, in 7 bands from 150 Hz to 8 kHz, and corrects the generated voice by the
// difference with a smooth equaliser. Nothing is added (no echo, no hiss) — it only reshapes what
// is there. It runs on this computer as the audio arrives, so it adds no delay.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

class MicMatch {
public:
    static constexpr int kBands = 7;

    explicit MicMatch(int sampleRate);

    // Your microphone while you speak (capture thread). Mono floats in -1..1.
    void learnMic(const float* samples, size_t count);
    // The generated voice, corrected in place (worker thread). Learns from it before correcting.
    void process(int16_t* samples, size_t count, float amount);

    void forgetMic();              // a different microphone was picked: learn it from scratch
    bool ready() const;            // heard enough of both to correct anything
    float micSeconds() const;      // speech heard from the mic so far, capped at kNeedSeconds
    float voiceSeconds() const;
    static constexpr float kNeedSeconds = 4.f;

    // The current correction, for showing / testing: dB per band, and the level change.
    std::array<float, kBands> bandGainsDb(float amount) const;
    float levelGainDb(float amount) const;

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run(double x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    struct Side {  // running analysis of one signal
        std::array<Biquad, kBands> band;
        std::array<double, kBands> energy{};  // average energy per band while speaking
        double level = 0;                      // average energy while speaking
        double seconds = 0;
        std::array<double, kBands> blockEnergy{};
        double blockLevel = 0;
        size_t blockFill = 0;
    };
    void analyse(Side& side, double x);
    void setupAnalysis(Side& side);
    void updateEq(float amount);
    std::array<float, kBands> bandsLocked(float amount) const;
    float levelLocked(float amount) const;

    const int rate_;
    const size_t block_;  // 20 ms
    mutable std::mutex mtx_;
    Side mic_, voice_;
    std::array<Biquad, kBands> eq_;
    std::array<float, kBands> eqDb_{};
    float levelDb_ = 0;  // currently applied
    double levelGain_ = 1;
};
