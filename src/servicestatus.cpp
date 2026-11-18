#include "servicestatus.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "http.h"

using json = nlohmann::json;

namespace {

struct Source {
    const char* name;
    const char* host;  // status page host; empty = no machine-readable page, just the link
    const char* page;  // what people open
};

const Source kSources[] = {
    {"ElevenLabs", "status.elevenlabs.io", "https://status.elevenlabs.io"},
    {"Claude", "status.claude.com", "https://status.claude.com"},
    {"Gemini", "", "https://aistudio.google.com/status"},
    {"DeepL", "status.deepl.com", "https://status.deepl.com/?tab=api"},
};

std::string str(const json& j, const char* key) {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

int impactHealth(const std::string& impact) {
    if (impact == "full_outage" || impact == "partial_outage") return kHealthMajor;
    if (impact == "degraded_performance") return kHealthMinor;
    if (impact == "under_maintenance") return kHealthMaintenance;
    return kHealthOk;
}

}  // namespace

// {"status": {"indicator": "none|minor|major|critical|maintenance", "description": "..."}}
bool parseStatuspage(const std::string& text, int& health, std::string& detail) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("status") || !j["status"].is_object()) return false;
    const std::string ind = str(j["status"], "indicator");
    if (ind.empty()) return false;
    health = ind == "none" ? kHealthOk : ind == "minor" ? kHealthMinor : ind == "maintenance" ? kHealthMaintenance : kHealthMajor;
    detail = str(j["status"], "description");
    if (detail.empty()) detail = health == kHealthOk ? "All Systems Operational" : "Problems reported";
    return true;
}

// {"ongoing_incidents": [{"name", "current_worst_impact", ...}], "in_progress_maintenances": [...]}
// (the older "/proxy/<host>" form wraps the same in {"summary": {...}}).
bool parseIncidentIo(const std::string& text, int& health, std::string& detail) {
    json j = json::parse(text, nullptr, false);
    if (j.is_object() && j.contains("summary") && j["summary"].is_object()) j = j["summary"];
    if (!j.is_object() || !j.contains("ongoing_incidents") || !j["ongoing_incidents"].is_array()) return false;
    health = kHealthOk;
    detail.clear();
    for (const json& inc : j["ongoing_incidents"]) {
        int h = impactHealth(str(inc, "current_worst_impact"));
        if (h == kHealthOk) h = kHealthMinor;  // an open incident with no impact given: still worth a note
        if (h > health) {
            health = h;
            detail = str(inc, "name");
        }
    }
    if (health == kHealthOk && j.contains("in_progress_maintenances") && j["in_progress_maintenances"].is_array() &&
        !j["in_progress_maintenances"].empty()) {
        health = kHealthMaintenance;
        detail = str(j["in_progress_maintenances"][0], "name");
    }
    if (detail.empty()) detail = health == kHealthOk ? "All Systems Operational" : "Problems reported";
    return true;
}

struct StatusWatcher::Impl {
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::vector<ServiceStatus> st;
    std::thread thread;
    std::atomic<bool> running{false}, cancel{false};
    bool soon = false;
    std::chrono::steady_clock::time_point last{};

    void checkOne(Http& http, const Source& src, ServiceStatus& out) {
        out.health = kHealthUnknown;
        out.detail = "Open the status page to check";
        if (!*src.host) return;
        const std::string host = src.host;
        const std::string urls[] = {"https://" + host + "/api/v2/status.json", "https://" + host + "/api/v1/summary",
                                    "https://" + host + "/proxy/" + host};
        for (int i = 0; i < 3; ++i) {
            HttpRequest r;
            r.url = urls[i];
            r.headers = {"Accept: application/json"};
            r.followRedirects = true;
            r.timeoutSec = 10;
            long status = 0;
            std::string body, err;
            if (!http.perform(r, status, body, err) || status != 200) continue;
            int h;
            std::string d;
            if ((i == 0 && parseStatuspage(body, h, d)) || (i > 0 && parseIncidentIo(body, h, d))) {
                out.health = h;
                out.detail = d;
                return;
            }
        }
        out.detail = "Couldn't read the status page";
    }

    void loop() {
        Http http;
        http.cancel = &cancel;
        while (running) {
            std::vector<ServiceStatus> fresh;
            for (const Source& src : kSources) {
                ServiceStatus s;
                s.name = src.name;
                s.page = src.page;
                if (!running) return;
                checkOne(http, src, s);
                fresh.push_back(s);
            }
            std::unique_lock<std::mutex> lk(mtx);
            st = fresh;
            last = std::chrono::steady_clock::now();
            soon = false;
            cv.wait_for(lk, std::chrono::minutes(5), [this] { return !running || soon; });
        }
    }
};

StatusWatcher::StatusWatcher() : p_(new Impl) {
    for (const Source& src : kSources) {
        ServiceStatus s;
        s.name = src.name;
        s.page = src.page;
        s.detail = "Checking";
        p_->st.push_back(s);
    }
}

StatusWatcher::~StatusWatcher() { stop(); }

void StatusWatcher::stop() {
    if (p_->running.exchange(false)) {
        p_->cancel = true;
        p_->cv.notify_all();
        p_->thread.join();
    }
}

void StatusWatcher::start() {
    if (p_->running.exchange(true)) return;
    p_->thread = std::thread([this] { p_->loop(); });
}

void StatusWatcher::checkSoon() {
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (std::chrono::steady_clock::now() - p_->last < std::chrono::minutes(1)) return;
    p_->soon = true;
    p_->cv.notify_all();
}

std::vector<ServiceStatus> StatusWatcher::state() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->st;
}
