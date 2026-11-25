#include "usage.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "applog.h"
#include "http.h"
#include "translate.h"

using json = nlohmann::json;

namespace {

long long num(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return -1;
    const json& v = j[key];
    if (v.is_number_integer()) return v.get<long long>();
    if (v.is_number()) return (long long)v.get<double>();
    return -1;
}

std::string keyOf(const std::string& k) {  // trimmed, like the requests themselves
    size_t a = k.find_first_not_of(" \t\r\n"), b = k.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : k.substr(a, b - a + 1);
}

}  // namespace

bool parseElevenSubscription(const std::string& text, Quota& q) {
    const json j = json::parse(text, nullptr, false);
    const long long used = num(j, "character_count"), limit = num(j, "character_limit");
    if (used < 0 || limit < 0) return false;
    q.known = true;
    q.used = used;
    q.limit = limit;
    const long long reset = num(j, "next_character_count_reset_unix");
    q.resetsAt = reset > 0 ? reset : 0;
    q.problem.clear();
    return true;
}

// ElevenLabs' usage by model and day: {"time": [ms, ...], "usage": {"scribe_v3": [credits per day], ...}}.
bool parseElevenDays(const std::string& text, std::vector<ElevenDay>& days) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("time") || !j["time"].is_array() || !j.contains("usage") || !j["usage"].is_object()) return false;
    days.assign(j["time"].size(), ElevenDay());
    for (size_t i = 0; i < days.size(); ++i)
        days[i].start = j["time"][i].is_number() ? int64_t(j["time"][i].get<double>() / 1000.0) : 0;
    for (auto it = j["usage"].begin(); it != j["usage"].end(); ++it) {
        if (!it.value().is_array()) continue;
        const std::string m = it.key();
        auto has = [&](const char* s) { return m.find(s) != std::string::npos; };
        for (size_t i = 0; i < days.size() && i < it.value().size(); ++i) {
            const double v = it.value()[i].is_number() ? it.value()[i].get<double>() : 0.0;
            ElevenDay& d = days[i];
            if (has("scribe") && has("live")) d.live += v;           // words while someone talks
            else if (has("scribe")) d.written += v;                  // speech written down
            else if (has("sts")) d.changer += v;                     // voice to voice
            else if (m.rfind("eleven_", 0) == 0) d.voice += v;       // a voice speaking text
            else d.other += v;                                       // music, sound effects, other products
        }
    }
    return true;
}

bool parseDeeplUsage(const std::string& text, Quota& q) {
    const json j = json::parse(text, nullptr, false);
    const long long used = num(j, "character_count"), limit = num(j, "character_limit");
    if (used < 0 || limit < 0) return false;
    q.known = true;
    q.used = used;
    q.limit = limit;
    q.resetsAt = 0;
    q.problem.clear();
    return true;
}

AiSpend aiSpend(int service, const std::string& model) {
    AiSpend s;
    aiTokens(service, s.tokensIn, s.tokensOut);
    double in, out;
    const std::string m = !model.empty()        ? model
                          : service == kClaude ? "claude-haiku-5-5"
                          : service == kGemini ? "gemini-3.5-flash-lite"
                                               : "gpt-6-luna";
    s.dollars = priceOf(m, in, out) ? (double(s.tokensIn) * in + double(s.tokensOut) * out) / 1e6 : -1.0;
    return s;
}

struct UsageWatcher::Impl {
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::thread thread;
    std::atomic<bool> running{false}, cancel{false};
    std::string elevenKey, deeplKey;
    std::string refusedEl, refusedDl;  // keys that got 401/403 here (only the loop thread uses these)
    bool wake = false;
    Quota el, dl;
    std::vector<ElevenDay> elDays;
    std::chrono::steady_clock::time_point last{}, lastDays{};
    std::string daysKey;  // the key elDays were read with

    // The last 7 days of credits by model, for the Usage page. Every half hour at most.
    void readDays(Http& http, const std::string& key) {
        if (key.empty() || key == refusedEl) return;
        const auto now = std::chrono::steady_clock::now();
        if (key == daysKey && lastDays != std::chrono::steady_clock::time_point{} && now - lastDays < std::chrono::minutes(30)) return;
        const long long end = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const long long start = end - 7LL * 24 * 3600 * 1000;
        HttpRequest r;
        r.url = "https://api.elevenlabs.io/v1/usage/character-stats?start_unix=" + std::to_string(start) + "&end_unix=" + std::to_string(end) +
                "&aggregation_interval=day&breakdown_type=model";
        r.headers = {"xi-api-key: " + key, "Accept: application/json"};
        r.timeoutSec = 15;
        long status = 0;
        std::string body, err;
        std::vector<ElevenDay> days;
        if (!http.perform(r, status, body, err) || status != 200 || !parseElevenDays(body, days)) {
            if (status != 0 && status != 200) logLine("ElevenLabs usage by day: HTTP " + std::to_string(status));
            std::lock_guard<std::mutex> lk(mtx);
            lastDays = now;  // tried: not again for a while
            return;
        }
        std::lock_guard<std::mutex> lk(mtx);
        elDays = days;
        daysKey = key;
        lastDays = now;
    }

