#include "listen.h"
#include "applog.h"
#include "emotion.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "appaudio.h"
#include "audio.h"
#include "cleansound.h"
#include "core.h"
#include "eleven.h"
#include "extras.h"
#include "focus.h"
#include "livestt.h"
#include "osc.h"
#include "translate.h"
#include "voiceid.h"

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr int kWorkers = 2;          // phrases are transcribed side by side, shown in spoken order
constexpr size_t kMaxWaiting = 3;    // more than this waiting = falling behind: drop the oldest
constexpr size_t kMaxCaptions = 200;
constexpr int kChatboxGapMs = 1600;  // VRChat mutes the chatbox when it is written to too often

// ---- "Only this language". "They speak" tells the speech service which language to write the
// words down in; it doesn't check that they are in it (Chinese written down as Arabic is nonsense,
// and it got translated). With "Only this language" the service says what it heard instead, and
// speech that is clearly another language gets no caption.

// Languages written with the same letters: the words alone can't tell them apart ("ar" is the guess
// for any Arabic-script text), so a guess inside the same group is no proof of another language.
bool sameLetters(const Language* a, const Language* b) {
    if (!a || !b) return false;
    static const char* const kGroups[][8] = {
        {"ar", "fa", "ur", "ps", "ku", "sd", "ug", nullptr},
        {"ru", "uk", "bg", "sr", "be", "kk", "mk", nullptr},
        {"zh", "yue", nullptr},
        {"hi", "mr", "ne", nullptr},
    };
    for (const auto& group : kGroups) {
        bool hasA = false, hasB = false;
        for (const char* const* c = group; *c; ++c) {
            hasA = hasA || std::strncmp(a->code, *c, std::strlen(*c)) == 0;
            hasB = hasB || std::strncmp(b->code, *c, std::strlen(*c)) == 0;
        }
        if (hasA && hasB) return true;
    }
    return false;
}

// The language these words are clearly in, when that is not `only` (another script, or another
// language's everyday words). Null when they could be `only`, or it can't be told.
const Language* clearlyOther(const Language* only, const std::string& text) {
    if (!only) return nullptr;
    bool clear = false;
    const Language* guess = findLanguage(guessLanguage(text, &clear));
    return clear && guess && guess != only && !sameLetters(guess, only) ? guess : nullptr;
}

// Languages that are written in their own letters, not the Latin ones.
bool ownLetters(const Language* l) {
    if (!l) return false;
    static const char* const kOwn[] = {"ar", "fa", "ur", "ps", "he", "yi", "ru", "uk", "bg", "be", "mk", "kk", "ky", "tg", "mn",
                                       "el", "hi", "mr", "ne", "bn", "ta", "te", "kn", "ml", "gu", "pa", "si", "th", "lo", "km",
                                       "my", "ka", "hy", "am", "zh", "yue", "ja", "ko"};
    for (const char* c : kOwn)
        if (std::strcmp(l->code, c) == 0) return true;
    return false;
}

// Words written only in Latin letters (punctuation, digits and accents aside): at least one letter,
// and nothing from another alphabet.
bool latinOnly(const std::string& text) {
    bool letter = false;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned cp = c;
        int len = 1;
        if (c >= 0xF0) { cp = c & 0x07; len = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; len = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; len = 2; }
        for (int k = 1; k < len && i + size_t(k) < text.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + size_t(k)]) & 0x3F);
        i += size_t(len);
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0x24F)) letter = true;
        else if (cp >= 0x370 && !(cp >= 0x2000 && cp <= 0x206F)) return false;  // another alphabet (general punctuation aside)
    }
    return letter;
}

// The language to caption alone, or null when "Only this language" is off.
const Language* onlyLanguage(const ListenSettings& s) {
    return s.onlyFromLang && !s.fromLang.empty() ? findLanguage(s.fromLang) : nullptr;
}

// ---- Cleaner sound for the speech service and the voice fingerprints.

// Takes out the rumble under a voice (below about 90 Hz: a boomy mic, a fan, a game's bass). Speech
// has nothing down there, and it's what makes a bassy mic hard to write down and to recognise.
struct RumbleCut {
    // A second-order high-pass at 90 Hz for 16 kHz sound.
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void run(float* s, int n) {
        constexpr float b0 = 0.975319f, b1 = -1.950638f, b2 = 0.975319f, a1 = -1.950027f, a2 = 0.951246f;
        for (int i = 0; i < n; ++i) {
            const float x = s[i];
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            s[i] = y;
        }
        if (std::fabs(y1) < 1e-20f) y1 = y2 = 0;  // no denormals in long silences
    }
};

// A quiet phrase (someone far away, a low mic) is brought up to an ordinary level before it is
// written down: at most six times louder, and never so far that it clips. A phrase that is loud
// enough already is left exactly as it is.
void evenLevel(std::vector<int16_t>& pcm) {
    if (pcm.size() < 1600) return;
    std::vector<int> mags;
    mags.reserve(pcm.size() / 4 + 1);
    for (size_t i = 0; i < pcm.size(); i += 4) mags.push_back(std::abs(int(pcm[i])));
    const size_t at = mags.size() - 1 - mags.size() / 200;  // the level only a two-hundredth of it is above: clicks don't count
    std::nth_element(mags.begin(), mags.begin() + long(at), mags.end());
    const float top = float(mags[at]);
    if (top < 60.f) return;  // silence: nothing to bring up
    const float gain = std::min(6.f, 16000.f / top);
    if (gain <= 1.15f) return;
    for (int16_t& v : pcm) v = int16_t(std::max(-32767.f, std::min(32767.f, float(v) * gain)));
}

struct Job {
    uint64_t id = 0;
    std::vector<int16_t> pcm;
    std::vector<int16_t> pan;  // per 10 ms: where the sound came from, -32767 left .. 32767 right
    Clock::time_point heard;
    std::shared_future<LiveText> live;  // its words from the live connection, when it was streamed
};

// The ring holds pairs: the sound (left and right together) and half their difference. Kept in one
// ring so the two can never drift apart. Returns how many frames fitted.
size_t pushPairs(SpscRing<float>& ring, const float* frames, size_t count, int channels) {
    static thread_local std::vector<float> pairs;
    pairs.resize(count * 2);
    for (size_t i = 0; i < count; ++i) {
        const float l = frames[i * size_t(channels)], r = channels > 1 ? frames[i * size_t(channels) + 1] : l;
        pairs[i * 2] = 0.5f * (l + r);
        pairs[i * 2 + 1] = 0.5f * (l - r);
    }
    const size_t room = ring.space() / 2;
    const size_t n = std::min(count, room);
    ring.push(pairs.data(), n * 2);
    return n;
}

// Where 10 ms of sound came from: -1 all left, +1 all right, 0 the middle (or mono).
float panOf(const float* mid, const float* side, size_t n) {
    double el = 0, er = 0;
    for (size_t i = 0; i < n; ++i) {
        const double l = double(mid[i]) + side[i], r = double(mid[i]) - side[i];
        el += l * l;
        er += r * r;
    }
    return el + er < 1e-9 ? 0.f : float((er - el) / (er + el));
}

struct Pace { int holdMs, maxSeconds; };
Pace paceOf(int pace) {
    if (pace == kPaceQuick) return {300, 4};
    if (pace == kPaceFull) return {700, 12};
    return {450, 7};
}

std::string clockNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[8];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    return buf;
}

int msSince(Clock::time_point t) { return int(std::chrono::duration_cast<milliseconds>(Clock::now() - t).count()); }

bool sameNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

}  // namespace

struct Listener::Impl {
    // ---- shared, guarded by mtx
    mutable std::mutex mtx;
    ListenConfig cfg;
    std::deque<Job> jobs;
    std::deque<Caption> feed;
    uint64_t nextId = 1;
    // What the listener said lately: the text, the caption it came before, and when.
    struct Mine { std::string text; uint64_t beforeId; Clock::time_point when; };
    std::deque<Mine> mine;
    int warm = 0;  // workers that should open their connections now
    std::string status, error, chatText;
    std::condition_variable cv;

