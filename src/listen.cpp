#include "listen.h"
#include "applog.h"
#include "emotion.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>

#include "appaudio.h"
#include "audio.h"
#include "core.h"
#include "eleven.h"
#include "osc.h"
#include "translate.h"

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr int kWorkers = 2;          // phrases are transcribed side by side, shown in spoken order
constexpr size_t kMaxWaiting = 3;    // more than this waiting = falling behind: drop the oldest
constexpr size_t kMaxCaptions = 200;
constexpr int kChatboxGapMs = 1600;  // VRChat mutes the chatbox when it is written to too often

struct Job {
    uint64_t id = 0;
    std::vector<int16_t> pcm;
    Clock::time_point heard;
};

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
    int warm = 0;  // workers that should open their connections now
    std::string status, error, chatText;
    std::condition_variable cv;

    // Voices heard this session, to keep the same colour for the same person across captions.
    VoiceBook voices;                 // guarded by mtx
    // Captions while they talk: the caption being spoken and the words so far.
    uint64_t liveId = 0;              // guarded by mtx; 0 = none
    std::thread liveThread;           // one transcription of the unfinished phrase at a time
    std::atomic<bool> liveBusy{false};

    std::atomic<uint64_t> version{1};
    std::atomic<bool> running{false}, cancel{false}, hearing{false};
    std::atomic<int> busy{0}, lastMs{0};
    std::atomic<float> levelDb{-100.f};

    SpscRing<float> ring{16000 * 10};
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
        if (in) static_cast<Impl*>(d->pUserData)->ring.push(static_cast<const float*>(in), frames);
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
        c.capture.channels = 1;
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
            ma_data_converter_config c = ma_data_converter_config_init(ma_format_s16, ma_format_f32, ma_uint32(channels), 1,
                                                                       ma_uint32(rate), Segmenter::kRate);
            convOn = ma_data_converter_init(&c, nullptr, &conv) == MA_SUCCESS;
            convChannels = channels;
            convRate = rate;
            if (!convOn) return;
        }
        convOut.resize(4096);
        while (frames > 0) {
            ma_uint64 nIn = frames, nOut = convOut.size();
            if (ma_data_converter_process_pcm_frames(&conv, in, &nIn, convOut.data(), &nOut) != MA_SUCCESS) return;
            ring.push(convOut.data(), size_t(nOut));
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

    void enqueue(std::vector<int16_t>&& pcm) {
        std::lock_guard<std::mutex> lk(mtx);
        while (jobs.size() >= kMaxWaiting) {  // stay live rather than fall further behind
            eraseCaption(jobs.front().id);
            jobs.pop_front();
        }
        Job j;
        j.pcm = std::move(pcm);
        j.heard = Clock::now();
        if (liveId != 0) {  // the words shown while they talked become this caption
            j.id = liveId;
            liveId = 0;
            jobs.push_back(std::move(j));
            ++version;
            cv.notify_one();
            return;
        }
        j.id = nextId++;
        Caption c;
        c.id = j.id;
        c.time = clockNow();
        feed.push_back(std::move(c));
        while (feed.size() > kMaxCaptions) feed.pop_front();
        jobs.push_back(std::move(j));
        ++version;
        cv.notify_one();
    }

    void captureLoop() {
        Segmenter seg;
        OscOut osc;
        float block[Segmenter::kBlock];
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
            if (ring.size() >= size_t(Segmenter::kBlock)) {
                ring.pop(block, Segmenter::kBlock);
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

            GateParams g;
            const Pace pace = paceOf(s.pace);
            g.thresholdDb = s.thresholdDb;
            g.holdMs = pace.holdMs;
            g.maxSeconds = pace.maxSeconds;
            std::vector<int16_t> phrase;
            const bool done = seg.feed(in, g, phrase);
            levelDb = seg.levelDb();
            const bool open = seg.open();
            hearing = open;
            if (open && !done) showLiveWords(seg.current());
            if (done) {
                enqueue(std::move(phrase));
            } else if (open && !wasOpen) {
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
            std::string text, lang, err;
            if (api.transcribe(pcm, c.s.fromLang, text, lang, err) && !text.empty()) {
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

    // Which voice this is, among the ones heard this session (a best guess; see VoiceBook). mtx held.
    // Lines already in the feed follow when a voice gets its number or two turn out to be one.
    int voiceIdOf(const VoicePrint& p) {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::vector<std::pair<int, int>> renamed;
        const int id = voices.who(p, now, cfg.keepVoices, renamed);
        for (const auto& r : renamed)
            for (Caption& c : feed)
                for (CaptionLine& l : c.lines)
                    if (l.voiceId == r.first) l.voiceId = r.second;
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
        if (!api.transcribeTurns(job.pcm, c.s.fromLang, c.s.tellApart, t, err, events, c.soundLabels)) { fail(job.id, err); return; }

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
        const bool same = to != nullptr && from == to;  // already in the reader's language
        // Only music or noise: nothing anyone said.
        bool noise = false;
        if (c.ignoreNoise) {
            bool music = false;
            for (const std::string& e : t.events)
                music = music || e.find("music") != std::string::npos || e.find("singing") != std::string::npos ||
                        e.find("song") != std::string::npos;
            noise = (t.words == 0 && !t.events.empty()) || (music && t.words <= 6);
        }
        if (t.turns.empty() || noise || (same && c.s.onlyOtherLanguages)) {
            std::lock_guard<std::mutex> lk(mtx);
            eraseCaption(job.id);  // noise, music, or nothing to translate
            error.clear();
            return;
        }

        bool several = false;
        for (const SpeakerTurn& turn : t.turns) several = several || turn.speaker != t.turns[0].speaker;
        std::vector<CaptionLine> lines;
        std::string problem, chat;
        TranslateSetup setup = c.translate;
        setup.faithful = true;
        setup.wordNote = wordListNote(c.words);
        {   // the last few things people said, so slang and references are understood in context
            std::lock_guard<std::mutex> lk(mtx);
            std::vector<std::string> recent;
            for (auto it = feed.rbegin(); it != feed.rend() && recent.size() < 4; ++it) {
                if (it->pending || it->id == job.id) continue;
                for (const CaptionLine& l : it->lines) recent.push_back("Them: " + (l.original.empty() ? l.text : l.original));
            }
            for (auto it = recent.rbegin(); it != recent.rend(); ++it) setup.context += *it + "\n";
        }
        for (const SpeakerTurn& turn : t.turns) {
            CaptionLine line;
            line.speaker = several ? turn.speaker : -1;
            line.text = applyWordFixes(turn.text, c.words);
            line.voice = guessVoiceGender(job.pcm, several ? turn.start : -1.f, several ? turn.end : -1.f);
            if (c.voiceIds) {
                VoicePrint vp;
                if (voicePrint(job.pcm, several ? turn.start : -1.f, several ? turn.end : -1.f, vp)) {
                    std::lock_guard<std::mutex> lk(mtx);
                    line.voiceId = voiceIdOf(vp);
                }
            }
            setup.speaker = line.voice;  // gendered target languages: "I'm tired" -> cansado / cansada
            if (!same && to && problem.empty()) {
                std::string translated;
                if (translator.translate(setup, line.text, from, *to, translated, err)) {
                    if (translator.wordsGender) line.voice = translator.wordsGender;  // their words beat a guess from the voice
                    static std::string lastVia;  // say when another service had to step in, and when that ends
                    if (translator.via != lastVia && (translator.via.find('(') != std::string::npos || lastVia.find('(') != std::string::npos))
                        logLine("Listen translated with " + translator.via);
                    lastVia = translator.via;
                    line.original = line.text;
                    line.text = translated;
                } else if (err == "cancelled") {
                    fail(job.id, err);
                    return;
                } else {
                    problem = err;  // keep the caption, untranslated
                }
            }
            if (c.maskSwears) {
                line.text = maskSwears(line.text);
                line.original = maskSwears(line.original);
            }
            if (!chat.empty()) chat += "\n";
            chat += line.text;
            lines.push_back(std::move(line));
        }

        std::lock_guard<std::mutex> lk(mtx);
        for (Caption& cap : feed) {
            if (cap.id != job.id) continue;
            cap.pending = false;
            cap.live = false;
            cap.language = from ? from->name : t.language;
            cap.code = from ? from->code : std::string();
            cap.sure = sure && from;
            cap.lines = std::move(lines);
            lastMs = msSince(job.heard);
            if (c.s.chatbox) chatText = chat;  // only the newest caption is worth sending
            ++version;
            break;
        }
        error = problem;
    }

    void workerLoop() {
        Eleven api;  // each worker keeps its own connections open between phrases
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
    p_->capThread = std::thread([this] { p_->captureLoop(); });
    for (int i = 0; i < kWorkers; ++i) p_->workers.emplace_back([this] { p_->workerLoop(); });
}

void Listener::stop() {
    if (!p_->running.exchange(false)) return;
    p_->cancel = true;
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->cv.notify_all();
    }
    p_->capThread.join();
    if (p_->liveThread.joinable()) p_->liveThread.join();
    for (std::thread& w : p_->workers) w.join();
    p_->workers.clear();
    if (p_->convOn) { ma_data_converter_uninit(&p_->conv, nullptr); p_->convOn = false; }
    if (p_->ctxOk) { ma_context_uninit(&p_->ctx); p_->ctxOk = false; }
}

void Listener::configure(const ListenConfig& c) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->cfg = c;
}

void Listener::clear() {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->jobs.clear();
    p_->feed.clear();
    p_->chatText.clear();
    ++p_->version;
}

uint64_t Listener::version() const { return p_->version; }

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
    return s;
}

void Listener::pushPcm(const int16_t* interleaved, size_t frames, int channels, int rate) {
    p_->pushPcm(interleaved, frames, channels, rate);
}
