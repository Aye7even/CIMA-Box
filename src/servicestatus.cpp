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
    const char* host;   // status page host ("" = none that can be read)
    const char* page;   // what people open
    const char* probe;  // the service itself: any answer (even "no key") means it's up; "" = don't ask
    const char* probe2;
    const char* feed;   // "google" = Google's incident feed, looked at for Gemini
};

// DeepL's and Gemini's status pages can't be read by a program the way the others can, so for
// those two CIMA Box also asks the service itself whether it answers. That is a fact about right
// now, from this PC, which is what matters when a translation just failed.
const Source kSources[] = {
    {"ElevenLabs", "status.elevenlabs.io", "https://status.elevenlabs.io", "", "", ""},
    {"Claude", "status.claude.com", "https://status.claude.com", "", "", ""},
    {"Gemini", "", "https://aistudio.google.com/status", "https://generativelanguage.googleapis.com/v1beta/models", "", "google"},
    {"OpenAI", "status.openai.com", "https://status.openai.com", "", "", ""},
    {"DeepL", "status.deepl.com", "https://status.deepl.com/?tab=api", "https://api-free.deepl.com/v2/languages",
     "https://api.deepl.com/v2/languages", ""},
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

// Instatus: {"page": {"status": "UP|HASISSUES|UNDERMAINTENANCE"}, "activeIncidents": [{"name", "impact"}]}
bool parseInstatus(const std::string& text, int& health, std::string& detail) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("page") || !j["page"].is_object()) return false;
    const std::string st = str(j["page"], "status");
    if (st.empty()) return false;
    health = st == "UP" ? kHealthOk : st == "UNDERMAINTENANCE" ? kHealthMaintenance : kHealthMinor;
    detail.clear();
    if (j.contains("activeIncidents") && j["activeIncidents"].is_array() && !j["activeIncidents"].empty()) {
        const json& inc = j["activeIncidents"][0];
        detail = str(inc, "name");
        if (str(inc, "impact") == "MAJOROUTAGE") health = kHealthMajor;
    }
    if (detail.empty()) detail = health == kHealthOk ? "All Systems Operational" : "Problems reported";
    return true;
}

// Better Stack: {"data": {"attributes": {"aggregate_state": "operational|degraded|downtime|maintenance"}}}
bool parseBetterStack(const std::string& text, int& health, std::string& detail) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("data") || !j["data"].is_object() || !j["data"].contains("attributes")) return false;
    const std::string st = str(j["data"]["attributes"], "aggregate_state");
    if (st.empty()) return false;
    health = st == "operational" ? kHealthOk : st == "maintenance" ? kHealthMaintenance : st == "downtime" ? kHealthMajor : kHealthMinor;
    detail = health == kHealthOk ? "All Systems Operational" : "Problems reported";
    return true;
}

// Google's incident feed: a list of incidents; one without an "end" is still going on. Only
// incidents that name Gemini count.
bool parseGoogleIncidents(const std::string& text, int& health, std::string& detail) {
    const json j = json::parse(text, nullptr, false);
    if (!j.is_array()) return false;
    health = kHealthOk;
    detail.clear();
    for (const json& inc : j) {
        if (!inc.is_object() || !str(inc, "end").empty()) continue;
        bool gemini = str(inc, "service_name").find("Gemini") != std::string::npos;
        if (inc.contains("affected_products") && inc["affected_products"].is_array())
            for (const json& pr : inc["affected_products"])
                if (str(pr, "title").find("Gemini") != std::string::npos) gemini = true;
        if (!gemini) continue;
        const int h = str(inc, "status_impact") == "SERVICE_OUTAGE" ? kHealthMajor : kHealthMinor;
        if (h > health) {
            health = h;
            detail = str(inc, "external_desc");
            if (detail.size() > 160) detail = detail.substr(0, 157) + "...";
        }
    }
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
    // Google's feed is big: read it every third round, and remember the answer in between.
    int round = 0, googleHealth = kHealthUnknown;
    std::string googleDetail;

    bool get(Http& http, const std::string& url, long& status, std::string& body) {
        HttpRequest r;
        r.url = url;
        r.headers = {"Accept: application/json"};
        r.followRedirects = true;
        r.timeoutSec = 10;
        std::string err;
        status = 0;
        body.clear();
        return http.perform(r, status, body, err);
    }

    // The status page, in whichever of the usual formats it speaks.
    bool readPage(Http& http, const std::string& host, int& health, std::string& detail) {
        struct Try { const char* path; bool (*parse)(const std::string&, int&, std::string&); };
        const Try tries[] = {{"/api/v2/status.json", parseStatuspage}, {"/api/v1/summary", parseIncidentIo},
                             {"/summary.json", parseInstatus},         {"/index.json", parseBetterStack}};
        for (const Try& t : tries) {
            long status = 0;
            std::string body;
            if (get(http, "https://" + host + t.path, status, body) && status == 200 && t.parse(body, health, detail)) return true;
            if (!running) return false;
        }
        long status = 0;
        std::string body;
        return get(http, "https://" + host + "/proxy/" + host, status, body) && status == 200 && parseIncidentIo(body, health, detail);
    }

    // Does the service itself answer? 1 = yes (any answer below 500, "no key" included), 0 = no,
    // -1 = this PC couldn't reach it at all (which may be this PC's connection, not the service).
    int answers(Http& http, const char* url) {
        long status = 0;
        std::string body;
        if (!get(http, url, status, body) || status == 0) return -1;
        return status < 500 ? 1 : 0;
    }

    void checkOne(Http& http, const Source& src, ServiceStatus& out) {
        out.health = kHealthUnknown;
        out.detail = "Couldn't check right now";
        int h = kHealthUnknown;
        std::string d;
        const bool page = *src.host && readPage(http, src.host, h, d);
        if (page) {
            out.health = h;
            out.detail = d;
        }
        if (std::string(src.feed) == "google") {
            if (round % 3 == 0 || googleHealth == kHealthUnknown) {
                long status = 0;
                std::string body;
                if (get(http, "https://status.cloud.google.com/incidents.json", status, body) && status == 200)
                    if (!parseGoogleIncidents(body, googleHealth, googleDetail)) googleHealth = kHealthUnknown;
            }
            if (googleHealth != kHealthUnknown && googleHealth != kHealthOk) {
                out.health = googleHealth;
                out.detail = googleDetail.empty() ? "Google reports a problem with Gemini" : googleDetail;
            }
        }
        if (!*src.probe) return;
        const int a = answers(http, src.probe), b = *src.probe2 ? answers(http, src.probe2) : a;
        if (a == 0 && b == 0) {  // the service itself is failing: that outranks whatever a page says
            out.health = kHealthMajor;
            out.detail = std::string(src.name) + " isn't answering right now";
        } else if (a == 0 || b == 0) {
            if (out.health == kHealthUnknown || out.health == kHealthOk) {
                out.health = kHealthMinor;
                out.detail = std::string(src.name) + (a == 0 ? " Free" : " Pro") + " isn't answering right now";
            }
        } else if (a == 1 && b == 1 && out.health == kHealthUnknown) {
            out.health = kHealthOk;
            out.detail = "Answering normally (checked from this PC)";
        }
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
            ++round;
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