    // Voices heard this session, to keep the same colour for the same person across captions.
    VoiceBook voices;                 // guarded by mtx
    // "Only this language": how often each voice was clearly heard in that language and in another
    // one (mtx). A line too short to tell the language from goes by who said it.
    struct VoiceLang { int wanted = 0, other = 0; };
    std::map<int, VoiceLang> voiceLangs;
    std::string voiceLangsFor;        // the language those counts are about
    // Captions while they talk: the caption being spoken and the words so far.
    uint64_t liveId = 0;              // guarded by mtx; 0 = none
    std::thread liveThread;           // one transcription of the unfinished phrase at a time
    std::atomic<bool> liveBusy{false};
    // The same, streamed (LiveStt): their voice goes to the speech service while they talk, so the
    // words arrive within a moment and nothing is uploaded twice. Each sentence is translated as soon
    // as it's finished, while they go on talking; when the phrase ends, its own words come straight
    // back and the whole caption is translated at once. The regular pass (who said what, voices,
    // sounds) then replaces it. If streaming isn't available, the looks every 1.5 s are used.
    std::string liveHost;                       // tests only
    std::unique_ptr<LiveStt> liveStt;           // made by start()
    bool liveThis = false;                      // capThread: this phrase is being streamed
    size_t liveSent = 0;                        // capThread: how much of it has gone
    std::atomic<bool> liveAccept{false};        // words arriving now belong to the phrase being spoken
    std::atomic<long long> liveBeganMs{0};
    std::string liveText;                       // mtx: the words so far (or the finished phrase's)
    bool liveDone = false;                      // mtx: ...and the phrase has ended
    uint64_t liveFor = 0, liveSeq = 0;          // mtx: the caption they belong to; counts changes
    std::map<std::string, std::string> liveSaid;  // mtx: sentence -> its translation, for liveFor
    // mtx: when each finished sentence was first seen with exactly these words, for liveFor. The live
    // words are corrected as more is heard, and each corrected sentence would be a new request: one is
    // asked only once its words have stayed the same for a moment (or the phrase has ended).
    std::map<std::string, long long> liveSeen;
    static constexpr long long kSettledMs = 500;
    struct LiveEnd { uint64_t id; std::shared_future<LiveText> text; };
    std::deque<LiveEnd> liveEnds;               // mtx: finished phrases whose own words are on their way
    std::condition_variable liveCv;
    std::thread liveTrThread;
    std::atomic<int> lastSttMs{0}, lastTrMs{0};

    std::atomic<uint64_t> version{1};
    std::atomic<bool> running{false}, cancel{false}, hearing{false};
    std::atomic<int> busy{0}, lastMs{0};
    std::atomic<float> levelDb{-100.f};

    SpscRing<float> ring{16000 * 10 * 2};  // pairs: see pushPairs
    std::thread capThread;
    std::vector<std::thread> workers;

    // ---- audio source, touched by capThread only (pushPcm: by the one thread feeding it)
    ma_context ctx;
    bool ctxOk = false;
    ma_device dev;
    bool devOn = false;
    AppCapture app;
    uint32_t appPid = 0;
    ma_data_converter conv;
    bool convOn = false;
    int convChannels = 0, convRate = 0;
    std::vector<float> convOut;

    void setStatus(const std::string& s, const std::string& e) {
        std::lock_guard<std::mutex> lk(mtx);
        status = s;
        error = e;
    }

    // ---------------------------------------------------------------- sources

    static void onDeviceAudio(ma_device* d, void*, const void* in, ma_uint32 frames) {
        if (in) pushPairs(static_cast<Impl*>(d->pUserData)->ring, static_cast<const float*>(in), frames, 2);
    }

    bool openDevice(bool loopback, const std::string& name, std::string& err) {
        if (!ctxOk) { err = "Couldn't start the audio system."; return false; }
        struct Find {
            const std::string* name;
            ma_device_type type;
            ma_device_id id;
            bool found;
        } find = {&name, loopback ? ma_device_type_playback : ma_device_type_capture, {}, false};
        if (!name.empty()) {
            ma_context_enumerate_devices(
                &ctx,
                [](ma_context*, ma_device_type type, const ma_device_info* info, void* user) -> ma_bool32 {
                    Find* f = static_cast<Find*>(user);
                    if (type != f->type || *f->name != info->name) return MA_TRUE;
                    f->id = info->id;
                    f->found = true;
                    return MA_FALSE;
                },
                &find);
        }
        ma_device_config c = ma_device_config_init(loopback ? ma_device_type_loopback : ma_device_type_capture);
        c.capture.pDeviceID = find.found ? &find.id : nullptr;  // not found (unplugged): system default
        c.capture.format = ma_format_f32;
        c.capture.channels = 2;  // stereo: where a voice comes from helps tell people apart (a mono device is copied to both)
        c.sampleRate = Segmenter::kRate;  // miniaudio converts from the device's own format
        c.dataCallback = onDeviceAudio;
        c.pUserData = this;
        if (ma_device_init(&ctx, &c, &dev) != MA_SUCCESS) {
            err = loopback ? "Couldn't listen to that playback device. Listening to speakers needs Windows."
                           : "Couldn't open that input device. Pick another one.";
            return false;
        }
        if (ma_device_start(&dev) != MA_SUCCESS) {
            ma_device_uninit(&dev);
            err = "That device wouldn't start. Pick another one.";
            return false;
        }
        devOn = true;
        return true;
    }

    void closeSource() {
        if (devOn) { ma_device_uninit(&dev); devOn = false; }
        app.stop();
        appPid = 0;
    }

    void pushPcm(const int16_t* in, size_t frames, int channels, int rate) {
        if (channels <= 0 || rate <= 0) return;
        if (!convOn || channels != convChannels || rate != convRate) {
            if (convOn) ma_data_converter_uninit(&conv, nullptr);
            ma_data_converter_config c = ma_data_converter_config_init(ma_format_s16, ma_format_f32, ma_uint32(channels), 2,
                                                                       ma_uint32(rate), Segmenter::kRate);
            convOn = ma_data_converter_init(&c, nullptr, &conv) == MA_SUCCESS;
            convChannels = channels;
            convRate = rate;
            if (!convOn) return;
        }
        convOut.resize(4096 * 2);
        while (frames > 0) {
            ma_uint64 nIn = frames, nOut = convOut.size() / 2;
            if (ma_data_converter_process_pcm_frames(&conv, in, &nIn, convOut.data(), &nOut) != MA_SUCCESS) return;
            pushPairs(ring, convOut.data(), size_t(nOut), 2);
            in += nIn * size_t(channels);
            frames -= size_t(nIn);
            if (nIn == 0 && nOut == 0) return;
        }
    }

    // ---------------------------------------------------------------- phrases in

    void dropPending() {  // mtx held
        jobs.clear();
        for (size_t i = feed.size(); i-- > 0;)
            if (feed[i].pending) feed.erase(feed.begin() + long(i));
        ++version;
    }

    void eraseCaption(uint64_t id) {  // mtx held
        for (size_t i = 0; i < feed.size(); ++i)
            if (feed[i].id == id) { feed.erase(feed.begin() + long(i)); ++version; return; }
    }

    uint64_t enqueue(std::vector<int16_t>&& pcm, std::vector<int16_t>&& pan, std::shared_future<LiveText> words = {}) {
        std::lock_guard<std::mutex> lk(mtx);
        while (jobs.size() >= kMaxWaiting) {  // stay live rather than fall further behind
            eraseCaption(jobs.front().id);
            jobs.pop_front();
        }
        Job j;
        j.pcm = std::move(pcm);
        j.pan = std::move(pan);
        j.heard = Clock::now();
        j.live = words;
        if (liveId != 0) {  // the words shown while they talked become this caption
            j.id = liveId;
            liveId = 0;
            const uint64_t id = j.id;
            jobs.push_back(std::move(j));
            ++version;
            cv.notify_one();
            return id;
        }
        j.id = nextId++;
        Caption c;
        c.id = j.id;
        c.time = clockNow();
        feed.push_back(std::move(c));
        while (feed.size() > kMaxCaptions) feed.pop_front();
        const uint64_t id = j.id;
        jobs.push_back(std::move(j));
        ++version;
        cv.notify_one();
        return id;
    }

