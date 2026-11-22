// Translation models that run on this PC (no key, no internet once downloaded).
//
// A model is one .gguf file from Hugging Face. It's run by llama.cpp's own server (llama-server),
// downloaded once from llama.cpp's official releases on GitHub and started hidden, listening only on
// this PC (127.0.0.1) behind a key made up fresh each time. translate.cpp talks to it like to any
// other AI service, so every feature that works with Claude, Gemini or OpenAI works with it.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// One model on offer. Sizes are what the download page said when this list was written; the real
// size is looked up before a download starts.
struct LocalModel {
    const char* id;       // also its file name: <id>.gguf
    const char* name;     // "Gemma 4 E4B"
    const char* level;    // "Light", "Balanced", "Best": shown first, translated
    const char* repo;     // Hugging Face repository
    const char* quant;    // which file in it: the name contains this
    double sizeGb;        // download and disk size
    int memoryGb;         // free memory it needs to run (graphics memory, or RAM without a graphics card)
    const char* blurb;    // one line: who it's for (translated)
    bool byChoice;        // offered, never picked for anyone by itself (the big ones: only ever someone's own choice)
    bool retired;         // no longer offered: shown (to keep using, or delete) only where it's already downloaded
};
const std::vector<LocalModel>& localCatalog();
const LocalModel* findLocalModel(const std::string& id);

enum LocalModelStage { kModelNone = 0, kModelQueued, kModelDownloading, kModelChecking, kModelInstalled, kModelFailed };
enum LocalEngineStage { kEngineOff = 0, kEngineGetting, kEngineStarting, kEngineReady, kEngineFailed };

struct LocalModelState {
    std::string id;
    int stage = kModelNone;
    uint64_t done = 0, total = 0;  // bytes, while downloading
    uint64_t bytes = 0;            // on disk, when installed
    std::string message;           // English; why it failed
};

struct LocalState {
    std::vector<LocalModelState> models;  // one per catalog entry, same order
    int engine = kEngineOff;
    std::string engineMessage;     // English; what it's doing, or why it failed
    uint64_t engineDone = 0, engineTotal = 0;  // bytes, while the engine downloads
    std::string running;           // the model that's loaded and answering ("" = none)
    std::string device;            // the graphics card it's on, as the engine named it ("" = unknown, or the processor)
    std::string url, key;          // where it answers, and the key it wants (only while `running`)
    uint64_t diskBytes = 0;        // everything this feature keeps on disk
};

class LocalAi {
public:
    // `dir`: where models and the engine are kept. `log`: one line at a time, for the app log.
    LocalAi(const std::filesystem::path& dir, std::function<void(const std::string&)> log);
    ~LocalAi();

    void download(const std::string& id);  // in the background; progress in state()
    void cancel(const std::string& id);    // stops a download (what's there is kept, to go on later)
    void remove(const std::string& id);    // deletes it (stops it first if it's running)

    // The model that should be loaded and answering ("" = none: the engine is stopped and its
    // memory freed). `gpu`: use the graphics card. Call as often as you like.
    void want(const std::string& id, bool gpu);

    LocalState state() const;
    bool installed(const std::string& id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// ---- for tests
void setLocalAiHosts(const std::string& huggingFace, const std::string& gitHubApi);
std::string sha256Hex(const std::filesystem::path& file, const std::function<bool()>& keepGoing);
// From Hugging Face's file list (JSON): the file to download for `quant`. False when there's none.
bool pickModelFile(const std::string& treeJson, const std::string& quant, std::string& path, uint64_t& size, std::string& sha256);
// From a GitHub release (JSON): the Windows engine to download. False when it has none; then
// `nightlyUrl` is its "nightly-tag.txt", if it has one (that file names the release that does).
bool pickEngineAsset(const std::string& releaseJson, std::string& url, uint64_t& size, std::string& sha256, std::string& nightlyUrl);
