#include "livestt.h"
#include "usagecount.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kRate = 16000;
constexpr size_t kMinChunk = kRate / 10;  // 100 ms: the service takes chunks of 0.1 to 1 s
constexpr size_t kMaxChunk = kRate;       // 1 s

std::string base64(const unsigned char* p, size_t n) {
    static const char* const k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const unsigned v = (unsigned(p[i]) << 16) | (unsigned(p[i + 1]) << 8) | p[i + 2];
        out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
    }
    if (i + 1 == n) {
        const unsigned v = unsigned(p[i]) << 16;
        out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == n) {
        const unsigned v = (unsigned(p[i]) << 16) | (unsigned(p[i + 1]) << 8);
        out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += '=';
    }
    return out;
}

std::string urlPart(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out += char(c);
        else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", c); out += b; }
    }
    return out;
}

int msBetween(Clock::time_point a, Clock::time_point b) {
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
}

// Aborts a connection attempt that takes too long, or when quitting.
struct ConnectWatch { Clock::time_point until; const std::atomic<bool>* quit; };
int onConnectProgress(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* w = static_cast<const ConnectWatch*>(ud);
    return (Clock::now() > w->until || w->quit->load()) ? 1 : 0;
}

// Waits until the socket can be read (or written), at most `ms`.
void waitSocket(CURL* h, int ms, bool write) {
    curl_socket_t s = CURL_SOCKET_BAD;
    if (curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &s) != CURLE_OK || s == CURL_SOCKET_BAD) return;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    select(int(s) + 1, write ? nullptr : &fds, write ? &fds : nullptr, nullptr, &tv);
}

bool isError(const std::string& type) {
    static const char* const k[] = {"error", "auth_error", "quota_exceeded", "commit_throttled", "unaccepted_terms",
                                    "rate_limited", "queue_overflow", "resource_exhausted", "session_time_limit_exceeded",
                                    "input_error", "invalid_request", "chunk_size_exceeded", "insufficient_audio_activity",
                                    "transcriber_error"};
    for (const char* e : k)
        if (type == e) return true;
    return false;
}

}  // namespace

LiveStt::LiveStt(std::string host) : host_(std::move(host)) {}

