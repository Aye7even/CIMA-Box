// Early translation (an option, for the AI services).
//
// A line is normally translated once its phrase has ended: after the pause that ends it, or, for a
// sentence that sounds unfinished, after waiting a moment for the rest. With this on, the words so
// far are translated during that wait, on a connection of their own. The result is used only if the
// phrase then ends with the same words and the same settings, so what is said never changes, only
// how soon. If you go on talking, it is dropped and the whole sentence is translated as usual: that
// dropped request is what this costs.
//
// Used from one thread (the voice worker).
#pragma once
#include "translate.h"

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>

// The words alone: lower case, no punctuation, single spaces. The words heard while you talk and the
// finished phrase often differ only in a capital or a full stop.
std::string wordsOnly(const std::string& s);
// The same line: the same words, and a question on both sides or on neither (a question mark can
// change the whole sentence in the other language).
bool sameWords(const std::string& sent, const std::string& finished);

class EarlyTranslation {
public:
    struct Result {
        bool ok = false;
        std::string out, err, via;
    };

    ~EarlyTranslation() { stop(); }

    // Starts translating `text` now (stopping one that was for other words). Nothing to wait for.
    void start(const TranslateSetup& setup, const std::string& text, const Language* from, const Language& to);
    bool has(const std::string& text) const { return job_.valid() && text_ == text; }
    // The finished phrase: true when the early translation is of these very words with these settings
    // and it worked (it is waited for if still on its way: it had a head start). Anything else is
    // dropped, and the line is translated the regular way.
    bool take(const TranslateSetup& setup, const std::string& heard, const Language* from, const Language& to, Result& r);
    void stop();

    int started() const { return started_; }
    int used() const { return used_; }

private:
    std::string text_, key_;  // what was sent, and the settings it was sent with
    std::shared_future<Result> job_;
    std::shared_ptr<std::atomic<bool>> stop_;
    std::shared_ptr<Translator> translator_;  // its own connections, kept open between lines
    std::chrono::steady_clock::time_point at_{};
    int started_ = 0, used_ = 0;
};
