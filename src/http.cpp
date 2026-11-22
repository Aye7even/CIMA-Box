#include "http.h"

#include <cstdio>
#include <future>
#include <thread>
#include <curl/curl.h>

namespace {

struct Transfer {
    CURL* h = nullptr;
    const ByteSink* sink = nullptr;
    std::string body;
    std::atomic<bool>* cancel = nullptr;
    std::atomic<bool>* stop = nullptr;
    std::atomic<long long>* sentAt = nullptr;
};

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

size_t onWrite(char* p, size_t sz, size_t n, void* ud) {
    auto* t = static_cast<Transfer*>(ud);
    long code = 0;
    curl_easy_getinfo(t->h, CURLINFO_RESPONSE_CODE, &code);
    if (t->sink && (code == 200 || code == 206)) (*t->sink)(p, sz * n);
    else if (t->body.size() < (8u << 20)) t->body.append(p, sz * n);
    return sz * n;
}

int onProgress(void* ud, curl_off_t, curl_off_t, curl_off_t ultotal, curl_off_t ulnow) {
    auto* t = static_cast<Transfer*>(ud);
    if (t->sentAt && ultotal > 0 && ulnow >= ultotal && t->sentAt->load() == 0) t->sentAt->store(nowMs());
    return (t->cancel && t->cancel->load()) || (t->stop && t->stop->load()) ? 1 : 0;
}

}  // namespace

Http::Http() {
    static const bool inited = (curl_global_init(CURL_GLOBAL_DEFAULT), true);
    (void)inited;
    h_ = curl_easy_init();
    m_ = curl_multi_init();
}

Http::~Http() {
    if (m_) curl_multi_cleanup(static_cast<CURLM*>(m_));
    if (h_) curl_easy_cleanup(static_cast<CURL*>(h_));
}

double Http::idleSeconds() const {
    if (!used_) return 1e9;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - last_).count();
}

std::string Http::urlEncode(const std::string& s) {
    std::string out;
    char buf[4];
    for (unsigned char ch : s) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out += char(ch);
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", ch);
            out += buf;
        }
    }
    return out;
}

