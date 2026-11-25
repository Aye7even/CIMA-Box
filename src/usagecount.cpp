#include "usagecount.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>

using json = nlohmann::json;

void UseAmount::add(const UseAmount& o) {
    requests += o.requests;
    audioMs += o.audioMs;
    letters += o.letters;
    tokensIn += o.tokensIn;
    cacheRead += o.cacheRead;
    cacheWritten += o.cacheWritten;
    tokensOut += o.tokensOut;
    workMs += o.workMs;
    dollars += o.dollars;
}

UseAmount UseTotals::service(int s) const {
    UseAmount a;
    if (s < 0 || s >= kUseServices) return a;
    for (int f = 0; f < kUseFeatures; ++f) a.add(by[s][f]);
    return a;
}

namespace {

const char* const kServiceKeys[kUseServices] = {"elevenlabs", "claude", "gemini", "openai", "deepl", "microsoft", "local"};
const char* const kFeatureKeys[kUseFeatures] = {"my_voice", "captions", "live", "check", "ear", "changer", "other"};

std::mutex gMx;
UseTotals gSession;
std::map<std::string, UseTotals> gDays;  // "2026-11-24" -> that day (saved days, and today as it goes)
std::deque<UseRecent> gRecent[kUseServices];
bool gDirty = false;

std::string dayOf(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::strftime(b, sizeof(b), "%Y-%m-%d", &tm);
    return b;
}

json toJson(const UseAmount& a) {
    json j = json::object();
    auto put = [&](const char* k, long long v) { if (v) j[k] = v; };
    put("requests", a.requests);
    put("audio_ms", a.audioMs);
    put("letters", a.letters);
    put("tokens_in", a.tokensIn);
    put("cache_read", a.cacheRead);
    put("cache_written", a.cacheWritten);
    put("tokens_out", a.tokensOut);
    put("work_ms", a.workMs);
    if (a.dollars > 0) j["dollars"] = a.dollars;
    return j;
}

UseAmount fromJson(const json& j) {
    UseAmount a;
    if (!j.is_object()) return a;
    a.requests = j.value("requests", 0LL);
    a.audioMs = j.value("audio_ms", 0LL);
    a.letters = j.value("letters", 0LL);
    a.tokensIn = j.value("tokens_in", 0LL);
    a.cacheRead = j.value("cache_read", 0LL);
    a.cacheWritten = j.value("cache_written", 0LL);
    a.tokensOut = j.value("tokens_out", 0LL);
    a.workMs = j.value("work_ms", 0LL);
    a.dollars = j.value("dollars", 0.0);
    return a;
}

thread_local int tFeature = kUseOther;

}  // namespace

void countUse(int service, int feature, const UseAmount& amount) {
    if (service < 0 || service >= kUseServices) return;
    if (feature < 0 || feature >= kUseFeatures) feature = kUseOther;
    const std::time_t now = std::time(nullptr);
    std::lock_guard<std::mutex> lk(gMx);
    gSession.by[service][feature].add(amount);
    gDays[dayOf(now)].by[service][feature].add(amount);
    if (amount.requests > 0) {
        UseRecent r;
        r.at = int64_t(now);
        r.feature = feature;
        r.amount = amount;
        gRecent[service].push_front(r);
        while (gRecent[service].size() > 40) gRecent[service].pop_back();
    }
    gDirty = true;
}

UseTotals useSession() {
    std::lock_guard<std::mutex> lk(gMx);
    return gSession;
}

UseTotals useDays(int days) {
    UseTotals t;
    const std::time_t now = std::time(nullptr);
    std::lock_guard<std::mutex> lk(gMx);
    for (int d = 0; d < std::max(1, days); ++d) {
        const auto it = gDays.find(dayOf(now - std::time_t(d) * 86400));
        if (it == gDays.end()) continue;
        for (int s = 0; s < kUseServices; ++s)
            for (int f = 0; f < kUseFeatures; ++f) t.by[s][f].add(it->second.by[s][f]);
    }
    return t;
}

std::vector<UseRecent> useRecent(int service) {
    if (service < 0 || service >= kUseServices) return {};
    std::lock_guard<std::mutex> lk(gMx);
    return std::vector<UseRecent>(gRecent[service].begin(), gRecent[service].end());
}

void useLoad(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object() || !j.contains("days") || !j["days"].is_object()) return;
    std::lock_guard<std::mutex> lk(gMx);
    for (auto day = j["days"].begin(); day != j["days"].end(); ++day) {
        if (!day.value().is_object()) continue;
        UseTotals& t = gDays[day.key()];
        for (int s = 0; s < kUseServices; ++s) {
            if (!day.value().contains(kServiceKeys[s])) continue;
            const json& sj = day.value()[kServiceKeys[s]];
            for (int f = 0; f < kUseFeatures; ++f)
                if (sj.is_object() && sj.contains(kFeatureKeys[f])) t.by[s][f].add(fromJson(sj[kFeatureKeys[f]]));
        }
    }
}

void useSave(const std::filesystem::path& file) {
    json days = json::object();
    {
        std::lock_guard<std::mutex> lk(gMx);
        if (!gDirty) return;
        // Two months are kept; older days are dropped.
        const std::string oldest = dayOf(std::time(nullptr) - std::time_t(62) * 86400);
        for (auto it = gDays.begin(); it != gDays.end();) {
            if (it->first < oldest) it = gDays.erase(it);
            else ++it;
        }
        for (const auto& d : gDays) {
            json dj = json::object();
            for (int s = 0; s < kUseServices; ++s) {
                json sj = json::object();
                for (int f = 0; f < kUseFeatures; ++f)
                    if (!d.second.by[s][f].empty()) sj[kFeatureKeys[f]] = toJson(d.second.by[s][f]);
                if (!sj.empty()) dj[kServiceKeys[s]] = sj;
            }
            if (!dj.empty()) days[d.first] = dj;
        }
        gDirty = false;
    }
    const std::filesystem::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << json({{"v", 1}, {"days", days}}).dump(1);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

size_t utf8Count(const std::string& text) {
    size_t n = 0;
    for (unsigned char c : text) n += (c & 0xC0) != 0x80;
    return n;
}

// List prices per million tokens (short prompts, which translations always are).
bool priceOf(const std::string& model, double& in, double& out) {
    if (model.empty() || model.find("claude-haiku-5-5") == 0) { in = 0.10; out = 0.50; return true; }
    if (model.find("gemini-3.5-flash-lite") == 0) { in = 0.30; out = 2.50; return true; }
    if (model.find("gpt-6-luna") == 0) { in = 0.10; out = 0.50; return true; }
    return false;
}

double aiDollars(int useService, const std::string& model, long long in, long long cacheRead, long long cacheWritten, long long out) {
    double pi = 0, po = 0;
    if (!priceOf(model, pi, po)) return 0;
    double input = double(in);
    if (useService == kUseClaude) input += double(cacheWritten) * 1.25 + double(cacheRead) * 0.1;
    else input += double(cacheRead);  // Gemini and OpenAI charge less for it; counted at full price here, to be safe
    return (input * pi + double(out) * po) / 1e6;
}

int currentUseFeature() { return tFeature; }
ScopedUseFeature::ScopedUseFeature(int feature) : previous(tFeature) { tFeature = feature; }
ScopedUseFeature::~ScopedUseFeature() { tFeature = previous; }