LiveStt::~LiveStt() {
    quit_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void LiveStt::begin(const Options& o) {
    std::lock_guard<std::mutex> lk(mx_);
    if (!thread_.joinable()) thread_ = std::thread([this] { run(); });  // started the first time it's used
    wanted_ = o;
    Item it;
    it.kind = Item::kAudio;
    it.first = true;
    queue_.push_back(std::move(it));
    cv_.notify_one();
    UseAmount a;  // for the Usage page: one phrase streamed
    a.requests = 1;
    countUse(kUseEleven, useFeature, a);
}

void LiveStt::audio(const int16_t* pcm, size_t n) {
    if (n == 0) return;
    std::lock_guard<std::mutex> lk(mx_);
    if (!queue_.empty() && queue_.back().kind == Item::kAudio) {
        queue_.back().pcm.insert(queue_.back().pcm.end(), pcm, pcm + n);
    } else {
        Item it;
        it.pcm.assign(pcm, pcm + n);
        queue_.push_back(std::move(it));
    }
    cv_.notify_one();
}

std::shared_future<LiveText> LiveStt::commit() {
    auto p = std::make_shared<std::promise<LiveText>>();
    std::shared_future<LiveText> f = p->get_future().share();
    std::lock_guard<std::mutex> lk(mx_);
    if (!thread_.joinable()) {
        LiveText t;
        t.err = "not started";
        p->set_value(t);
        return f;
    }
    Item it;
    it.kind = Item::kCommit;
    it.result = p;
    queue_.push_back(std::move(it));
    cv_.notify_one();
    return f;
}

void LiveStt::discard() {
    std::lock_guard<std::mutex> lk(mx_);
    if (!thread_.joinable()) return;
    Item it;
    it.kind = Item::kDiscard;
    queue_.push_back(std::move(it));
    cv_.notify_one();
}

bool LiveStt::available() const {
    std::lock_guard<std::mutex> lk(mx_);
    return Clock::now() >= pausedUntil_;
}

std::string LiveStt::problem() const {
    std::lock_guard<std::mutex> lk(mx_);
    return problem_;
}

std::string LiveStt::model() const {
    std::lock_guard<std::mutex> lk(mx_);
    return model_;
}

void LiveStt::giveUpFor(int seconds, const std::string& why) {
    {
        std::lock_guard<std::mutex> lk(mx_);
        pausedUntil_ = Clock::now() + std::chrono::seconds(seconds);
        problem_ = why;
    }
    if (onLog) onLog("Live transcription paused for " + std::to_string(seconds) + " s, using the regular way: " + why);
}

// ------------------------------------------------------------------ the live thread

void LiveStt::run() {
    static const bool inited = (curl_global_init(CURL_GLOBAL_DEFAULT), true);
    (void)inited;
    while (!quit_) {
        std::deque<Item> work;
        Options want;
        {
            std::unique_lock<std::mutex> lk(mx_);
            cv_.wait_for(lk, std::chrono::milliseconds(connected_ ? 2 : 50), [this] { return quit_ || !queue_.empty(); });
            work.swap(queue_);
            want = wanted_;
        }
        if (quit_) break;

        for (Item& it : work) {
            if (it.kind == Item::kAudio && it.first) {
                // A new phrase. A connection made for other settings (key, language, word times) is
                // replaced, once the earlier phrases' text has come back.
                const bool other = connected_ && (session_.apiKey != want.apiKey || session_.language != want.language ||
                                                  session_.timestamps != want.timestamps);
                if (other) {
                    const auto until = Clock::now() + std::chrono::milliseconds(1500);
                    while (connected_ && !waiting_.empty() && Clock::now() < until) receive();
                    disconnect("settings changed");
                }
                bool paused;
                {
                    std::lock_guard<std::mutex> lk(mx_);
                    paused = Clock::now() < pausedUntil_;
                }
                if (!connected_ && !paused) connect(want);
                phraseOpen_ = true;
                phraseBroken_ = !connected_;
                pending_.clear();
                phraseOffset_ = sessionSamples_;
            }
            if (it.kind == Item::kAudio) {
                if (phraseOpen_ && !phraseBroken_) pending_.insert(pending_.end(), it.pcm.begin(), it.pcm.end());
                // Sent in chunks of at least 100 ms, as soon as there are enough.
                while (connected_ && !phraseBroken_ && pending_.size() >= kMinChunk) {
                    const size_t n = std::min(pending_.size(), kMaxChunk);
                    if (!sendChunk(pending_, 0, n, false)) break;
                    pending_.erase(pending_.begin(), pending_.begin() + std::ptrdiff_t(n));
                }
                continue;
            }
            // The end of a phrase (kept or dropped).
            const bool keep = it.kind == Item::kCommit;
            if (!phraseOpen_ || phraseBroken_ || !connected_) {
                if (keep && it.result) {
                    LiveText t;
                    t.err = phraseBroken_ ? "the live connection wasn't there for all of this phrase" : "not connected";
                    it.result->set_value(t);
                }
                phraseOpen_ = false;
                pending_.clear();
                continue;
            }
            Waiting w;
            w.result = keep ? it.result : nullptr;
            w.wantExtra = session_.timestamps || session_.language.empty();
            w.offset = phraseOffset_;
            w.sent = Clock::now();
            if (pending_.empty()) pending_.assign(kRate / 100, 0);  // nothing left to send: 10 ms of quiet carries the commit
            bool sent = true;
            while (sent && !pending_.empty()) {
                const size_t n = std::min(pending_.size(), kMaxChunk);
                sent = sendChunk(pending_, 0, n, n == pending_.size());
                pending_.erase(pending_.begin(), pending_.begin() + std::ptrdiff_t(n));
            }
            phraseOpen_ = false;
            pending_.clear();
            w.length = sessionSamples_ - w.offset;
            if (sent && connected_) {
                waiting_.push_back(std::move(w));
            } else if (keep && it.result) {
                LiveText t;
                t.err = "the live connection dropped";
                it.result->set_value(t);
            }
        }

        if (connected_) {
            receive();
            const auto now = Clock::now();
            for (Waiting& w : waiting_) {
                // The second message (word times, language) is late: the text alone will do.
                if (w.gotText && !w.text.ok && w.wantExtra && msBetween(w.textAt, now) > 700) finish(w, true, "");
                // No answer at all: something is wrong with this connection.
                if (!w.gotText && msBetween(w.sent, now) > 6000) {
                    disconnect("no answer for 6 s");
                    break;
                }
            }
            // Done with: answered and not waiting for a second message any more, or failed.
            while (!waiting_.empty()) {
                const Waiting& f = waiting_.front();
                const bool answered = f.text.ok && (!f.wantExtra || msBetween(f.textAt, now) > 3000);
                if (!answered && f.text.err.empty()) break;
                waiting_.pop_front();
            }
            // The service closes a quiet connection after 15 s: close it first, so a phrase never starts
            // on one that is about to go. The next phrase opens a new one (while you're talking).
            if (connected_ && waiting_.empty() && !phraseOpen_ && msBetween(lastSent_, now) > idleCloseMs) disconnect("");
        }
    }
    disconnect("closing");
}

bool LiveStt::connect(const Options& o) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string model;
        {
            std::lock_guard<std::mutex> lk(mx_);
            model = model_;
        }
        CURL* h = curl_easy_init();
        if (!h) return false;
        std::string url = host_ + "/v1/speech-to-text/realtime?model_id=" + model +
                          "&audio_format=pcm_16000&commit_strategy=manual";
        if (!o.language.empty()) url += "&language_code=" + urlPart(o.language);
        else url += "&include_language_detection=true";
        if (o.timestamps) url += "&include_timestamps=true";
        curl_slist* hdrs = curl_slist_append(nullptr, ("xi-api-key: " + o.apiKey).c_str());
        ConnectWatch watch{Clock::now() + std::chrono::seconds(6), &quit_};
        curl_easy_setopt(h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 2L);  // WebSocket: do the upgrade, then it's ours
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(h, CURLOPT_USERAGENT, "CIMABox");
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, onConnectProgress);
        curl_easy_setopt(h, CURLOPT_XFERINFODATA, &watch);
        const CURLcode rc = curl_easy_perform(h);
        long code = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_setopt(h, CURLOPT_NOPROGRESS, 1L);
        curl_slist_free_all(hdrs);
        if (rc != CURLE_OK) {
            curl_easy_cleanup(h);
            if (quit_) return false;
            if (rc == CURLE_UNSUPPORTED_PROTOCOL || rc == CURLE_NOT_BUILT_IN) {
                giveUpFor(3600, "this build can't open live connections");
                return false;
            }
            if (code == 401 || code == 403) {
                giveUpFor(60, "ElevenLabs didn't accept the key for live transcription (" + std::to_string(code) + ")");
                return false;
            }
            if (code == 400 && attempt == 0 && model == "scribe_v3_live") {  // that model isn't offered to this account
                std::lock_guard<std::mutex> lk(mx_);
                model_ = "scribe_v2_realtime";
                continue;
            }
            if (++failures_ >= 3) giveUpFor(60, std::string("couldn't connect: ") + curl_easy_strerror(rc));
            else if (onLog) onLog(std::string("Live transcription couldn't connect: ") + curl_easy_strerror(rc));
            return false;
        }
        curl_ = h;
        connected_ = true;
        started_ = false;
        session_ = o;
        sessionSamples_ = 0;
        firstChunk_ = true;
        inbox_.clear();
        lastSent_ = Clock::now();
        // The service says it's ready (or why not) right away.
        const auto until = Clock::now() + std::chrono::seconds(3);
        while (connected_ && !started_ && Clock::now() < until && !quit_) receive();
        if (started_) {
            failures_ = 0;
            if (onLog) onLog("Live transcription connected (" + model + ")");
            return true;
        }
        const bool modelRefused = !connected_ && model == "scribe_v3_live" && model_ != model;
        if (connected_) disconnect("");
        if (modelRefused && attempt == 0) continue;  // retry with the other model
        if (++failures_ >= 3) giveUpFor(60, "the live connection didn't start");
        return false;
    }
    return false;
}

