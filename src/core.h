// Dependency-free building blocks: lock-free ring buffer, phrase segmenter, WAV header.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

// Single-producer / single-consumer ring buffer (safe to use from an audio callback).
template <class T>
class SpscRing {
public:
    explicit SpscRing(size_t capacity) : buf_(capacity + 1) {}

    size_t size() const {
        size_t w = w_.load(std::memory_order_acquire), r = r_.load(std::memory_order_acquire);
        return w >= r ? w - r : w + buf_.size() - r;
    }
    size_t space() const { return buf_.size() - 1 - size(); }

    size_t push(const T* data, size_t n) {  // producer thread only
        n = std::min(n, space());
        size_t w = w_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            buf_[w] = data[i];
            w = (w + 1) % buf_.size();
        }
        w_.store(w, std::memory_order_release);
        return n;
    }
    size_t pop(T* out, size_t n) {  // consumer thread only
        n = std::min(n, size());
        size_t r = r_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            out[i] = buf_[r];
            r = (r + 1) % buf_.size();
        }
        r_.store(r, std::memory_order_release);
        return n;
    }
    void clear() { r_.store(w_.load(std::memory_order_acquire), std::memory_order_release); }  // consumer only

private:
    std::vector<T> buf_;
    std::atomic<size_t> r_{0}, w_{0};
};

struct GateParams {
    bool pushToTalk = false;   // false = voice activation
    bool pttDown = false;
    float thresholdDb = -40.f; // voice activation opens above this level
    int holdMs = 350;          // silence needed before a phrase is considered finished
    int maxSeconds = 12;       // long phrases are split after this
};

// Cuts the microphone stream (16 kHz mono) into phrases, fed in 10 ms blocks.
class Segmenter {
public:
    static constexpr int kRate = 16000;
    static constexpr int kBlock = 160;
    static constexpr int kPttTailMs = 300;   // keep recording this long after the key is released
    static constexpr int kMaxTailMs = 400;   // quiet audio kept after the last loud block
    static constexpr int kSplitGraceS = 3;   // a too-long phrase may run this much longer to reach a pause

