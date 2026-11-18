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

// List prices per million tokens (short prompts, which translations always are).
bool priceOf(const std::string& model, double& in, double& out) {
    if (model.empty() || model.find("claude-haiku-5-5") == 0) { in = 0.10; out = 0.50; return true; }
    if (model.find("gemini-3.5-flash-lite") == 0) { in = 0.30; out = 2.50; return true; }
    return false;
}

AiSpend aiSpend(bool claude, const std::string& model) {
    AiSpend s;
    aiTokens(claude ? kClaude : kGemini, s.tokensIn, s.tokensOut);
    double in, out;
    const std::string m = model.empty() ? (claude ? "claude-haiku-5-5" : "gemini-3.5-flash-lite") : model;
    s.dollars = priceOf(m, in, out) ? (double(s.tokensIn) * in + double(s.tokensOut) * out) / 1e6 : -1.0;
    return s;
}

struct UsageWatcher::Impl {
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::thread thread;
    std::atomic<bool> running{false}, cancel{false};
    std::string elevenKey, deeplKey;
    bool wake = false;
    Quota el, dl;
    std::chrono::steady_clock::time_point last{};

    void read(Http& http, const std::string& key, bool isEleven, Quota& q) {
        if (key.empty()) { q = Quota(); return; }
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
            logLine(std::string(name) + " usage: HTTP " + std::to_string(status));
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

Quota UsageWatcher::eleven() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->el;
}

Quota UsageWatcher::deepl() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->dl;
}