void LiveStt::disconnect(const std::string& why) {
    if (curl_) {
        size_t sent = 0;
        curl_ws_send(static_cast<CURL*>(curl_), "", 0, &sent, 0, CURLWS_CLOSE);
        curl_easy_cleanup(static_cast<CURL*>(curl_));
        curl_ = nullptr;
    }
    const bool was = connected_;
    connected_ = false;
    started_ = false;
    if (phraseOpen_) phraseBroken_ = true;  // part of this phrase went down with the connection
    pending_.clear();
    inbox_.clear();
    failAll(why.empty() ? "the live connection closed" : why);
    if (was && !why.empty() && why != "closing" && onLog) onLog("Live transcription disconnected: " + why);
}

void LiveStt::failAll(const std::string& err) {
    for (Waiting& w : waiting_) finish(w, false, err);
    waiting_.clear();
}

void LiveStt::finish(Waiting& w, bool ok, const std::string& err) {
    if (w.text.ok || !w.text.err.empty()) return;  // already answered
    if (!ok) w.text.err = err.empty() ? "failed" : err;
    w.text.ok = ok;
    if (w.result) w.result->set_value(w.text);
}

bool LiveStt::sendText(const std::string& s) {
    CURL* h = static_cast<CURL*>(curl_);
    size_t off = 0;
    const auto until = Clock::now() + std::chrono::seconds(3);
    while (off < s.size()) {
        size_t sent = 0;
        const CURLcode rc = curl_ws_send(h, s.data() + off, s.size() - off, &sent, 0, CURLWS_TEXT);
        off += sent;
        if (rc == CURLE_AGAIN) {
            if (Clock::now() > until) { disconnect("sending timed out"); return false; }
            waitSocket(h, 20, true);
            continue;
        }
        if (rc != CURLE_OK) {
            disconnect(std::string("couldn't send: ") + curl_easy_strerror(rc));
            return false;
        }
    }
    lastSent_ = Clock::now();
    return true;
}

