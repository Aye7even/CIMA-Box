#include "http.h"

#include <cstdio>
#include <curl/curl.h>

namespace {

struct Transfer {
    CURL* h = nullptr;
    const ByteSink* sink = nullptr;
    std::string body;
    std::atomic<bool>* cancel = nullptr;
};

size_t onWrite(char* p, size_t sz, size_t n, void* ud) {
    auto* t = static_cast<Transfer*>(ud);
    long code = 0;
    curl_easy_getinfo(t->h, CURLINFO_RESPONSE_CODE, &code);
    if (t->sink && code == 200) (*t->sink)(p, sz * n);
    else if (t->body.size() < (8u << 20)) t->body.append(p, sz * n);
    return sz * n;
}

int onProgress(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* t = static_cast<Transfer*>(ud);
    return (t->cancel && t->cancel->load()) ? 1 : 0;
}

}  // namespace

Http::Http() {
    static const bool inited = (curl_global_init(CURL_GLOBAL_DEFAULT), true);
    (void)inited;
    h_ = curl_easy_init();
}

Http::~Http() {
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
    t.h = h; t.sink = r.sink; t.cancel = cancel;

    curl_slist* hdrs = nullptr;
    for (const std::string& line : r.headers) hdrs = curl_slist_append(hdrs, line.c_str());
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
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);

    const CURLcode rc = curl_easy_perform(h);
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
