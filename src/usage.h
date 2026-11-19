// How much is left: ElevenLabs credits and DeepL characters, read from their accounts now and then.
// Claude, Gemini and OpenAI have no way to ask that with a normal key, so for them the app counts what
// it used itself in this run (tokens, and the price that comes to).
#pragma once
#include <cstdint>
#include <memory>
#include <string>

struct Quota {
    bool known = false;      // read successfully at least once
    long long used = 0, limit = 0;
    int64_t resetsAt = 0;    // unix time, 0 = not given
    std::string problem;     // e.g. the key may not read usage
};

struct AiSpend {
    long long tokensIn = 0, tokensOut = 0;
    double dollars = 0;      // at list price; < 0 when the model's price isn't known
};

class UsageWatcher {
public:
    UsageWatcher();
    ~UsageWatcher();
    UsageWatcher(const UsageWatcher&) = delete;
    UsageWatcher& operator=(const UsageWatcher&) = delete;

    void start();
    void stop();
    void setKeys(const std::string& eleven, const std::string& deepl);  // a changed key is read right away
    void refreshSoon();                                                  // after use (at most every 30 s)
    Quota eleven() const;
    Quota deepl() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Tokens an AI service (kClaude, kGemini, kOpenAI) used in this run, as counted from its answers
// (see translate.cpp).
AiSpend aiSpend(int service, const std::string& model);

bool parseElevenSubscription(const std::string& json, Quota& q);
bool parseDeeplUsage(const std::string& json, Quota& q);
bool priceOf(const std::string& model, double& inPerMillion, double& outPerMillion);