bool LiveStt::sendChunk(const std::vector<int16_t>& pcm, size_t from, size_t count, bool commit) {
    json m = {{"message_type", "input_audio_chunk"},
              {"audio_base_64", base64(reinterpret_cast<const unsigned char*>(pcm.data() + from), count * sizeof(int16_t))},
              {"commit", commit},
              {"sample_rate", kRate}};
    if (firstChunk_ && !session_.previousText.empty()) m["previous_text"] = session_.previousText;  // the first chunk only
    if (!sendText(m.dump())) return false;
    firstChunk_ = false;
    sessionSamples_ += (long long)count;
    UseAmount a;  // for the Usage page: the sound streamed (a phrase counts as one request when it starts)
    a.audioMs = (long long)count * 1000 / kRate;
    countUse(kUseEleven, useFeature, a);
    return true;
}

void LiveStt::receive() {
    CURL* h = static_cast<CURL*>(curl_);
    if (!h) return;
    char buf[16384];
    for (int round = 0; round < 2 && connected_; ++round) {
        bool got = false;
        for (;;) {
            size_t n = 0;
            const curl_ws_frame* meta = nullptr;
            const CURLcode rc = curl_ws_recv(h, buf, sizeof(buf), &n, &meta);
            if (rc == CURLE_AGAIN) break;
            if (rc != CURLE_OK) {
                disconnect(rc == CURLE_GOT_NOTHING ? "the service closed the connection" : curl_easy_strerror(rc));
                return;
            }
            got = true;
            if (!meta) continue;
            if (meta->flags & CURLWS_CLOSE) {
                disconnect("the service closed the connection");
                return;
            }
            if (meta->flags & (CURLWS_PING | CURLWS_PONG)) continue;  // curl answers pings itself
            inbox_.append(buf, n);
            if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
                std::string message;
                message.swap(inbox_);
                handle(message);
                if (!connected_) return;
            }
        }
        if (got || round == 1) break;
        waitSocket(h, 3, false);  // nothing yet: a short wait for the socket, then look once more
    }
}