    void read(Http& http, const std::string& key, bool isEleven, Quota& q) {
        if (key.empty()) { q = Quota(); return; }
        // This key was already refused: it won't start working by itself, so don't ask (and log) again
        // every time. A different key is checked as usual.
        if (key == (isEleven ? refusedEl : refusedDl)) return;
        HttpRequest r;
        if (isEleven) {
            r.url = "https://api.elevenlabs.io/v1/user/subscription";
            r.headers = {"xi-api-key: " + key, "Accept: application/json"};
        } else {
            r.url = deeplHost(key) + "/v2/usage";
            r.headers = {"Authorization: DeepL-Auth-Key " + key};
        }
        r.timeoutSec = 15;
        long status = 0;
        std::string body, err;
        const char* name = isEleven ? "ElevenLabs" : "DeepL";
        if (!http.perform(r, status, body, err)) {
            q.problem = "Couldn't check";
            return;
        }
        if (status == 401 || status == 403) {
            // ElevenLabs keys can be made without permission to read the account.
            q = Quota();
            q.problem = isEleven ? "This key isn't allowed to read usage (it needs User: Read access)."
                                 : "The DeepL key was refused.";
            logLine(std::string(name) + " usage: HTTP " + std::to_string(status) +
                    (isEleven ? " (this key can't read the account's credits; everything else still works)" : ""));
            (isEleven ? refusedEl : refusedDl) = key;
            return;
        }
        if (status != 200 || !(isEleven ? parseElevenSubscription(body, q) : parseDeeplUsage(body, q))) {
            q.problem = "Couldn't check";
            logLine(std::string(name) + " usage: HTTP " + std::to_string(status));
        }
    }

    void loop() {
        Http http;
        http.cancel = &cancel;
        std::string seenEl, seenDl;
        while (running) {
            std::string ek, dk;
            {
                std::lock_guard<std::mutex> lk(mtx);
                ek = elevenKey;
                dk = deeplKey;
            }
            Quota e, d;
            {
                std::lock_guard<std::mutex> lk(mtx);
                e = ek == seenEl ? el : Quota();  // a new key starts from nothing
                d = dk == seenDl ? dl : Quota();
            }
            read(http, ek, true, e);
            if (!running) return;
            read(http, dk, false, d);
            if (!running) return;
            readDays(http, ek);
            seenEl = ek;
            seenDl = dk;
            std::unique_lock<std::mutex> lk(mtx);
            if (ek == elevenKey) el = e;
            if (dk == deeplKey) dl = d;
            last = std::chrono::steady_clock::now();
            wake = false;
            cv.wait_for(lk, std::chrono::minutes(5), [this] { return !running || wake; });
        }
    }
};

UsageWatcher::UsageWatcher() : p_(new Impl) {}
UsageWatcher::~UsageWatcher() { stop(); }

void UsageWatcher::start() {
    if (p_->running.exchange(true)) return;
    p_->thread = std::thread([this] { p_->loop(); });
}

void UsageWatcher::stop() {
    if (p_->running.exchange(false)) {
        p_->cancel = true;
        p_->cv.notify_all();
        p_->thread.join();
    }
}

void UsageWatcher::setKeys(const std::string& eleven, const std::string& deepl) {
    const std::string e = keyOf(eleven), d = keyOf(deepl);
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (e == p_->elevenKey && d == p_->deeplKey) return;
    p_->elevenKey = e;
    p_->deeplKey = d;
    p_->wake = true;
    p_->cv.notify_all();
}

void UsageWatcher::refreshSoon() {
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (std::chrono::steady_clock::now() - p_->last < std::chrono::seconds(30)) return;
    p_->wake = true;
    p_->cv.notify_all();
}

std::vector<ElevenDay> UsageWatcher::elevenDays() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->daysKey == p_->elevenKey ? p_->elDays : std::vector<ElevenDay>();
}

Quota UsageWatcher::eleven() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->el;
}

Quota UsageWatcher::deepl() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->dl;
}
