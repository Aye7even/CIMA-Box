// Live transcription: your voice goes to ElevenLabs while you talk (realtime speech-to-text over a
// WebSocket), so the words are ready the moment you stop instead of being uploaded only then.
//
// The phrase itself is still cut by CIMA Box (voice activation or push to talk, and the sentence
// check): each phrase is streamed while you say it and "committed" when it ends, and the committed
// text comes back for that phrase. If anything goes wrong, the phrase gets no live text and the
// regular upload is used instead, so a live problem never loses what you said.
//
// One background thread owns the connection. The other calls only queue work, so the microphone
// loop is never kept waiting by the network.
#pragma once
#include "emotion.h"  // HeardToken

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct LiveText {
    bool ok = false;
    std::string text;
    std::string language;           // the detected language's code, when the service said
    std::vector<HeardToken> words;  // with times from the start of this phrase's audio (when asked for)
    std::string err;
};

class LiveStt {
public:
    struct Options {
        std::string apiKey;
        std::string language;      // ISO code you speak, or "" to have it detected
        bool timestamps = false;   // word times (for following your tone)
        std::string previousText;  // a little of what was said before: helps it hear names and the topic
    };

    // `host` is for tests (a stand-in server, "ws://127.0.0.1:port").
    explicit LiveStt(std::string host = "wss://api.elevenlabs.io");
    ~LiveStt();
    LiveStt(const LiveStt&) = delete;
    LiveStt& operator=(const LiveStt&) = delete;

    // A phrase starts: connects now if it isn't connected (while you're still talking).
    void begin(const Options& o);
    // More of the phrase (16 kHz mono).
    void audio(const int16_t* pcm, size_t n);
    // The phrase ended: its text arrives through the future (ok = false: use the regular way).
    std::shared_future<LiveText> commit();
    // The phrase was dropped (a click or a cough): its audio is committed and the text ignored.
    void discard();

    // False for a minute after it failed in a way that will keep failing (bad key, no credits, the
    // service refused it): the regular way is used meanwhile. problem() says why.
    bool available() const;
    std::string problem() const;
    std::string model() const;

    // The words so far, while you talk (called on the live thread: keep it quick).
    std::function<void(const std::string&)> onPartial;
    // Something worth a line in the log (called on the live thread).
    std::function<void(const std::string&)> onLog;

    // How long the server keeps a quiet connection (it closes after 15 s without a message from us);
    // CIMA Box closes it itself a little before. Changeable for tests.
    int idleCloseMs = 13000;

    // The UseFeature (usagecount.h) what's streamed counts under on the Usage page (0 = My voice).
    std::atomic<int> useFeature{0};

private:
    struct Item {
        enum Kind { kAudio, kCommit, kDiscard } kind = kAudio;
        std::vector<int16_t> pcm;
        bool first = false;  // the first audio of a phrase
        std::shared_ptr<std::promise<LiveText>> result;
    };
    struct Waiting {
        std::shared_ptr<std::promise<LiveText>> result;  // null for a discarded phrase
        bool wantExtra = false;                          // word times or the language come in a second message
        bool gotText = false;
        LiveText text;
        long long offset = 0;  // where this phrase starts in the connection's audio (samples)
        long long length = 0;  // how much audio the phrase is (samples)
        std::chrono::steady_clock::time_point sent, textAt;
    };

    void run();
    bool connect(const Options& o);
    void disconnect(const std::string& why);
    bool sendText(const std::string& s);
    bool sendChunk(const std::vector<int16_t>& pcm, size_t from, size_t count, bool commit);
    void receive();
    void handle(const std::string& message);
    void finish(Waiting& w, bool ok, const std::string& err);
    void failAll(const std::string& err);
    void giveUpFor(int seconds, const std::string& why);

    const std::string host_;
    mutable std::mutex mx_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    Options wanted_;            // from the last begin()
    bool connectWanted_ = false;
    std::atomic<bool> quit_{false};
    std::thread thread_;

    // live thread only
    void* curl_ = nullptr;
    bool connected_ = false, started_ = false;
    Options session_;              // what the open connection was made with
    long long sessionSamples_ = 0;  // audio sent on this connection
    long long phraseOffset_ = 0;
    bool phraseOpen_ = false, phraseBroken_ = false, firstChunk_ = true;
    std::vector<int16_t> pending_;  // audio waiting to make up a chunk
    std::deque<Waiting> waiting_;
    std::string inbox_;
    std::chrono::steady_clock::time_point lastSent_;
    int failures_ = 0;

    // shared
    std::string model_ = "scribe_v3_live";
    std::string problem_;
    std::chrono::steady_clock::time_point pausedUntil_{};
};
