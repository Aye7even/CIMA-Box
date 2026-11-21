#include "early.h"

#include <cctype>
#include <cstring>

std::string wordsOnly(const std::string& s) {
    // 。 、 ？ ！ … “ ” ‘ ’ « » ¿ ¡ ， ． ؟ ،
    static const char* const kMarks[] = {"\xE3\x80\x82", "\xE3\x80\x81", "\xEF\xBC\x9F", "\xEF\xBC\x81", "\xE2\x80\xA6", "\xE2\x80\x9C",
                                         "\xE2\x80\x9D", "\xE2\x80\x98", "\xE2\x80\x99", "\xC2\xAB",     "\xC2\xBB",     "\xC2\xBF",
                                         "\xC2\xA1",     "\xEF\xBC\x8C", "\xEF\xBC\x8E", "\xD8\x9F",     "\xD8\x8C"};
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool mark = false;
        for (const char* m : kMarks) {
            const size_t n = std::strlen(m);
            if (s.compare(i, n, m) == 0) {
                i += n;
                mark = true;
                break;
            }
        }
        if (mark) continue;
        const unsigned char c = (unsigned char)s[i++];
        if (c < 0x80 && std::ispunct(c)) continue;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        out += c < 0x80 ? char(std::tolower(c)) : char(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

namespace {
bool asksSomething(const std::string& s) {
    return s.find('?') != std::string::npos || s.find("\xEF\xBC\x9F") != std::string::npos || s.find("\xD8\x9F") != std::string::npos;
}
// Everything that changes how a line is worded. The recent conversation is left out on purpose: it
// may have moved on by a line, and either way it is only context.
std::string keyOf(const TranslateSetup& t, const Language* from, const Language& to) {
    return std::to_string(t.service) + "|" + t.claudeModel + "|" + t.geminiModel + "|" + t.openaiModel + "|" + t.localModel + "|" +
           to.code + "|" + (from ? from->code : "?") + "|" + std::to_string(t.level) + "|" + t.customStyle + "|" + t.persona + "|" +
           t.variety + "|" + std::to_string(t.speaker) + "|" + std::to_string(t.listener) + "|" + std::to_string(t.swear) + "|" +
           std::to_string(t.wordNote.size());
}
}  // namespace

bool sameWords(const std::string& sent, const std::string& finished) {
    const std::string a = wordsOnly(sent);
    return !a.empty() && a == wordsOnly(finished) && asksSomething(sent) == asksSomething(finished);
}

void EarlyTranslation::stop() {
    if (!job_.valid()) return;
    *stop_ = true;
    job_.wait();
    job_ = std::shared_future<Result>();
    text_.clear();
}

void EarlyTranslation::start(const TranslateSetup& setup, const std::string& text, const Language* from, const Language& to) {
    const std::string key = keyOf(setup, from, to);
    if (job_.valid() && key_ == key && text_ == text) return;  // already on it
    stop();
    text_ = text;
    key_ = key;
    at_ = std::chrono::steady_clock::now();
    stop_ = std::make_shared<std::atomic<bool>>(false);
    if (!translator_) translator_ = std::make_shared<Translator>();
    ++started_;
    const std::shared_ptr<std::atomic<bool>> stop = stop_;
    const std::shared_ptr<Translator> tr = translator_;
    const Language target = to;
    job_ = std::async(std::launch::async, [setup, text, from, target, stop, tr] {
              tr->http.cancel = stop.get();
              Result r;
              r.ok = tr->translate(setup, text, from, target, r.out, r.err);
              r.via = tr->via;
              tr->http.cancel = nullptr;
              return r;
          }).share();
}

bool EarlyTranslation::take(const TranslateSetup& setup, const std::string& heard, const Language* from, const Language& to,
                            Result& r) {
    if (!job_.valid()) return false;
    if (key_ != keyOf(setup, from, to) || !sameWords(text_, heard) ||
        std::chrono::steady_clock::now() - at_ > std::chrono::seconds(8)) {
        stop();
        return false;
    }
    r = job_.get();
    job_ = std::shared_future<Result>();
    text_.clear();
    // Whatever it came to (the AI's line, or a backup's when the AI was slow or said no: asking again
    // now would only end the same way, later). A failure is done again the regular way.
    if (!r.ok) return false;
    ++used_;
    return true;
}