    void captureLoop() {
        Segmenter seg;
        OscOut osc;
        float block[Segmenter::kBlock], side[Segmenter::kBlock], pairs[Segmenter::kBlock * 2];
        float ahead[Segmenter::kBlock];  // the block with everything off to the sides turned down (focus: in front)
        FrontFilter frontFilter;
        RumbleCut rumbleMid, rumbleSide;
        NearGate nearGate;  // ("near" and "far" are taken by the Windows headers)
        int focusWas = kFocusAll;
        double rawEnergy = 0, aheadEnergy = 0;  // smoothed over about 100 ms
        const float silence[Segmenter::kBlock] = {};
        int kind = -1;             // what is open right now (-1 = nothing)
        std::string name;
        bool wasOn = false, wasOpen = false, sourceOk = false;
        auto lastAudio = Clock::now(), lastSilence = lastAudio, lastChat = lastAudio - milliseconds(kChatboxGapMs);
        auto nextTry = lastAudio, nextAppCheck = lastAudio;

        while (running) {
            ListenSettings s;
            std::string chat;
            {
                std::lock_guard<std::mutex> lk(mtx);
                s = cfg.s;
                if (!chatText.empty() && msSince(lastChat) >= kChatboxGapMs) chat.swap(chatText);
            }
            if (!chat.empty()) {
                osc.chatbox(chat);
                lastChat = Clock::now();
            }

            // ---- switched off
            if (!s.on) {
                if (wasOn) {
                    closeSource();
                    kind = -1;
                    seg.reset();
                    ring.clear();
                    hearing = false;
                    levelDb = -100.f;
                    std::lock_guard<std::mutex> lk(mtx);
                    dropPending();
                    chatText.clear();
                    status = "Listen is off.";
                    error.clear();
                }
                wasOn = wasOpen = false;
                std::this_thread::sleep_for(milliseconds(40));
                continue;
            }

            // ---- keep the right source open
            const std::string wantName = s.source == kSrcApp ? s.app : s.source == kSrcSpeakers ? s.speakers
                                         : s.source == kSrcInput ? s.input : std::string();
            const auto now = Clock::now();
            if (!wasOn || kind != s.source || name != wantName) {
                closeSource();
                seg.reset();
                ring.clear();
                kind = s.source;
                name = wantName;
                sourceOk = false;
                nextTry = nextAppCheck = now;
                wasOn = true;
            }
            if (kind == kSrcApp) {
                if (now >= nextAppCheck) {
                    nextAppCheck = now + milliseconds(1000);
                    if (appPid != 0 && (!app.running() || !sameNoCase(exeOfPid(appPid), name))) {
                        app.stop();  // the program closed
                        appPid = 0;
                        sourceOk = false;
                    }
                    if (appPid == 0) {
                        const uint32_t pid = name.empty() ? 0 : findAppPid(name);
                        std::string err;
                        if (name.empty()) {
                            setStatus(kAppCaptureSupported ? "Pick an application to listen to."
                                                           : "Listening to one application needs Windows.", "");
                        } else if (pid == 0) {
                            setStatus("Waiting for " + name + " to start.", "");
                        } else if (app.start(pid, [this](const int16_t* f, size_t n, int ch, int rate) { pushPcm(f, n, ch, rate); }, err)) {
                            appPid = pid;
                            sourceOk = true;
                            setStatus("Listening to " + name + ".", "");
                        } else {
                            setStatus("", err);
                            nextAppCheck = now + milliseconds(5000);
                        }
                    }
                }
            } else if (!sourceOk && now >= nextTry) {
                nextTry = now + milliseconds(3000);
                std::string err;
                if (kind == kSrcExternal) {
                    sourceOk = true;
                    setStatus("Listening.", "");
                } else if (openDevice(kind == kSrcSpeakers, name, err)) {
                    sourceOk = true;
                    const std::string shown = name.empty() ? (kind == kSrcSpeakers ? "the default speakers" : "the default input")
                                                           : name;
                    setStatus((kind == kSrcSpeakers ? "Listening to everything playing on " : "Listening to ") + shown + ".", "");
                } else {
                    setStatus("", err);
                }
            }

            // ---- next 10 ms of audio. A silent program or playback device delivers nothing at all,
            // so after a short wait the gap is filled with silence to let an open phrase finish.
            const float* in = nullptr;
            int16_t pan = 0;
            if (ring.size() >= size_t(Segmenter::kBlock) * 2) {
                ring.pop(pairs, Segmenter::kBlock * 2);
                for (int i = 0; i < Segmenter::kBlock; ++i) {
                    block[i] = pairs[i * 2];
                    side[i] = pairs[i * 2 + 1];
                }
                rumbleMid.run(block, Segmenter::kBlock);
                rumbleSide.run(side, Segmenter::kBlock);
                pan = int16_t(std::lrint(panOf(block, side, Segmenter::kBlock) * 32767.f));
                in = block;
                lastAudio = lastSilence = Clock::now();
            } else if (msSince(lastAudio) > 150 && msSince(lastSilence) >= 10) {
                lastSilence += milliseconds(10);
                if (msSince(lastSilence) > 500) lastSilence = Clock::now();
                in = silence;
            } else {
                std::this_thread::sleep_for(milliseconds(4));
                continue;
            }

            // In a crowd: only the voice in front, or only the loudest (closest) voices.
            if (s.focus != focusWas) {
                frontFilter.reset();
                nearGate.reset();
                rawEnergy = aheadEnergy = 0;
                focusWas = s.focus;
            }
            if (s.focus == kFocusFront) {
                const float* raw = in;
                frontFilter.process(in == silence ? silence : block, in == silence ? silence : side, ahead, Segmenter::kBlock);
                in = ahead;
                // When what's left is under a tenth of what came in, nobody in front is talking: it's
                // someone at the side, turned down. That counts as silence, so it opens no caption.
                // (A voice in front under a louder one from the side keeps well over a tenth.)
                double er = 0, ea = 0;
                for (int i = 0; i < Segmenter::kBlock; ++i) {
                    er += double(raw[i]) * raw[i];
                    ea += double(ahead[i]) * ahead[i];
                }
                rawEnergy = 0.9 * rawEnergy + 0.1 * er;
                aheadEnergy = 0.9 * aheadEnergy + 0.1 * ea;
                if (aheadEnergy < 0.1 * rawEnergy) in = silence;
            }
            GateParams g;
            const Pace pace = paceOf(s.pace);
            g.thresholdDb = s.thresholdDb;
            if (s.focus != kFocusAll) {
                double sum = 0;
                for (int i = 0; i < Segmenter::kBlock; ++i) sum += double(in[i]) * in[i];
                g.thresholdDb = nearGate.threshold(s.thresholdDb, float(10.0 * std::log10(sum / Segmenter::kBlock + 1e-10)));
            }
            g.holdMs = pace.holdMs;
            g.maxSeconds = pace.maxSeconds;
            std::vector<int16_t> phrase, phrasePan;
            const bool done = seg.feed(in, g, phrase, &pan, 1, &phrasePan);
            levelDb = seg.levelDb();
            const bool open = seg.open();
            hearing = open;
            // Captions while they talk, streamed: begin when a phrase opens (also right after a long
            // one was cut), send what's new every block, hand it in when it ends.
            if (open && !liveThis && !done) {
                bool want;
                LiveStt::Options o;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    want = cfg.liveCaptions && !cfg.apiKey.empty();
                    o.apiKey = cfg.apiKey;
                    o.language = onlyLanguage(cfg.s) ? std::string() : cfg.s.fromLang;  // only this language: it says what it heard
                }
                if (want && liveStt && liveStt->available()) {
                    liveStt->begin(o);
                    liveThis = true;
                    liveSent = 0;
                    liveBeganMs = std::chrono::duration_cast<milliseconds>(Clock::now().time_since_epoch()).count();
                    liveAccept = true;
                }
            }
            if (liveThis && open && !done) {
                const std::vector<int16_t>& sofar = seg.current();
                if (sofar.size() < liveSent) liveSent = 0;
                if (sofar.size() > liveSent) {
                    liveStt->audio(sofar.data() + liveSent, sofar.size() - liveSent);
                    liveSent = sofar.size();
                }
            }
            if (open && !done && !liveThis) showLiveWords(seg.current());
            if (done) {
                std::shared_future<LiveText> words;
                if (liveThis) {
                    if (phrase.size() > liveSent) liveStt->audio(phrase.data() + liveSent, phrase.size() - liveSent);
                    liveAccept = false;
                    words = liveStt->commit();
                    liveThis = false;
                }
                evenLevel(phrase);
                const uint64_t id = enqueue(std::move(phrase), std::move(phrasePan), words);
                if (words.valid()) {
                    std::lock_guard<std::mutex> lk(mtx);
                    liveEnds.push_back({id, words});
                    liveCv.notify_one();
                }
            } else if (liveThis && !open) {
                liveAccept = false;
                liveStt->discard();  // too short to be a phrase (a click, a cough)
                liveThis = false;
                std::lock_guard<std::mutex> lk(mtx);
                if (liveId != 0) {  // its first words were already showing
                    eraseCaption(liveId);
                    liveId = 0;
                }
            }
            if (!done && open && !wasOpen) {
                std::lock_guard<std::mutex> lk(mtx);
                warm = kWorkers;  // someone started talking: connect while they are still speaking
                cv.notify_all();
            }
            wasOpen = open;
        }
        closeSource();
    }

    // Captions while they talk: every ~1.5 s of new speech, the unfinished phrase is transcribed (not
    // translated) and shown as it is. The finished phrase replaces it as usual.
    size_t liveFrom = 0;  // capture thread only: phrase length at the last look
    void showLiveWords(const std::vector<int16_t>& sofar) {
        ListenConfig c;
        {
            std::lock_guard<std::mutex> lk(mtx);
            c = cfg;
        }
        if (!c.liveCaptions || c.apiKey.empty()) return;
        if (sofar.size() < liveFrom) liveFrom = 0;  // a new phrase
        if (sofar.size() < size_t(Segmenter::kRate) * 3 / 2 || sofar.size() - liveFrom < size_t(Segmenter::kRate) * 3 / 2) return;
        if (liveBusy.exchange(true)) return;
        liveFrom = sofar.size();
        if (liveThread.joinable()) liveThread.join();
        uint64_t id;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (liveId == 0) {
                Caption cap;
                cap.id = liveId = nextId++;
                cap.time = clockNow();
                cap.live = true;
                feed.push_back(std::move(cap));
                ++version;
            }
            id = liveId;
        }
        liveThread = std::thread([this, pcm = sofar, c, id] {
            Eleven api;
            api.apiKey = c.apiKey;
            api.http.cancel = &cancel;
            api.useFeature = kUseLive;
            std::string text, lang, err;
            const Language* only = onlyLanguage(c.s);
            if (api.transcribe(pcm, only ? std::string() : c.s.fromLang, text, lang, err) && !text.empty() && !clearlyOther(only, text)) {
                text = applyWordFixes(text, c.words);
                if (c.maskSwears) text = maskSwears(text);
                std::lock_guard<std::mutex> lk(mtx);
                for (Caption& cap : feed)
                    if (cap.id == id && cap.pending) {  // still being spoken or translated: show the words so far
                        cap.live = true;
                        cap.lines.assign(1, CaptionLine());
                        cap.lines[0].text = text;
                        ++version;
                    }
            }
            liveBusy = false;
        });
    }

    // ---- streamed live words (see the members above)

    // The words so far with every finished sentence that has been translated in its translation. mtx held.
    std::string liveLine() const {
        bool open = false;
        const std::vector<std::string> parts = sentencesOf(liveText, open);
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            const bool finished = liveDone || i + 1 < parts.size() || !open;
            const auto it = finished ? liveSaid.find(parts[i]) : liveSaid.end();
            if (!out.empty()) out += ' ';
            out += it != liveSaid.end() && !it->second.empty() ? it->second : parts[i];
        }
        return out;
    }
    void showLive() {  // mtx held
        for (Caption& cap : feed)
            if (cap.id == liveFor && cap.pending) {  // still being spoken, or waiting for the regular pass
                cap.live = true;
                if (clearlyOther(onlyLanguage(cfg.s), liveText)) {  // only this language: another one isn't shown
                    cap.lines.clear();
                    ++version;
                    continue;
                }
                cap.lines.assign(1, CaptionLine());
                cap.lines[0].text = cfg.maskSwears ? maskSwears(liveLine()) : liveLine();
                ++version;
            }
    }
    // Words from the speech service while they talk (its thread).
    void onLiveWords(const std::string& words) {
        const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        if (!liveAccept || nowMs - liveBeganMs < 250 || words.empty()) return;  // not the last phrase's stragglers
        std::lock_guard<std::mutex> lk(mtx);
        if (liveId == 0) {
            Caption cap;
            cap.id = liveId = nextId++;
            cap.time = clockNow();
            cap.live = true;
            feed.push_back(std::move(cap));
        }
        if (liveFor != liveId) {
            liveFor = liveId;
            liveSaid.clear();
            liveSeen.clear();
        }
        liveText = applyWordFixes(words, cfg.words);
        liveDone = false;
        ++liveSeq;
        showLive();
        liveCv.notify_one();
    }
    // Translates the finished sentences that haven't been yet, in one request, and shows them.
    // True when a finished sentence is still waiting for its words to settle (look again shortly).
    bool translateLive(uint64_t id, const std::string& text, bool done, const ListenConfig& c, Translator& tr) {
        const Language* to = findLanguage(c.s.toLang);
        // Only with an AI service: the others count every character, and this says each sentence twice.
        if (!to || !c.liveTranslate || !isAiService(c.translate.service)) return false;
        bool open = false;
        const std::vector<std::string> parts = sentencesOf(text, open);
        std::vector<std::string> todo;
        std::vector<const Language*> froms;
        bool settling = false;
        // The whole phrase so far says more about its language than one short sentence of it ("No way."
        // alone looks Spanish, "Hold on." French). A sentence whose own words can't settle it goes by
        // the phrase when the phrase is clearly in the reader's language: it isn't sent to be translated
        // into the language it's already in.
        // (Also with "They speak" picked: what someone says in the reader's language isn't in that one.)
        bool phraseSure = false;
        const Language* phrase = findLanguage(guessLanguage(text, &phraseSure));
        const bool phraseIsReaders = phrase == to && phraseSure;
        {
            const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
            std::lock_guard<std::mutex> lk(mtx);
            if (liveFor != id) return false;
            for (size_t i = 0; i < parts.size(); ++i) {
                const bool finished = done || i + 1 < parts.size() || !open;
                if (!finished || liveSaid.count(parts[i])) continue;
                // While they're still talking, a sentence is asked about only once its words have
                // stayed exactly the same for a moment: the live words are still being corrected, and
                // each corrected version would be another request. When the phrase ends, at once.
                if (!done) {
                    const auto seenAt = liveSeen.emplace(parts[i], nowMs).first;
                    if (nowMs - seenAt->second < kSettledMs) {
                        settling = true;
                        continue;
                    }
                }
                bool sure = false;
                const Language* guessed = findLanguage(guessLanguage(parts[i], &sure));
                const Language* only = onlyLanguage(c.s);
                const Language* from = c.s.fromLang.empty() ? guessed : findLanguage(c.s.fromLang);
                if (clearlyOther(only, parts[i])) {
                    liveSaid[parts[i]] = parts[i];  // only this language: not translated (and not shown, see showLive)
                    continue;
                }
                const bool notPicked = !c.s.fromLang.empty() && ownLetters(from) && !ownLetters(to) && latinOnly(parts[i]);
                if (from == to || (guessed == to && sure) || (!sure && phraseIsReaders) || (guessed == to && notPicked)) {
                    liveSaid[parts[i]] = parts[i];  // already in the reader's language
                    continue;
                }
                // (Too few words to tell the language: still asked, as before. Short lines in another
                // language are often like that, and they'd go untranslated until the end.)
                todo.push_back(parts[i]);
                froms.push_back(from);
            }
        }
        if (todo.empty()) return settling;
        TranslateSetup setup = c.translate;
        setup.faithful = true;
        setup.purpose = kForLiveSentence;
        std::vector<std::string> outs;
        std::vector<int> genders;
        std::string why;
        const std::vector<int> voices(todo.size(), 0);
        const bool ok = tr.translateLines(setup, todo, froms, voices, *to, outs, genders, why);
        std::lock_guard<std::mutex> lk(mtx);
        if (liveFor != id) return false;
        for (size_t k = 0; k < todo.size(); ++k)
            liveSaid[todo[k]] = ok && k < outs.size() ? outs[k] : std::string();  // empty: shown as said, not asked again
        showLive();
        return settling;
    }
    void liveTranslateLoop() {
        Translator tr;
        tr.http.cancel = &cancel;
        uint64_t seen = 0;
        bool settling = false;  // a finished sentence is waiting for its words to settle: look again soon
        for (;;) {
            uint64_t id = 0;
            std::string text;
            bool done = false;
            ListenConfig c;
            std::shared_future<LiveText> ending;
            {
                std::unique_lock<std::mutex> lk(mtx);
                const auto ready = [&] { return !running || liveSeq != seen || !liveEnds.empty(); };
                if (settling) liveCv.wait_for(lk, std::chrono::milliseconds(150), ready);
                else liveCv.wait(lk, ready);
                if (!running) return;
                c = cfg;
                if (!liveEnds.empty()) {  // a phrase ended: its own words first
                    id = liveEnds.front().id;
                    ending = liveEnds.front().text;
                    liveEnds.pop_front();
                } else {
                    seen = liveSeq;
                    id = liveFor;
                    text = liveText;
                    done = liveDone;
                }
            }
            if (ending.valid()) {
                if (ending.wait_for(std::chrono::milliseconds(1500)) != std::future_status::ready) continue;
                const LiveText t = ending.get();
                if (!t.ok || t.text.empty()) continue;
                std::lock_guard<std::mutex> lk(mtx);
                if (liveFor != id) {
                    liveFor = id;
                    liveSaid.clear();
                    liveSeen.clear();
                }
                liveText = text = applyWordFixes(t.text, c.words);
                liveDone = done = true;
                seen = ++liveSeq;
                showLive();
            }
            if (cancel) {
                settling = false;
                continue;
            }
            settling = translateLive(id, text, done, c, tr);
        }
    }

    // Which voice this is, among the ones heard this session (a best guess; see VoiceBook). mtx held.
    // Lines already in the feed follow when a voice gets its number or two turn out to be one.
    int voiceIdOf(const VoicePrint& p) {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::vector<std::pair<int, int>> renamed;
        const int id = voices.who(p, now, cfg.keepVoices, renamed);
        for (const auto& r : renamed) {
            for (Caption& c : feed)
                for (CaptionLine& l : c.lines)
                    if (l.voiceId == r.first) l.voiceId = r.second;
            // What they've been heard speaking goes with them. A voice that only now gets a number
            // takes the number over as its own (whoever had it before has left); two numbers that
            // were one person add up.
            const auto was = voiceLangs.find(r.first);
            VoiceLang moved = was != voiceLangs.end() ? was->second : VoiceLang();
            if (was != voiceLangs.end()) voiceLangs.erase(was);
            if (r.first < 100) {
                const VoiceLang& into = voiceLangs[r.second];
                moved.wanted += into.wanted;
                moved.other += into.other;
            }
            voiceLangs[r.second] = moved;
        }
        if (!renamed.empty()) ++version;
        return id;
    }

    // ---------------------------------------------------------------- phrases out

    void fail(uint64_t id, const std::string& err) {
        std::lock_guard<std::mutex> lk(mtx);
        eraseCaption(id);
        if (err != "cancelled") error = err;
    }

    void process(const Job& job, const ListenConfig& c, Eleven& api, Translator& translator) {
        Transcript t;
        std::string err;
        const bool events = c.soundLabels || c.ignoreNoise;
        const Clock::time_point tStart = Clock::now();
        // Streamed while they talked, and nothing is wanted that only the regular pass gives (who said
        // what, sounds): its words are here already, so the audio isn't sent a second time.
        bool fromLive = false;
        // "Only this language": the speech service isn't told the language, so it says which one it heard.
        const Language* only = onlyLanguage(c.s);
        const std::string sttLang = only ? std::string() : c.s.fromLang;
        // ("Ignore noise" needs nothing more: words came, so it wasn't only noise.)
        if (job.live.valid() && !c.s.tellApart && !c.soundLabels &&
            job.live.wait_for(std::chrono::milliseconds(1500)) == std::future_status::ready) {
            const LiveText lt = job.live.get();
            if (lt.ok && !lt.text.empty()) {
                SpeakerTurn turn;
                turn.text = t.text = lt.text;
                t.language = lt.language;  // languageSure stays 0: each line's language is checked below
                t.words = roughWords(lt.text);
                t.turns.push_back(turn);
                fromLive = true;
            }
        }
        // What goes to the speech service: the phrase as it was heard, unless it is cleaned below.
        const std::vector<int16_t>* toWrite = &job.pcm;
        std::vector<int16_t> cleaned;
        if (!fromLive) {
            CleanSound* ear = cleanSoundEngine();
            // Is there a voice in it at all? Phrases are cut by loudness, so a door, footsteps, a fan or
            // a game's music is one too. Those go no further: not sent, not paid for, and not in the
            // queue ahead of someone talking. Not with sound labels on (then "[music]" is wanted), and
            // not when the live connection already heard words in it.
            float speech = 0;
            if (c.s.skipNoVoice && !c.soundLabels && ear && ear->installed() && ear->speechSeconds(job.pcm, speech) && speech < 0.2f) {
                bool liveWords = false;
                if (job.live.valid() && job.live.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    const LiveText lt = job.live.get();
                    liveWords = lt.ok && !lt.text.empty();
                }
                if (!liveWords) {
                    static std::atomic<int> count{0};
                    const int n = ++count;
                    if (n <= 3 || n % 25 == 0) {
                        char b[200];
                        std::snprintf(b, sizeof(b), "Listen: %.1f s of sound with no voice in it was not sent to the speech service (%d so far).",
                                      job.pcm.size() / 16000.0, n);
                        logLine(b);
                    }
                    std::lock_guard<std::mutex> lk(mtx);
                    eraseCaption(job.id);
                    error.clear();
                    return;
                }
            }
            // A trial: a noisy phrase has the hiss, fan or hum taken out before it is written down.
            // Only the words: voices are told apart from the sound as it was (see cleansound.h).
            if (c.s.cleanWords && ear && ear->installed()) {
                const float out = standsOutDb(job.pcm);
                if (out < 20.f) {
                    cleaned = job.pcm;
                    const Clock::time_point tClean = Clock::now();
                    if (ear->clean(cleaned)) {
                        toWrite = &cleaned;
                        static std::atomic<int> count{0};
                        const int n = ++count;
                        if (n <= 3 || n % 25 == 0) {
                            char b[240];
                            std::snprintf(b, sizeof(b), "Listen: a noisy phrase was cleaned before it was written down (the voice stood out by %.0f dB, %.0f after; took %d ms; %d so far).",
                                          out, standsOutDb(cleaned), msSince(tClean), n);
                            logLine(b);
                        }
                    }
                }
            }
        }
        if (!fromLive && !api.transcribeTurns(*toWrite, sttLang, c.s.tellApart, t, err, events, c.soundLabels)) { fail(job.id, err); return; }
        const int sttMs = msSince(tStart);
        int trMs = 0;

        const Language* to = findLanguage(c.s.toLang);
        const Language* from = findLanguage(c.s.fromLang.empty() ? t.language : c.s.fromLang);
        // "Languages around me": a language outside the list is a misheard short phrase. With one
        // language on the list it's that one; otherwise the translator works it out.
        bool sure = !c.s.fromLang.empty() || t.languageSure >= 0.8f;
        if (c.s.fromLang.empty() && !c.around.empty() && from &&
            std::find(c.around.begin(), c.around.end(), std::string(from->code)) == c.around.end()) {
            from = c.around.size() == 1 ? findLanguage(c.around[0]) : nullptr;
            sure = c.around.size() == 1;
        }
        // Where each voice comes from (VRChat and other games that place voices around you). The speech
        // service sometimes puts two people talking over each other into one line: when that line's
        // words clearly come from two places, it becomes two lines, one per person.
        const bool stereo = panUseful(job.pan);
        if (stereo) {
            int nextSpeaker = 0;
            for (const SpeakerTurn& tr : t.turns) nextSpeaker = std::max(nextSpeaker, tr.speaker + 1);
            for (size_t i = 0; i < t.turns.size(); ++i) {
                const SpeakerTurn& tr = t.turns[i];
                std::vector<float> wpan, wlen;
                bool timed = tr.words.size() >= 4;
                for (const TurnWord& w : tr.words) {
                    float p = 0;
                    if (w.start < 0 || w.end < w.start || !panOver(job.pcm, job.pan, w.start, w.end, p)) { timed = false; break; }
                    wpan.push_back(p);
                    wlen.push_back(w.end - w.start);
                }
                if (!timed) continue;
                const std::vector<int> side = splitByPlace(wpan, wlen);
                if (side.empty()) continue;
                SpeakerTurn part[2];
                part[0].speaker = tr.speaker;
                part[1].speaker = nextSpeaker++;
                for (size_t k = 0; k < tr.words.size(); ++k) {
                    SpeakerTurn& to2 = part[side[k]];
                    const TurnWord& w = tr.words[k];
                    to2.words.push_back(w);
                    to2.text += w.text;
                    if (to2.start < 0 || w.start < to2.start) to2.start = w.start;
                    to2.end = std::max(to2.end, w.end);
                }
                for (SpeakerTurn& pt : part) {
                    size_t a = pt.text.find_first_not_of(" \t"), b = pt.text.find_last_not_of(" \t");
                    pt.text = a == std::string::npos ? std::string() : pt.text.substr(a, b - a + 1);
                }
                const bool firstIs0 = part[0].start <= part[1].start;
                t.turns[i] = part[firstIs0 ? 0 : 1];
                t.turns.insert(t.turns.begin() + long(i) + 1, part[firstIs0 ? 1 : 0]);
                ++i;  // past the second half
            }
        }
        // The speech service gives one language for the whole caption, but two people can talk at
        // once in different languages (English and French): a line whose own words clearly show
        // another language goes by that one, so it still gets translated (and a line already in the
        // reader's language isn't). Only with more than one line, and only with the spoken language
        // on automatic: a single line keeps the service's answer, which heard the whole thing.
        std::vector<const Language*> lineFrom(t.turns.size(), from);
        if (c.s.fromLang.empty() && t.turns.size() > 1)
            for (size_t i = 0; i < t.turns.size(); ++i) {
                bool clear = false;
                const Language* l = findLanguage(guessLanguage(t.turns[i].text, &clear));
                if (!clear || !l || l == from) continue;
                if (!c.around.empty() && std::find(c.around.begin(), c.around.end(), std::string(l->code)) == c.around.end()) continue;
                lineFrom[i] = l;
            }
        // "They speak" picked (without "Only this language"): every line was taken to be in that
        // language, so a line in the reader's own language was "translated" into the language it was
        // already in (and "Skip speech in my language" never left it out). A line whose words clearly
        // are the reader's language goes by that.
        // A line too short to tell on its own ("Sorry.") goes by the whole caption when that clearly is.
        if (!c.s.fromLang.empty() && !only && to) {
            std::string all;
            for (const SpeakerTurn& tr : t.turns) all += tr.text + " ";
            bool allClear = false;
            const bool allReaders = findLanguage(guessLanguage(all, &allClear)) == to && allClear;
            // "They speak" is a language with its own letters (Arabic, Russian, Chinese...): a line
            // written only in Latin letters isn't in it, so a guess of the reader's language will do.
            const bool picksOwn = ownLetters(findLanguage(c.s.fromLang)) && !ownLetters(to);
            for (size_t i = 0; i < t.turns.size(); ++i) {
                bool clear = false;
                const Language* g = findLanguage(guessLanguage(t.turns[i].text, &clear));
                if (lineFrom[i] != to &&
                    ((g == to && clear) || (!clear && allReaders) || (g == to && picksOwn && latinOnly(t.turns[i].text))))
                    lineFrom[i] = to;
            }
        }
        // "Only this language": each line is that language (kept), clearly another one (left out), or
        // too short to tell (kept for now; who said it may settle it once the voices are named).
        std::vector<char> lineOut(t.turns.size(), 0), lineUnsure(t.turns.size(), 0);
        bool allOut = only && !t.turns.empty();
        if (only) {
            const Language* said = findLanguage(t.language);  // what the service heard, for the whole caption
            const bool saidSure = said && t.languageSure >= 0.8f;
            for (size_t i = 0; i < t.turns.size(); ++i) {
                const Language* other = clearlyOther(only, t.turns[i].text);
                bool clear = false;
                const bool wordsAreIt = findLanguage(guessLanguage(t.turns[i].text, &clear)) == only && clear;
                if (wordsAreIt || (!other && said == only)) {
                    lineFrom[i] = only;
                } else if (other || (saidSure && said != only && !sameLetters(said, only))) {
                    lineOut[i] = 1;
                    lineFrom[i] = other ? other : said;
                } else {
                    lineUnsure[i] = 1;
                    lineFrom[i] = nullptr;  // the translator works out what it is
                }
                allOut = allOut && lineOut[i];
            }
        }
        auto lineSame = [&](size_t i) { return to != nullptr && lineFrom[i] == to; };  // already in the reader's language
        bool same = true;  // every line is
        for (size_t i = 0; i < t.turns.size(); ++i) same = same && lineSame(i);
        // Only music or noise: nothing anyone said.
        bool noise = false;
        if (c.ignoreNoise) {
            bool music = false;
            for (const std::string& e : t.events)
                music = music || e.find("music") != std::string::npos || e.find("singing") != std::string::npos ||
                        e.find("song") != std::string::npos;
            noise = (t.words == 0 && !t.events.empty()) || (music && t.words <= 6);
        }
        // (With voices told apart, a caption that is all another language still goes on to the naming
        // below, so the voice is remembered as one that speaks another language. It isn't translated.)
        if (t.turns.empty() || noise || (same && c.s.onlyOtherLanguages) || (allOut && !c.voiceIds)) {
            std::lock_guard<std::mutex> lk(mtx);
            eraseCaption(job.id);  // noise, music, or nothing to translate
            error.clear();
            return;
        }

        bool several = false;
        for (const SpeakerTurn& turn : t.turns) several = several || turn.speaker != t.turns[0].speaker;
        std::vector<CaptionLine> lines;
        std::string problem, chat;
        // The languages heard, for the caption's label: "English", or "English, French" when two
        // people spoke different ones.
        std::vector<const Language*> heard;
        auto noteHeard = [&](const Language* l) {
            if (l && std::find(heard.begin(), heard.end(), l) == heard.end()) heard.push_back(l);
        };
        TranslateSetup setup = c.translate;
        setup.faithful = true;
        setup.wordNote = wordListNote(c.words);
        {   // the last few things people said, so slang and references are understood in context
            std::lock_guard<std::mutex> lk(mtx);
            // Newest first, with the listener's own lines where they were said: "which one?" or
            // "me too" only makes sense next to what it answers.
            std::vector<std::string> recent;
            auto own = mine.rbegin();
            auto addOwn = [&](uint64_t afterId) {  // their lines said after caption `afterId`
                for (; own != mine.rend() && own->beforeId > afterId && recent.size() < 6; ++own)
                    if (own->beforeId <= job.id && Clock::now() - own->when < std::chrono::seconds(90))
                        recent.push_back("Listener: " + own->text);
            };
            size_t theirs = 0;
            for (auto it = feed.rbegin(); it != feed.rend() && theirs < 4 && recent.size() < 6; ++it) {
                if (it->pending || it->id == job.id || it->id > job.id) continue;
                addOwn(it->id);
                for (const CaptionLine& l : it->lines) {
                    recent.push_back("Them: " + (l.original.empty() ? l.text : l.original));
                    ++theirs;
                }
            }
            addOwn(0);
            for (auto it = recent.rbegin(); it != recent.rend(); ++it) setup.context += *it + "\n";
        }
        // When each person spoke (word by word), so a voice print can leave out everyone else.
        auto spansOf = [](const SpeakerTurn& tr) {
            std::vector<std::pair<float, float>> v;
            for (const TurnWord& w : tr.words)
                if (w.start >= 0 && w.end >= w.start) v.push_back({std::max(0.f, w.start - 0.05f), w.end + 0.05f});
            if (v.empty() && tr.start >= 0 && tr.end > tr.start) v.push_back({tr.start, tr.end});
            return v;
        };
        std::vector<size_t> turnOf;  // which turn each line came from
        // Lines whose voice is to be named. With the recognition model, each one's fingerprint is
        // worked out while the translation is on its way, so naming them comes after it.
        struct ToName { size_t line; VoicePrint vp; std::vector<std::pair<float, float>> spans; };
        std::vector<ToName> toName;
        for (size_t ti = 0; ti < t.turns.size(); ++ti) {
            const SpeakerTurn& turn = t.turns[ti];
            if (c.s.onlyOtherLanguages && lineSame(ti)) continue;  // only the other languages are wanted
            if (!lineOut[ti]) noteHeard(lineFrom[ti]);
            CaptionLine line;
            line.speaker = several ? turn.speaker : -1;
            line.text = applyWordFixes(turn.text, c.words);
            line.voice = guessVoiceGender(job.pcm, several ? turn.start : -1.f, several ? turn.end : -1.f);
            if (c.voiceIds) {
                VoicePrint vp;
                bool printed = false;
                std::vector<std::pair<float, float>> solo;  // this person's own moments
                const std::vector<std::pair<float, float>> mine = spansOf(turn);
                if (several && !mine.empty()) {
                    // Only this person's own moments: not while anyone else talked, and (with stereo)
                    // not when the sound came from somewhere else. Too little left: unnamed, not a guess.
                    std::vector<std::pair<float, float>> others;
                    for (size_t k = 0; k < t.turns.size(); ++k)
                        if (k != ti && t.turns[k].speaker != turn.speaker)
                            for (const auto& sp : spansOf(t.turns[k])) others.push_back(sp);
                    float dir = 0;
                    const bool hasDir = stereo && panOver(job.pcm, job.pan, turn.start, turn.end, dir);
                    solo = soloSpans(mine, others, job.pan, dir, hasDir);
                    printed = voicePrintSpans(job.pcm, solo, vp);
                    vp.pan = dir;
                    vp.hasPan = hasDir;
                } else {
                    solo.push_back({several ? turn.start : 0.f, several ? turn.end : float(job.pcm.size()) / 16000.f});
                    printed = voicePrint(job.pcm, several ? turn.start : -1.f, several ? turn.end : -1.f, vp);
                    float dir = 0;
                    if (printed && stereo && panOver(job.pcm, job.pan, several ? turn.start : 0.f,
                                                     several ? turn.end : float(job.pcm.size()) / 16000.f, dir)) {
                        vp.pan = dir;
                        vp.hasPan = true;
                    }
                }
                if (printed) toName.push_back({lines.size(), vp, solo});
            }
            lines.push_back(std::move(line));
            turnOf.push_back(ti);
        }

        // Fingerprints, off to the side: started now, collected after the translation.
        std::future<void> printing;
        VoiceId* engine = c.voiceModel ? voiceIdEngine() : nullptr;
        if (engine && !toName.empty() && engine->installed())
            printing = std::async(std::launch::async, [&toName, &job, engine] {
                std::vector<float> sound;
                for (ToName& n : toName) {
                    sound.clear();
                    for (const auto& sp : n.spans) {
                        const size_t a = size_t(std::max(0.f, sp.first) * 16000.f), b = std::min(job.pcm.size(), size_t(std::max(0.f, sp.second) * 16000.f));
                        for (size_t i = a; i < b; ++i) sound.push_back(float(job.pcm[i]) / 32768.f);
                    }
                    engine->fingerprint(sound.data(), sound.size(), n.vp.fp);  // empty when there's too little
                    n.vp.fpSeconds = float(sound.size()) / 16000.f;
                    // Is it one voice? The speech service sometimes writes two people down as one
                    // (talking over each other, or one right after the other). With three seconds or
                    // more, the two halves are fingerprinted too: one person's halves are alike, two
                    // people's are not, and a blend of two voices must never be learned as a person.
                    if (!n.vp.fp.empty() && sound.size() >= 16000 * 3) {
                        const size_t half = sound.size() / 2;
                        std::vector<float> a, b;
                        if (engine->fingerprint(sound.data(), half, a) && engine->fingerprint(sound.data() + half, sound.size() - half, b) &&
                            likeness(a, b) < 0.25f) {
                            n.vp.mixed = true;
                            n.vp.fp.clear();
                        }
                    }
                }
            });

        // Translate the lines that need it, all of a caption in one request where possible.
        if (to) {
            std::vector<size_t> todo;
            std::vector<std::string> texts;
            std::vector<const Language*> froms;
            std::vector<int> voices;
            for (size_t li = 0; li < lines.size(); ++li) {
                if (lineSame(turnOf[li]) || lineOut[turnOf[li]]) continue;
                todo.push_back(li);
                texts.push_back(lines[li].text);
                froms.push_back(lineFrom[turnOf[li]]);
                voices.push_back(lines[li].voice);  // gendered target languages: "I'm tired" -> cansado / cansada
            }
            if (!todo.empty()) {
                std::vector<std::string> outs;
                std::vector<int> genders;
                std::string why;
                const Clock::time_point tTr = Clock::now();
                if (!translator.translateLines(setup, texts, froms, voices, *to, outs, genders, why)) {
                    fail(job.id, why);  // cancelled
                    return;
                }
                trMs = msSince(tTr);
                static std::string lastVia;  // say when another service had to step in, and when that ends
                if (translator.via != lastVia && (translator.via.find('(') != std::string::npos || lastVia.find('(') != std::string::npos))
                    logLine("Listen translated with " + translator.via);
                lastVia = translator.via;
                for (size_t k = 0; k < todo.size(); ++k) {
                    CaptionLine& line = lines[todo[k]];
                    if (outs[k].empty()) {
                        if (problem.empty()) problem = why;  // keep the line, untranslated
                        continue;
                    }
                    if (genders[k]) line.voice = genders[k];  // their words beat a guess from the voice
                    line.original = line.text;
                    line.text = outs[k];
                }
            }
        }

        if (printing.valid()) printing.get();
        if (!toName.empty()) {
            std::lock_guard<std::mutex> lk(mtx);
            bool any = false;
            for (const ToName& n : toName) any = any || !n.vp.fp.empty() || n.vp.mixed;
            if (any) voices.setMax(12);  // fingerprints keep a dozen voices apart; the built-in way stays at six
            for (const ToName& n : toName) {
                lines[n.line].voiceId = voiceIdOf(n.vp);
                // Now and then, how sure it was, so the log can show whether voices are being told apart.
                static std::atomic<int> count{0};
                // (Every fifth line, and every line that wasn't a plain match: those are the ones to look at.)
                const std::string why = voices.lastWhy();
                const bool plain = why == "matched";
                if ((!n.vp.fp.empty() && voices.lastNearest() >= 0 && (!plain || ++count % 5 == 1)) || n.vp.mixed) {
                    char b[240];
                    if (n.vp.mixed)
                        std::snprintf(b, sizeof(b), "Voice fingerprints: %s.", why.c_str());
                    else
                        std::snprintf(b, sizeof(b), "Voice fingerprints: this line is %.2f like the nearest known voice, %.2f like the next (1 = the same): %s%s.",
                                      voices.lastNearest(), std::max(0.f, voices.lastNext()), why.empty() ? "no answer" : why.c_str(),
                                      lines[n.line].voiceId >= 0 && lines[n.line].voiceId < 100 ? (", Voice " + std::to_string(lines[n.line].voiceId + 1)).c_str() : "");
                    logLine(b);
                }
            }
        }

        // "Only this language": the lines in another language go. Each voice is remembered by what it
        // was clearly heard speaking, and a line too short to tell goes by that: from a voice that
        // speaks the language it stays, from a voice that has only spoken another one it goes. From a
        // voice nobody knows yet it stays (a missed friend is worse than one stray caption).
        if (only) {
            std::vector<CaptionLine> staying;
            int left = 0;
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (voiceLangsFor != only->code) {
                    voiceLangs.clear();
                    voiceLangsFor = only->code;
                }
                for (size_t li = 0; li < lines.size(); ++li) {
                    const size_t ti = turnOf[li];
                    const int id = lines[li].voiceId;
                    bool out = lineOut[ti] != 0;
                    if (id >= 0 && !lineUnsure[ti]) {
                        VoiceLang& v = voiceLangs[id];
                        ++(out ? v.other : v.wanted);
                    } else if (id >= 0) {
                        const auto it = voiceLangs.find(id);
                        if (it != voiceLangs.end() && it->second.other >= 2 && it->second.wanted == 0) out = true;
                    }
                    if (out) ++left;
                    else staying.push_back(std::move(lines[li]));
                }
            }
            if (left > 0) {
                static std::atomic<int> count{0};
                const int n = ++count;
                if (n <= 5 || n % 20 == 0)
                    logLine("Listen: only " + std::string(only->name) + " is captioned: " + std::to_string(left) +
                            (left == 1 ? " line" : " lines") + " in another language left out (" + std::to_string(n) + " so far).");
            }
            lines.swap(staying);
            if (lines.empty()) {
                std::lock_guard<std::mutex> lk(mtx);
                eraseCaption(job.id);
                error.clear();
                return;
            }
        }

        for (CaptionLine& line : lines) {
            if (c.maskSwears) {
                line.text = maskSwears(line.text);
                line.original = maskSwears(line.original);
            }
            if (!chat.empty()) chat += "\n";
            chat += line.text;
        }

        std::lock_guard<std::mutex> lk(mtx);
        for (Caption& cap : feed) {
            if (cap.id != job.id) continue;
            cap.pending = false;
            cap.live = false;
            cap.language = from ? from->name : t.language;
            if (heard.size() > 1) {
                cap.language.clear();
                for (const Language* l : heard) cap.language += (cap.language.empty() ? "" : ", ") + std::string(l->name);
            }
            cap.code = from ? from->code : std::string();
            cap.sure = sure && from;
            cap.lines = std::move(lines);
            lastMs = msSince(job.heard);
            lastSttMs = sttMs;
            lastTrMs = trMs;
            {   // where the time went, when a caption was slow (and now and then anyway)
                static std::atomic<int> count{0};
                const int total = msSince(job.heard), n = ++count;
                if (total > 2500 || n % 25 == 1) {
                    char b[200];
                    std::snprintf(b, sizeof(b), "Listen: caption %.1f s after they stopped (%.1f s of speech: words %.1f s, translation %.1f s, waiting %.1f s).",
                                  total / 1000.0, job.pcm.size() / 16000.0, sttMs / 1000.0, trMs / 1000.0,
                                  std::max(0, total - sttMs - trMs) / 1000.0);
                    logLine(b);
                }
            }
            if (c.s.chatbox) chatText = chat;  // only the newest caption is worth sending
            ++version;
            break;
        }
        error = problem;
    }

    void workerLoop() {
        Eleven api;  // each worker keeps its own connections open between phrases
        api.useFeature = kUseCaptions;
        Translator translator;
        api.http.cancel = &cancel;
        translator.http.cancel = &cancel;
        while (running) {
            Job job;
            ListenConfig c;
            bool haveJob = false;
            {
                std::unique_lock<std::mutex> lk(mtx);
                cv.wait(lk, [this] { return !running || !jobs.empty() || warm > 0; });
                if (!running) break;
                c = cfg;
                if (!jobs.empty()) {
                    job = std::move(jobs.front());
                    jobs.pop_front();
                    haveJob = true;
                } else {
                    --warm;
                }
            }
            api.apiKey = c.apiKey;
            if (!haveJob) {
                api.warm();
                if (findLanguage(c.s.toLang)) translator.warm(c.translate);
                continue;
            }
            ++busy;
            process(job, c, api, translator);
            --busy;
        }
    }
};