bool Http::perform(const HttpRequest& r, long& status, std::string& body, std::string& err) {
    status = 0;
    body.clear();
    CURL* h = static_cast<CURL*>(h_);
    if (!h) { err = "curl init failed"; return false; }
    curl_easy_reset(h);  // keeps the open connections, DNS cache and TLS sessions

    Transfer t;
    t.h = h; t.sink = r.sink; t.cancel = cancel; t.stop = stop; t.sentAt = &sentAtMs;
    sentAtMs = 0;

    curl_slist* hdrs = nullptr;
    for (const std::string& line : r.headers) hdrs = curl_slist_append(hdrs, line.c_str());
    // Never "Expect: 100-continue" (curl adds it to big uploads): it waits a round trip, or up to a
    // second on some networks, before sending the body.
    if (r.body || r.parts) hdrs = curl_slist_append(hdrs, "Expect:");
    curl_mime* mime = nullptr;
    if (r.parts) {
        mime = curl_mime_init(h);
        for (const HttpPart& p : *r.parts) {
            curl_mimepart* mp = curl_mime_addpart(mime);
            curl_mime_name(mp, p.name.c_str());
            curl_mime_data(mp, p.data.data(), p.data.size());
            if (!p.filename.empty()) curl_mime_filename(mp, p.filename.c_str());
            if (!p.mime.empty()) curl_mime_type(mp, p.mime.c_str());
        }
        curl_easy_setopt(h, CURLOPT_MIMEPOST, mime);
    } else if (r.body) {
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, r.body->c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(r.body->size()));
    } else if (r.headOnly) {
        curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    }
    curl_easy_setopt(h, CURLOPT_URL, r.url.c_str());
    if (hdrs) curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, onWrite);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &t);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 10L);
    if (r.timeoutMs > 0) curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, r.timeoutMs);
    else curl_easy_setopt(h, CURLOPT_TIMEOUT, r.timeoutSec);
    curl_easy_setopt(h, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "CIMABox");  // GitHub's API refuses requests without one
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, r.followRedirects ? 1L : 0L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, r.stallSec > 0 ? 1024L : 0L);
    curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, r.stallSec > 0 ? r.stallSec : 0L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);

    // Run the request ourselves, checking every 10 ms whether it was stopped or cancelled. (Inside
    // curl_easy_perform that check can come only once a second while waiting for an answer.)
    CURLcode rc = CURLE_OK;
    CURLM* m = static_cast<CURLM*>(m_);
    if (!m) {
        rc = curl_easy_perform(h);
    } else {
        bool stopped = false, failed = false, late = false;
        const long long began = nowMs();
        curl_multi_add_handle(m, h);
        for (;;) {
            int running = 0;
            if (curl_multi_perform(m, &running) != CURLM_OK) { failed = true; break; }
            if (sentAtMs.load() == 0) {  // the whole request has been sent: noted for sendWithSpare
                curl_off_t sent = 0, total = 0;
                curl_easy_getinfo(h, CURLINFO_SIZE_UPLOAD_T, &sent);
                curl_easy_getinfo(h, CURLINFO_CONTENT_LENGTH_UPLOAD_T, &total);
                if (total > 0 && sent >= total) sentAtMs = nowMs();
            }
            if (!running) break;
            if ((cancel && cancel->load()) || (stop && stop->load())) { stopped = true; break; }
            // The answer hasn't begun by the time it had to (see HttpRequest::started): too slow.
            if (r.startByMs > 0 && r.started && !r.started->load() && nowMs() - began > r.startByMs) { late = true; break; }
            curl_multi_poll(m, nullptr, 0, 10, nullptr);
        }
        if (stopped) rc = CURLE_ABORTED_BY_CALLBACK;
        else if (late) rc = CURLE_OPERATION_TIMEDOUT;
        else if (failed) rc = CURLE_FAILED_INIT;
        else {
            int left = 0;
            while (CURLMsg* msg = curl_multi_info_read(m, &left))
                if (msg->msg == CURLMSG_DONE && msg->easy_handle == h) rc = msg->data.result;
        }
        curl_multi_remove_handle(m, h);
    }
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    long newConnections = 1;
    curl_easy_getinfo(h, CURLINFO_NUM_CONNECTS, &newConnections);
    reused_ = (rc == CURLE_OK && newConnections == 0);
    used_ = true;
    last_ = std::chrono::steady_clock::now();
    if (mime) curl_mime_free(mime);
    curl_slist_free_all(hdrs);

    if (rc == CURLE_ABORTED_BY_CALLBACK) { err = "cancelled"; return false; }
    if (rc != CURLE_OK) { err = std::string("Network error: ") + curl_easy_strerror(rc); return false; }
    body.swap(t.body);
    return true;
}

int sendWithSpare(Http& main, Http& spare, int waitMs, const std::function<bool(Http&, int)>& shot,
                  const std::function<bool()>& keepWaiting) {
    std::atomic<bool> stop0{false}, stop1{false}, done0{false};
    std::atomic<int> result1{0};  // 0 not started or still going, 1 good, 2 failed
    std::atomic<bool>* const oldStop = main.stop;
    main.stop = &stop0;
    spare.cancel = main.cancel;
    spare.stop = &stop1;
    const long long started = nowMs();
    std::future<void> second;
    if (waitMs > 0)
        second = std::async(std::launch::async, [&] {
            // Wait for the answer: `waitMs` after the request was sent (the upload finished), or, if it
            // never seems to finish sending, a while longer (the connection itself may be stuck).
            for (;;) {
                if (done0 || stop1 || (main.cancel && *main.cancel)) return;
                const long long sent = main.sentAtMs.load(), now = nowMs();
                if (sent > 0 && now - sent >= waitMs) break;
                if (sent == 0 && now - started >= std::max<long long>(4000, 2LL * waitMs)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            const bool ok = shot(spare, 1);
            result1 = ok ? 1 : 2;
            if (ok) stop0 = true;  // the second copy won: stop the first
        });
    const bool ok0 = shot(main, 0);
    done0 = true;
    int winner = ok0 ? 0 : -1;
    if (second.valid()) {
        // The first copy answered well, the person cancelled, or its failure means a second copy won't
        // help: stop the second. Otherwise (too slow, a network error, busy) it may still come through.
        if (ok0 || (main.cancel && *main.cancel) || (!stop0 && !keepWaiting())) stop1 = true;
        second.get();
        if (!ok0 && result1 == 1) winner = 1;
    }
    main.stop = oldStop;
    spare.stop = nullptr;
    return winner;
}
