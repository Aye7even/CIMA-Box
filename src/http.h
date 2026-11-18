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
class Http {
public:
    Http();
    ~Http();
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;

    std::atomic<bool>* cancel = nullptr;  // set to true to abort a running request

    // False on a network failure or cancel (err is set). Otherwise `status` is the HTTP status
    // and `body` the response body (empty for a 200 that went to the sink).
    bool perform(const HttpRequest& r, long& status, std::string& body, std::string& err);

    bool lastReusedConnection() const { return reused_; }
    double idleSeconds() const;  // time since the last request finished

    static std::string urlEncode(const std::string& s);

private:
    void* h_ = nullptr;
    bool reused_ = false, used_ = false;
    std::chrono::steady_clock::time_point last_;
};