// ---------------------------------------------------------------- public

Listener::Listener() : p_(new Impl) {
    p_->status = "Listen is off.";
}

Listener::~Listener() { stop(); }

void Listener::start() {
    if (p_->running.exchange(true)) return;
    p_->cancel = false;
    p_->ctxOk = ma_context_init(nullptr, 0, nullptr, &p_->ctx) == MA_SUCCESS;
    p_->liveStt = p_->liveHost.empty() ? std::make_unique<LiveStt>() : std::make_unique<LiveStt>(p_->liveHost);
    p_->liveStt->onPartial = [this](const std::string& words) { p_->onLiveWords(words); };
    p_->liveStt->useFeature = kUseLive;
    p_->liveStt->onLog = [](const std::string& line) { logLine("Listen, live words: " + line); };
    p_->liveTrThread = std::thread([this] { p_->liveTranslateLoop(); });
    p_->capThread = std::thread([this] { p_->captureLoop(); });
    for (int i = 0; i < kWorkers; ++i) p_->workers.emplace_back([this] { p_->workerLoop(); });
}

void Listener::stop() {
    if (!p_->running.exchange(false)) return;
    p_->cancel = true;
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->cv.notify_all();
        p_->liveCv.notify_all();
    }
    p_->capThread.join();
    p_->liveAccept = false;
    p_->liveThis = false;
    if (p_->liveThread.joinable()) p_->liveThread.join();
    if (p_->liveTrThread.joinable()) p_->liveTrThread.join();
    p_->liveStt.reset();
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->liveEnds.clear();
    }
    for (std::thread& w : p_->workers) w.join();
    p_->workers.clear();
    if (p_->convOn) { ma_data_converter_uninit(&p_->conv, nullptr); p_->convOn = false; }
    if (p_->ctxOk) { ma_context_uninit(&p_->ctx); p_->ctxOk = false; }
}

