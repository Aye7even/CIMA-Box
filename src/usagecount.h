// What each service was used for, counted on this PC: requests, tokens, letters and seconds of
// sound, by feature (My voice, Listen's captions...). Kept for this session and per day (the days
// are saved to a small file next to the settings), for the Usage page. Nothing here leaves the PC.
//
// Thread-safe: every part of the app counts from its own thread.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The services on the Usage page.
enum UseService { kUseEleven = 0, kUseClaude, kUseGemini, kUseOpenAI, kUseDeepL, kUseMicrosoft, kUseLocal, kUseServices };

// What a request was for.
enum UseFeature {
    kUseMyVoice = 0,   // your own lines: My voice and Text
    kUseCaptions,      // Listen: a finished caption (written down, translated)
    kUseLive,          // Listen: words and sentences while they're still talking
    kUseCheck,         // "Show what my translated line means"
    kUseEar,           // Hear them in my ear (the voice reading captions)
    kUseChanger,       // Changer and Re-speak (voice to voice)
    kUseOther,         // Try a line, screen translation, learning mode, the setup's test
    kUseFeatures
};

struct UseAmount {
    long long requests = 0;
    long long audioMs = 0;       // sound sent to be written down or changed
    long long letters = 0;       // characters sent to be spoken or translated
    long long tokensIn = 0;      // AI: input at full price (not from a cache)
    long long cacheRead = 0;     // AI: input reused from the service's cache
    long long cacheWritten = 0;  // Claude: input newly cached
    long long tokensOut = 0;
    long long workMs = 0;        // a model on this PC: time spent
    double dollars = 0;          // AI: at list price
    void add(const UseAmount& o);
    bool empty() const { return requests == 0 && audioMs == 0 && letters == 0; }
};

// Counts one use. `feature` is a UseFeature (out of range: kUseOther).
void countUse(int service, int feature, const UseAmount& amount);

// The features of one service added up, and each feature on its own.
struct UseTotals {
    UseAmount by[kUseServices][kUseFeatures];
    UseAmount service(int s) const;
};
UseTotals useSession();
// The last `days` days including today (by this PC's calendar), from the saved days and this session.
UseTotals useDays(int days);

// The last requests to a service in this session, newest first (at most 40 kept).
struct UseRecent {
    int64_t at = 0;  // unix time
    int feature = kUseOther;
    UseAmount amount;
};
std::vector<UseRecent> useRecent(int service);

// The saved days: read once at start, written now and then (only when something changed) and at close.
void useLoad(const std::filesystem::path& file);
void useSave(const std::filesystem::path& file);

// Characters (not bytes) in UTF-8 text: what translators count.
size_t utf8Count(const std::string& text);

// List prices per million tokens for the models CIMA Box uses (false when unknown).
bool priceOf(const std::string& model, double& inPerMillion, double& outPerMillion);
// What an AI answer cost at list price. Claude: newly cached input a quarter more, reused a tenth.
double aiDollars(int useService, const std::string& model, long long in, long long cacheRead, long long cacheWritten, long long out);

// The thread's current feature, for code that doesn't get it passed (the translators). Set by
// Translator::translate and the like; ScopedUseFeature puts the old one back.
int currentUseFeature();
struct ScopedUseFeature {
    explicit ScopedUseFeature(int feature);
    ~ScopedUseFeature();
    int previous;
};