void LiveStt::handle(const std::string& message) {
    const json j = json::parse(message, nullptr, false);
    if (!j.is_object()) return;
    const std::string type = j.value("message_type", std::string());
    auto text = [&] { return j.contains("text") && j["text"].is_string() ? j["text"].get<std::string>() : std::string(); };

    if (type == "session_started") {
        started_ = true;
        return;
    }
    if (type == "partial_transcript") {
        if (phraseOpen_ && onPartial) onPartial(text());
        return;
    }
    if (type == "committed_transcript") {
        // Phrases that already have their text and were waiting for a second message that didn't
        // come: that message isn't coming now.
        while (!waiting_.empty() && waiting_.front().gotText && waiting_.front().text.ok) waiting_.pop_front();
        for (Waiting& w : waiting_) {
            if (w.gotText) continue;
            w.gotText = true;
            w.textAt = Clock::now();
            w.text.text = text();
            if (!w.wantExtra) {
                finish(w, true, "");
                w.wantExtra = false;
            }
            break;
        }
        while (!waiting_.empty() && waiting_.front().text.ok && !waiting_.front().wantExtra) waiting_.pop_front();
        return;
    }
    if (type == "committed_transcript_with_timestamps") {
        for (auto it = waiting_.begin(); it != waiting_.end(); ++it) {
            if (!it->gotText || !it->wantExtra) continue;
            Waiting& w = *it;
            const std::string t = text();
            if (!t.empty()) w.text.text = t;
            if (j.contains("language_code") && j["language_code"].is_string()) w.text.language = j["language_code"].get<std::string>();
            if (j.contains("words") && j["words"].is_array()) {
                // Word times either count from the start of the connection or from the start of this
                // phrase; which one isn't promised. Whichever puts the words inside the phrase's own
                // audio is the right one (after the first phrase of a connection the two differ).
                const float shift = float(w.offset) / float(kRate), dur = float(w.length) / float(kRate);
                std::vector<HeardToken> raw;
                for (const json& x : j["words"]) {
                    if (!x.is_object()) continue;
                    HeardToken tk;
                    tk.text = x.value("text", std::string());
                    const std::string kind = x.value("type", std::string("word"));
                    tk.kind = kind == "spacing" ? kTokSpace : kind == "audio_event" ? kTokSound : kTokWord;
                    if (x.contains("start") && x["start"].is_number()) tk.start = x["start"].get<float>();
                    if (x.contains("end") && x["end"].is_number()) tk.end = x["end"].get<float>();
                    if (!tk.text.empty()) raw.push_back(std::move(tk));
                }
                auto inside = [&](float by) {
                    int n = 0;
                    for (const HeardToken& tk : raw)
                        if (tk.kind == kTokWord && tk.start >= 0 && tk.start - by >= -0.3f && tk.end - by <= dur + 0.6f) ++n;
                    return n;
                };
                const bool fromPhrase = shift > 0.5f && inside(0.f) > inside(shift);
                const float by = fromPhrase ? 0.f : shift;
                static bool told = false;
                if (fromPhrase && !told && onLog) {
                    told = true;
                    onLog("Live transcription: word times count from the start of each phrase; using them that way.");
                }
                for (HeardToken& tk : raw) {
                    if (tk.start >= 0) tk.start = std::max(0.f, tk.start - by);
                    if (tk.end >= 0) tk.end = std::max(0.f, tk.end - by);
                    w.text.words.push_back(std::move(tk));
                }
            }
            finish(w, true, "");
            waiting_.erase(it);
            break;
        }
        return;
    }
    if (type == "warning") {
        if (onLog) onLog("Live transcription: " + j.value("warning", std::string()));
        return;
    }
    if (!isError(type)) return;
    const std::string err = type + (j.contains("error") && j["error"].is_string() ? ": " + j["error"].get<std::string>() : "");
    if (type == "insufficient_audio_activity" || type == "commit_throttled" || type == "input_error" ||
        type == "chunk_size_exceeded") {
        // About one phrase: it gets no live text (the regular way hears it), the connection stays.
        for (Waiting& w : waiting_)
            if (!w.gotText) {
                w.gotText = true;
                w.wantExtra = false;
                finish(w, false, err);
                break;
            }
        while (!waiting_.empty() && (waiting_.front().text.ok || !waiting_.front().text.err.empty())) waiting_.pop_front();
        if (type == "input_error" || type == "chunk_size_exceeded") disconnect(err);
        return;
    }
    if (type == "invalid_request") {
        bool switched = false;
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (model_ == "scribe_v3_live") {  // the newer model may not be offered: use the other one
                model_ = "scribe_v2_realtime";
                switched = true;
            }
        }
        disconnect(err);
        if (!switched) giveUpFor(300, err);
        return;
    }
    if (type == "auth_error" || type == "quota_exceeded" || type == "unaccepted_terms") {
        disconnect(err);
        giveUpFor(type == "auth_error" ? 60 : 300, err);
        return;
    }
    // Busy, a time limit, or a problem on their side: this connection ends, the next phrase opens a new one.
    disconnect(err);
    if (++failures_ >= 3) giveUpFor(60, err);
}
