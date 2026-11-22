// Listening in a crowd.
//
// Listen only ever gets one mix of everyone talking. Pulling a dozen voices apart is beyond it, but
// two things narrow the mix down to the people a caption is wanted for:
//
//   NearGate    - "Nearby": games make close voices louder than far ones. The level a caption opens
//                 at follows the loudest voices of the last few seconds, so the chatter further away
//                 stays under it.
//   FrontFilter - "In front": a voice straight ahead reaches both ears the same; a voice off to one
//                 side arrives louder and earlier in one of them. Sound that is the same on the left
//                 and the right is kept, the rest is turned down, pitch by pitch, 125 times a second.
//                 It can't tell in front from behind, and needs a stereo source.
#pragma once
#include <complex>
#include <deque>
#include <vector>

class NearGate {
public:
    void reset() { peak_ = -100.f; }
    // The level a phrase opens at: the one the user set, or `kBelow` dB under the loudest speech
    // lately, whichever is higher. `levelDb`: these 10 ms.
    float threshold(float userDb, float levelDb);

    static constexpr float kBelow = 14.f;     // dB under the loudest voice that still counts as nearby
    static constexpr float kFallDb = 0.03f;   // per 10 ms: the memory of a loud voice fades at 3 dB a second

private:
    float peak_ = -100.f;
};

class FrontFilter {
public:
    FrontFilter();
    void reset();
    // `n` samples in: the sound (left and right together) and half their difference, as Listen keeps
    // them. `n` samples out: the same sound with everything off to the sides turned down. The output
    // runs kDelay samples (32 ms) behind the input.
    void process(const float* mid, const float* side, float* out, int n);

    static constexpr int kSize = 512, kHop = 128, kDelay = kSize;
    static constexpr float kFloor = 0.1f;  // what is turned down stays at a tenth (-20 dB): no holes, no chirping

private:
    void frame();

    std::vector<float> window_;
    std::deque<float> inL_, inR_, out_;
    std::vector<float> acc_;   // overlapping output frames being added up
    std::vector<float> mask_;  // per pitch band, smoothed from frame to frame
    std::vector<std::complex<float>> l_, r_;
};
