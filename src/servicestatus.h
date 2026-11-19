// Is ElevenLabs (or Claude, Gemini, DeepL) having problems right now? Reads each company's public
// status page now and then, so a failing request can be told apart from a problem in the app.
// The usual status page systems are understood (Statuspage, incident.io, Instatus, Better Stack).
// DeepL and Gemini are also asked directly whether they answer, and Google's incident feed is read
// for Gemini. A service that can't be checked shows as "unknown", never as fine.
#pragma once
#include <memory>
#include <string>
#include <vector>

enum ServiceHealth { kHealthUnknown = 0, kHealthOk, kHealthMaintenance, kHealthMinor, kHealthMajor };

struct ServiceStatus {
    std::string name;    // "ElevenLabs"
    std::string page;    // the status page people can open
    int health = kHealthUnknown;
    std::string detail;  // "All Systems Operational", or the current incident's title
};

class StatusWatcher {
public:
    StatusWatcher();
    ~StatusWatcher();
    StatusWatcher(const StatusWatcher&) = delete;
    StatusWatcher& operator=(const StatusWatcher&) = delete;

    void start();                          // checks now, then every 5 minutes
    void stop();
    void checkSoon();                      // e.g. after a request failed (at most once a minute)
    std::vector<ServiceStatus> state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// The two formats, exposed for testing. False if the text isn't that format.
bool parseStatuspage(const std::string& json, int& health, std::string& detail);
bool parseIncidentIo(const std::string& json, int& health, std::string& detail);
bool parseInstatus(const std::string& json, int& health, std::string& detail);
bool parseBetterStack(const std::string& json, int& health, std::string& detail);
bool parseGoogleIncidents(const std::string& json, int& health, std::string& detail);
