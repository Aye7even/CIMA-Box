#include "focus.h"

#include <algorithm>
#include <cmath>

float NearGate::threshold(float userDb, float levelDb) {
    peak_ = std::max(peak_ - kFallDb, levelDb);
    return std::max(userDb, peak_ - kBelow);
}

namespace {
// In-place FFT, size a power of two. `inverse` leaves the result unscaled.
void fft(std::vector<std::complex<float>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = 2.f * 3.14159265358979f / float(len) * (inverse ? 1.f : -1.f);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
}  // namespace

FrontFilter::FrontFilter() : window_(kSize), acc_(kSize), mask_(kSize / 2 + 1), l_(kSize), r_(kSize) {
    // Square-root Hann, used going in and coming out: the overlapping frames then add up evenly.
    for (int i = 0; i < kSize; ++i) window_[i] = std::sqrt(0.5f - 0.5f * std::cos(2.f * 3.14159265358979f * float(i) / float(kSize)));
    reset();
}

void FrontFilter::reset() {
    inL_.clear();
    inR_.clear();
    out_.assign(kDelay, 0.f);  // so there's always output to hand back, a fixed 32 ms behind
    std::fill(acc_.begin(), acc_.end(), 0.f);
    std::fill(mask_.begin(), mask_.end(), 1.f);
}

void FrontFilter::frame() {
    for (int i = 0; i < kSize; ++i) {
        l_[i] = inL_[size_t(i)] * window_[i];
        r_[i] = inR_[size_t(i)] * window_[i];
    }
    fft(l_, false);
    fft(r_, false);
    const int bins = kSize / 2 + 1;
    // How alike the two ears are in each pitch band: 1 = the same sound in both (straight ahead),
    // towards 0 and below = louder or earlier in one ear (off to a side).
    std::vector<float> want(size_t(bins), 1.f);
    double speechSum = 0;
    int speechBins = 0;
    for (int k = 0; k < bins; ++k) {
        const float a = std::norm(l_[k]), b = std::norm(r_[k]);
        const float alike = a + b > 1e-12f ? 2.f * (l_[k] * std::conj(r_[k])).real() / (a + b) : 1.f;
        const float m = std::max(0.f, std::min(1.f, (alike - 0.6f) / 0.35f));  // below 0.6: a side voice; above 0.95: ahead
        want[size_t(k)] = kFloor + (1.f - kFloor) * m * m;
        const float hz = float(k) * 16000.f / float(kSize);
        if (hz >= 300.f && hz <= 3000.f) {
            speechSum += want[size_t(k)];
            ++speechBins;
        }
    }
    // Low pitches wrap around the head and sound alike in both ears wherever they come from, so
    // under 300 Hz the answer is taken from the bands above, where it can be told.
    const float broad = speechBins ? float(speechSum / speechBins) : 1.f;
    for (int k = 0; k < bins; ++k) {
        const float hz = float(k) * 16000.f / float(kSize);
        const float target = hz < 300.f ? std::min(want[size_t(k)], broad) : want[size_t(k)];
        mask_[size_t(k)] = 0.5f * mask_[size_t(k)] + 0.5f * target;  // no sudden jumps from frame to frame
        const std::complex<float> mid = 0.5f * (l_[k] + r_[k]) * mask_[size_t(k)];
        l_[k] = mid;
        if (k > 0 && k < kSize / 2) l_[kSize - k] = std::conj(mid);
    }
    fft(l_, true);
    for (int i = 0; i < kSize; ++i) acc_[size_t(i)] += l_[i].real() / float(kSize) * window_[i] * 0.5f;  // 4 frames overlap: they sum to 2
    for (int i = 0; i < kHop; ++i) out_.push_back(acc_[size_t(i)]);
    std::copy(acc_.begin() + kHop, acc_.end(), acc_.begin());
    std::fill(acc_.end() - kHop, acc_.end(), 0.f);
    inL_.erase(inL_.begin(), inL_.begin() + kHop);
    inR_.erase(inR_.begin(), inR_.begin() + kHop);
}

void FrontFilter::process(const float* mid, const float* side, float* out, int n) {
    for (int i = 0; i < n; ++i) {
        inL_.push_back(mid[i] + side[i]);
        inR_.push_back(mid[i] - side[i]);
    }
    while (inL_.size() >= size_t(kSize)) frame();
    for (int i = 0; i < n; ++i) {
        out[i] = out_.empty() ? 0.f : out_.front();
        if (!out_.empty()) out_.pop_front();
    }
}
