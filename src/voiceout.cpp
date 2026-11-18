// This file also compiles miniaudio itself (once for the whole program): its MP3 decoder is only
// visible here.
#define MINIAUDIO_IMPLEMENTATION
#include "audio.h"

#include "voiceout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

std::string outputFormatFor(int quality, int fallbackStep) {
    if (quality == kQualityBest) return "pcm_44100";
    if (quality == kQualityHigh) return fallbackStep > 0 ? "mp3_44100_128" : "mp3_44100_192";
    return "pcm_24000";
}

namespace {

// Windowed-sinc resampler (Blackman window, 32 taps each side). Keeps everything up to 95% of the
// lower rate's limit flat: linear interpolation would take ~3 dB off 15 kHz, which is exactly the
// detail the higher qualities are for.
class Resampler {
public:
    void setup(int inRate, int outRate) {
        step_ = double(inRate) / double(outRate);
        const double cutoff = 0.475 * std::min(1.0, 1.0 / step_);  // in cycles per input sample
        gain_ = 2.0 * cutoff;
        table_.resize(size_t(kTaps * 2 * kSub + 1));
        for (size_t i = 0; i < table_.size(); ++i) {
            const double d = double(i) / kSub - kTaps;  // distance in input samples
            const double x = 2.0 * cutoff * d;
            const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double w = 0.42 + 0.5 * std::cos(kPi * d / kTaps) + 0.08 * std::cos(2 * kPi * d / kTaps);
            table_[i] = float(gain_ * sinc * w);
        }
        hist_.assign(size_t(kTaps), 0.f);  // silence before the start, so nothing is lost at the front
        pos_ = 0;
    }
    template <class Out> void push(const int16_t* in, size_t n, Out&& out) {
        for (size_t i = 0; i < n; ++i) hist_.push_back(in[i] / 32768.f);
        // An output at input position p needs samples p-kTaps .. p+kTaps.
        while (pos_ + kTaps + 1 < double(hist_.size())) {
            const long centre = long(std::floor(pos_));
            const double frac = pos_ - double(centre);
            double acc = 0;
            for (long k = centre - kTaps + 1; k <= centre + kTaps; ++k) {
                if (k < 0) continue;
                const double t = (frac - double(k - centre) + kTaps) * kSub;  // table index for distance pos-k
                const size_t ti = size_t(t);
                if (ti + 1 >= table_.size()) continue;
                const double tf = t - double(ti);
                acc += hist_[size_t(k)] * (table_[ti] * (1 - tf) + table_[ti + 1] * tf);
            }
            out(float(acc));
            pos_ += step_;
        }
        // Drop what no future output needs.
        const long drop = long(std::floor(pos_)) - kTaps;
        if (drop > 4096) {
            hist_.erase(hist_.begin(), hist_.begin() + drop);
            pos_ -= double(drop);
        }
    }

private:
    static constexpr int kTaps = 32, kSub = 512;
    static constexpr double kPi = 3.14159265358979323846;
    double step_ = 1, gain_ = 1, pos_ = 0;
    std::vector<float> table_, hist_;
};

}  // namespace

struct VoiceOut::Impl {
    int outRate;
    bool mp3 = false;
    int inRate = 0;
    std::string pending;  // bytes not decoded yet (half a sample, or a partial MP3 frame)
    ma_dr_mp3dec dec;
    std::vector<int16_t> frame;  // one decoded MP3 frame
    Resampler resampler;
    bool convOn = false;
    std::vector<int16_t> out;

    explicit Impl(int rate) : outRate(rate) { frame.resize(MA_DR_MP3_MAX_SAMPLES_PER_FRAME); }
    ~Impl() { closeConv(); }

    void closeConv() { convOn = false; }

    bool openConv(int rate) {
        inRate = rate;
        convOn = rate != outRate && rate > 0;  // same rate: nothing to convert
        if (convOn) resampler.setup(rate, outRate);
        return true;
    }

    void emitResampled(const int16_t* in, size_t n, const Emit& emit) {
        if (n == 0) return;
        if (inRate == outRate || !convOn) {
            out.assign(in, in + n);
            emit(out.data(), out.size());
            return;
        }
        out.clear();
        resampler.push(in, n, [this](float v) {
            const long x = std::lround(double(v) * 32768.0);
            out.push_back(int16_t(std::max(-32768L, std::min(32767L, x))));
        });
        if (!out.empty()) emit(out.data(), out.size());
    }

    void decodeMp3(const Emit& emit, bool final) {
        size_t pos = 0;
        while (pos < pending.size()) {
            // Without the end of the stream, wait until a whole frame (at most ~1.4 kB) is in.
            if (!final && pending.size() - pos < 2048) break;
            ma_dr_mp3dec_frame_info info;
            std::memset(&info, 0, sizeof(info));
            const int samples = ma_dr_mp3dec_decode_frame(&dec, reinterpret_cast<const ma_uint8*>(pending.data() + pos),
                                                          int(pending.size() - pos), frame.data(), &info);
            if (info.frame_bytes <= 0) break;  // needs more data
            pos += size_t(info.frame_bytes);
            if (samples <= 0) continue;  // ID3 tag or a skipped frame
            if (info.hz != inRate) openConv(info.hz);
            if (info.channels == 2) {  // ElevenLabs sends mono, but just in case: mix down
                for (int i = 0; i < samples; ++i) frame[size_t(i)] = int16_t((int(frame[size_t(2 * i)]) + frame[size_t(2 * i + 1)]) / 2);
            }
            emitResampled(frame.data(), size_t(samples), emit);
        }
        pending.erase(0, pos);
    }
};

VoiceOut::VoiceOut(int playbackRate) : p_(new Impl(playbackRate)) {}
VoiceOut::~VoiceOut() = default;

void VoiceOut::begin(const std::string& format) {
    p_->pending.clear();
    p_->mp3 = format.compare(0, 4, "mp3_") == 0;
    int rate = 24000;
    const size_t a = format.find('_');
    if (a != std::string::npos) rate = std::atoi(format.c_str() + a + 1);
    if (p_->mp3) ma_dr_mp3dec_init(&p_->dec);
    p_->openConv(rate > 0 ? rate : 24000);
}

void VoiceOut::push(const char* bytes, size_t count, const Emit& emit) {
    p_->pending.append(bytes, count);
    if (p_->mp3) {
        p_->decodeMp3(emit, false);
        return;
    }
    const size_t samples = p_->pending.size() / 2;  // a chunk may end in the middle of a sample
    std::vector<int16_t> pcm(samples);
    std::memcpy(pcm.data(), p_->pending.data(), samples * 2);
    p_->pending.erase(0, samples * 2);
    p_->emitResampled(pcm.data(), samples, emit);
}

void VoiceOut::finish(const Emit& emit) {
    if (p_->mp3) p_->decodeMp3(emit, true);
    p_->pending.clear();
    // Push a little silence through the resampler so its last few samples come out too.
    if (p_->convOn) {
        std::vector<int16_t> zeros(40, 0);
        p_->emitResampled(zeros.data(), zeros.size(), emit);
    }
}