void Listener::useLiveHost(const std::string& host) { p_->liveHost = host; }

void Listener::configure(const ListenConfig& c) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->cfg = c;
}

void Listener::clear() {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->jobs.clear();
    p_->feed.clear();
    p_->mine.clear();
    p_->chatText.clear();
    ++p_->version;
}

uint64_t Listener::version() const { return p_->version; }

void Listener::noteMine(const std::string& text) {
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) return;
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->mine.push_back({text.substr(0, 300), p_->nextId, Clock::now()});
    while (p_->mine.size() > 4) p_->mine.pop_front();
}

void Listener::addScreenCaption(const std::string& text, const std::string& original) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    Caption c;
    c.id = p_->nextId++;
    c.pending = false;
    c.screen = true;
    c.time = clockNow();
    CaptionLine l;
    l.text = text;
    if (original != text) l.original = original;
    c.lines.push_back(std::move(l));
    p_->feed.push_back(std::move(c));
    while (p_->feed.size() > kMaxCaptions) p_->feed.pop_front();
    ++p_->version;
}

void Listener::captions(std::vector<Caption>& out) const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    out.assign(p_->feed.begin(), p_->feed.end());
    for (Caption& c : out)  // a voice without a number yet is shown as unnamed
        for (CaptionLine& l : c.lines)
            if (l.voiceId >= 100) l.voiceId = -1;
}

ListenState Listener::state() const {
    ListenState s;
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        s.status = p_->status;
        s.error = p_->error;
    }
    s.hearing = p_->hearing;
    s.busy = p_->busy > 0;
    s.levelDb = p_->levelDb;
    s.lastMs = p_->lastMs;
    s.sttMs = p_->lastSttMs;
    s.translateMs = p_->lastTrMs;
    s.streaming = p_->liveAccept;
    return s;
}

void Listener::pushPcm(const int16_t* interleaved, size_t frames, int channels, int rate) {
    p_->pushPcm(interleaved, frames, channels, rate);
}