    // Returns true when a finished phrase has been moved into `out`.
    // Optionally, `full` holds the same 10 ms at a higher sample rate (`fullCount` samples); it is cut
    // at exactly the same places and the matching phrase moved into `fullOut`.
    bool feed(const float* block, const GateParams& p, std::vector<int16_t>& out, const int16_t* full = nullptr,
              size_t fullCount = 0, std::vector<int16_t>* fullOut = nullptr) {
        double sum = 0;
        int16_t s[kBlock];
        for (int i = 0; i < kBlock; ++i) {
            float v = std::max(-1.f, std::min(1.f, block[i]));
            sum += double(v) * v;
            s[i] = int16_t(std::lrint(v * 32767.f));
        }
        db_ = float(10.0 * std::log10(sum / kBlock + 1e-10));

        // Word endings trail off below the opening threshold. Once a phrase is open, keep
        // counting audio as speech down to 10 dB lower, but never closer than 6 dB to the room noise.
        if (!open_ && !p.pushToTalk && db_ <= p.thresholdDb) {
            noiseDb_ = haveNoise_ ? noiseDb_ + 0.05f * (db_ - noiseDb_) : db_;
            haveNoise_ = true;
        }
        const float closeDb = haveNoise_ ? std::min(p.thresholdDb, std::max(p.thresholdDb - 10.f, noiseDb_ + 6.f))
                                         : p.thresholdDb;
        const bool loud = db_ > (open_ ? closeDb : p.thresholdDb);

        if (!open_) {
            const bool start = p.pushToTalk ? p.pttDown : loud;
            if (!start) {
                pre_.insert(pre_.end(), s, s + kBlock);
                const size_t keep = size_t(kRate) * (p.pushToTalk ? 150 : 300) / 1000;
                while (pre_.size() > keep) pre_.pop_front();
                if (full) {
                    preFull_.insert(preFull_.end(), full, full + fullCount);
                    while (preFull_.size() > keep / kBlock * fullCount) preFull_.pop_front();
                }
                return false;
            }
            open_ = true;
            cur_.assign(pre_.begin(), pre_.end());  // pre-roll so the first syllable isn't clipped
            pre_.clear();
            curFull_.assign(preFull_.begin(), preFull_.end());
            preFull_.clear();
            silentBlocks_ = voicedBlocks_ = heldBlocks_ = releasedBlocks_ = 0;
        }

        cur_.insert(cur_.end(), s, s + kBlock);
        if (full) curFull_.insert(curFull_.end(), full, full + fullCount);
        if (loud) { ++voicedBlocks_; silentBlocks_ = 0; } else { ++silentBlocks_; }
        if (p.pttDown) { ++heldBlocks_; releasedBlocks_ = 0; } else { ++releasedBlocks_; }

        const bool ended = p.pushToTalk ? releasedBlocks_ * 10 >= kPttTailMs : silentBlocks_ * 10 >= p.holdMs;
        // Too long: split at the next dip in level so a word isn't cut in half.
        const size_t soft = size_t(p.maxSeconds) * kRate, hard = soft + size_t(kSplitGraceS) * kRate;
        const bool tooLong = cur_.size() >= soft && (!loud || cur_.size() >= hard);
        if (!ended && !tooLong) return false;

        open_ = false;
        bool ok;
        if (p.pushToTalk) {
            ok = heldBlocks_ * 10 >= 250;  // ignore accidental taps
        } else {
            // keep the quiet tail (it often holds the end of the last word), drop only what exceeds it
            const size_t trailing = size_t(silentBlocks_) * kBlock;
            const size_t keep = size_t(kRate) * size_t(std::min(p.holdMs, kMaxTailMs)) / 1000;
            if (trailing > keep) {
                cur_.resize(cur_.size() - (trailing - keep));
                const size_t dropFull = (trailing - keep) / kBlock * fullCount;
                curFull_.resize(curFull_.size() > dropFull ? curFull_.size() - dropFull : 0);
            }
            ok = voicedBlocks_ * 10 >= 150;  // ignore clicks / coughs
        }
        if (ok) out.swap(cur_);
        if (ok && fullOut) fullOut->swap(curFull_);
        cur_.clear();
        curFull_.clear();
        return ok;
    }

    void reset() { open_ = false; cur_.clear(); pre_.clear(); curFull_.clear(); preFull_.clear(); }
    bool open() const { return open_; }
    float levelDb() const { return db_; }

private:
    std::vector<int16_t> cur_, curFull_;
    std::deque<int16_t> pre_, preFull_;
    bool open_ = false, haveNoise_ = false;
    int silentBlocks_ = 0, voicedBlocks_ = 0, heldBlocks_ = 0, releasedBlocks_ = 0;
    float db_ = -100.f, noiseDb_ = -100.f;
};

// 16-bit mono PCM -> in-memory WAV file.
inline std::string makeWav(const std::vector<int16_t>& pcm, uint32_t rate) {
    const uint32_t dataBytes = uint32_t(pcm.size() * 2);
    std::string w(44 + dataBytes, '\0');
    auto u32 = [&](size_t off, uint32_t v) { for (int i = 0; i < 4; ++i) w[off + i] = char((v >> (8 * i)) & 0xFF); };
    auto u16 = [&](size_t off, uint16_t v) { w[off] = char(v & 0xFF); w[off + 1] = char(v >> 8); };
    std::memcpy(&w[0], "RIFF", 4);  u32(4, 36 + dataBytes);
    std::memcpy(&w[8], "WAVEfmt ", 8);
    u32(16, 16); u16(20, 1); u16(22, 1); u32(24, rate); u32(28, rate * 2); u16(32, 2); u16(34, 16);
    std::memcpy(&w[36], "data", 4); u32(40, dataBytes);
    for (size_t i = 0; i < pcm.size(); ++i) u16(44 + i * 2, uint16_t(pcm[i]));
    return w;
}
