// Small libcurl wrapper that keeps its connection open between requests.
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

struct HttpPart { std::string name, data, filename, mime; };
using ByteSink = std::function<void(const char*, size_t)>;

struct HttpRequest {
    std::string url;
    std::vector<std::string> headers;
    const std::vector<HttpPart>* parts = nullptr;  // multipart POST
    const std::string* body = nullptr;             // POST with this body
    const ByteSink* sink = nullptr;                // receives a 200 response as it streams in
    bool headOnly = false;
    long timeoutSec = 120;
    long timeoutMs = 0;  // when set, used instead of timeoutSec (whole request, in milliseconds)
    bool followRedirects = false;  // e.g. GitHub release downloads, which redirect to a file server
};

// Not thread-safe: use one Http per thread.
class Http;

// Sends a request on `main`. If the answer is slow, a second copy goes out on `spare` (its own
// connection) and the first good answer is kept; the other copy is stopped. A slow answer is usually
// one stuck at the service for a moment, and a fresh copy then often comes back sooner.
// `shot(h, slot)` makes the request on connection h (slot 0 = first copy, 1 = second) and returns true
// for a good answer. `waitMs`: how long to wait for the answer once the request is sent before
// sending the second copy (0 = never). `keepWaiting(slot 0's failure)`: false when the first copy's
// failure means the second won't help either (it's stopped then). Returns the slot that answered
// well, or -1.
int sendWithSpare(Http& main, Http& spare, int waitMs, const std::function<bool(Http&, int)>& shot,
                  const std::function<bool()>& keepWaiting);

class Http {
public:
    Http();
    ~Http();
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;

    std::atomic<bool>* cancel = nullptr;  // set to true to abort a running request
    std::atomic<bool>* stop = nullptr;    // a second flag that also aborts it (e.g. the slower of two copies)

    // While a request runs: when its whole body had been sent (steady-clock ms; 0 = not yet, or unknown).
    std::atomic<long long> sentAtMs{0};

    // False on a network failure or cancel (err is set). Otherwise `status` is the HTTP status
    // and `body` the response body (empty for a 200 that went to the sink).
    bool perform(const HttpRequest& r, long& status, std::string& body, std::string& err);

    bool lastReusedConnection() const { return reused_; }
    double idleSeconds() const;  // time since the last request finished

    static std::string urlEncode(const std::string& s);

private:
    void* h_ = nullptr;
    void* m_ = nullptr;  // runs h_, so a stop or cancel is noticed within 10 ms (and keeps the open connections)
    bool reused_ = false, used_ = false;
    std::chrono::steady_clock::time_point last_;
};
