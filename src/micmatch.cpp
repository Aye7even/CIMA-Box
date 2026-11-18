#include "micmatch.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCentres[MicMatch::kBands] = {150, 300, 600, 1200, 2400, 4800, 8000};
constexpr double kSpeechDb = -50;   // 20 ms blocks quieter than this aren't speech
constexpr double kMemorySeconds = 30;  // the averages follow the last ~30 s of speech
constexpr float kMaxBandDb = 8, kMaxLevelDb = 12;

double dbOf(double energy) { return 10.0 * std::log10(energy + 1e-15); }

}  // namespace

MicMatch::MicMatch(int sampleRate) : rate_(sampleRate), block_(size_t(sampleRate / 50)) {
    setupAnalysis(mic_);
    setupAnalysis(voice_);
}

// Band-pass filters (Audio EQ Cookbook, constant 0 dB peak), one octave wide.
void MicMatch::setupAnalysis(Side& side) {
    for (int b = 0; b < kBands; ++b) {
        const double f = std::min(kCentres[b], rate_ * 0.45);
        const double w = 2 * kPi * f / rate_, cw = std::cos(w), al = std::sin(w) / (2 * 1.41);
        const double a0 = 1 + al;
        Biquad& q = side.band[size_t(b)];
        q = Biquad();
        q.b0 = al / a0;
        q.b1 = 0;
        q.b2 = -al / a0;
        q.a1 = -2 * cw / a0;
        q.a2 = (1 - al) / a0;
    }
}

void MicMatch::analyse(Side& s, double x) {
    for (int b = 0; b < kBands; ++b) {
        const double y = s.band[size_t(b)].run(x);
        s.blockEnergy[size_t(b)] += y * y;
    }
    s.blockLevel += x * x;
    if (++s.blockFill < block_) return;
    const double n = double(block_);
    if (dbOf(s.blockLevel / n) > kSpeechDb) {
        const double dt = n / rate_;
        // Plain average until there is enough, then a slowly moving one.
        const double a = s.seconds < kMemorySeconds ? dt / (s.seconds + dt) : dt / kMemorySeconds;
        for (int b = 0; b < kBands; ++b) s.energy[size_t(b)] += a * (s.blockEnergy[size_t(b)] / n - s.energy[size_t(b)]);
        s.level += a * (s.blockLevel / n - s.level);
        s.seconds += dt;
    }
    s.blockEnergy.fill(0);
    s.blockLevel = 0;
    s.blockFill = 0;
}

void MicMatch::learnMic(const float* samples, size_t count) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t i = 0; i < count; ++i) analyse(mic_, samples[i]);
}

void MicMatch::forgetMic() {
    std::lock_guard<std::mutex> lk(mtx_);
    mic_ = Side();
    setupAnalysis(mic_);
}

bool MicMatch::ready() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return mic_.seconds >= kNeedSeconds && voice_.seconds >= kNeedSeconds;
}

float MicMatch::micSeconds() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return float(std::min<double>(mic_.seconds, kNeedSeconds));
}

float MicMatch::voiceSeconds() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return float(std::min<double>(voice_.seconds, kNeedSeconds));
}

std::array<float, MicMatch::kBands> MicMatch::bandsLocked(float amount) const {
    std::array<float, kBands> g{};
    if (mic_.seconds < kNeedSeconds || voice_.seconds < kNeedSeconds) return g;
    // Compare shapes, not loudness: each side relative to its own average band.
    double micMean = 0, voiceMean = 0;
    for (int b = 0; b < kBands; ++b) {
        micMean += dbOf(mic_.energy[size_t(b)]) / kBands;
        voiceMean += dbOf(voice_.energy[size_t(b)]) / kBands;
    }
    for (int b = 0; b < kBands; ++b) {
        const double d = (dbOf(mic_.energy[size_t(b)]) - micMean) - (dbOf(voice_.energy[size_t(b)]) - voiceMean);
        g[size_t(b)] = float(std::max<double>(-kMaxBandDb, std::min<double>(kMaxBandDb, d)) * amount);
    }
    return g;
}

float MicMatch::levelLocked(float amount) const {
    if (mic_.seconds < kNeedSeconds || voice_.seconds < kNeedSeconds) return 0;
    const double d = dbOf(mic_.level) - dbOf(voice_.level);
    return float(std::max<double>(-kMaxLevelDb, std::min<double>(kMaxLevelDb, d)) * amount);
}

// Peaking filters at the band centres, set to the current difference.
void MicMatch::updateEq(float amount) {
    const std::array<float, kBands> want = bandsLocked(amount);
    for (int b = 0; b < kBands; ++b) {
        // Move gradually, so a change in the averages is never heard as a jump.
        const float target = want[size_t(b)];
        float& cur = eqDb_[size_t(b)];
        cur += std::max(-0.5f, std::min(0.5f, target - cur));
        const double f = std::min(kCentres[b], rate_ * 0.45);
        const double A = std::pow(10.0, cur / 40.0), w = 2 * kPi * f / rate_, cw = std::cos(w), al = std::sin(w) / (2 * 1.0);
        const double a0 = 1 + al / A;
        Biquad& q = eq_[size_t(b)];
        q.b0 = (1 + al * A) / a0;
        q.b1 = -2 * cw / a0;
        q.b2 = (1 - al * A) / a0;
        q.a1 = -2 * cw / a0;
        q.a2 = (1 - al / A) / a0;
    }
    const float wantLevel = levelLocked(amount);
    levelDb_ += std::max(-0.5f, std::min(0.5f, wantLevel - levelDb_));
    levelGain_ = std::pow(10.0, levelDb_ / 20.0);
}

void MicMatch::process(int16_t* samples, size_t count, float amount) {
    std::lock_guard<std::mutex> lk(mtx_);
    amount = std::max(0.f, std::min(1.f, amount));
    for (size_t i = 0; i < count; i += block_) {
        updateEq(amount);
        const size_t end = std::min(count, i + block_);
        for (size_t k = i; k < end; ++k) {
            const double x = samples[k] / 32768.0;
            analyse(voice_, x);  // learn from the voice as it came, before correcting it
            double y = x;
            for (Biquad& q : eq_) y = q.run(y);
            y *= levelGain_;
            if (std::fabs(y) > 0.9) y = (y > 0 ? 1 : -1) * (0.9 + 0.1 * std::tanh((std::fabs(y) - 0.9) / 0.1));  // no hard clipping
            samples[k] = int16_t(std::max(-32768L, std::min(32767L, std::lround(y * 32768.0))));
        }
    }
}

std::array<float, MicMatch::kBands> MicMatch::bandGainsDb(float amount) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return bandsLocked(amount);
}

float MicMatch::levelGainDb(float amount) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return levelLocked(amount);
}
