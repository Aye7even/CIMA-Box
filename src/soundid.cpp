#include "soundid.h"

#include "http.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

// The pack is put together by CIMA Box's own release build and attached to every release, so the
// app always asks its own project for it: one zip with the files below.
std::string gPackUrl = "https://github.com/Aye7even/CIMA-Box/releases/latest/download/cimabox-sounds-win-x64.zip";
#ifdef _WIN32
const char* const kLibrary = "sherpa-onnx-c-api.dll";
#else
const char* const kLibrary = "libsherpa-onnx-c-api.so";
#endif
const char* const kModel = "sounds.onnx";
const char* const kLabels = "sounds.csv";

std::atomic<SoundId*> gEngine{nullptr};

// ---- the library's interface: the handful of sherpa-onnx C functions this needs (c-api.h).
struct ZipformerTaggingConfig { const char* model; };
struct TaggingModelConfig {
    ZipformerTaggingConfig zipformer;
    const char* ced;
    int32_t num_threads;
    int32_t debug;
    const char* provider;
};
struct TaggingConfig {
    TaggingModelConfig model;
    const char* labels;
    int32_t top_k;
};
struct AudioEvent {
    const char* name;
    int32_t index;
    float prob;
};
struct Api {
    void* (*create)(const TaggingConfig*) = nullptr;
    void (*destroy)(void*) = nullptr;
    void* (*createStream)(void*) = nullptr;
    void (*accept)(void* stream, int32_t rate, const float* samples, int32_t n) = nullptr;
    const AudioEvent* const* (*compute)(void*, void* stream, int32_t topK) = nullptr;
    void (*freeResults)(const AudioEvent* const*) = nullptr;
    void (*destroyStream)(void* stream) = nullptr;
};

#ifdef _WIN32
using LibHandle = HMODULE;
LibHandle openLib(const fs::path& file) {
    // With the full path and this flag, the libraries it needs itself (ONNX Runtime) are looked for
    // next to it, in the pack's folder, not next to the app.
    return LoadLibraryExW(file.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}
void closeLib(LibHandle h) { if (h) FreeLibrary(h); }
void* symbol(LibHandle h, const char* name) { return reinterpret_cast<void*>(GetProcAddress(h, name)); }
std::string libError() { return "Windows error " + std::to_string(GetLastError()); }
// The library opens the model by a plain (non-Unicode) name. A Windows user folder can have letters
// outside the system's own character set, so it gets the file's short name (plain letters) where
// Windows has one.
std::string plainPath(const fs::path& file) {
    std::wstring w = file.wstring();
    wchar_t shortName[MAX_PATH];
    const DWORD n = GetShortPathNameW(w.c_str(), shortName, MAX_PATH);
    if (n > 0 && n < MAX_PATH) w.assign(shortName, n);
    const int len = WideCharToMultiByte(CP_ACP, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(len > 0 ? len : 0), '\0');
    if (len > 0) WideCharToMultiByte(CP_ACP, 0, w.c_str(), int(w.size()), &out[0], len, nullptr, nullptr);
    return out;
}
#else
std::string plainPath(const fs::path& file) { return file.string(); }
using LibHandle = void*;
LibHandle openLib(const fs::path& file) { return dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL); }
void closeLib(LibHandle h) { if (h) dlclose(h); }
void* symbol(LibHandle h, const char* name) { return dlsym(h, name); }
std::string libError() { const char* e = dlerror(); return e ? e : "unknown"; }
#endif

template <class F>
bool bind(LibHandle h, const char* name, F& fn) {
    fn = reinterpret_cast<F>(symbol(h, name));
    return fn != nullptr;
}

// Unpacks a zip into a folder with the tool the system has for it. True when it finished well.
bool unzip(const fs::path& zip, const fs::path& into) {
#ifdef _WIN32
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    const fs::path tar = fs::path(std::wstring(sys, n)) / L"tar.exe";  // part of Windows since Windows 10 (1803); it reads .zip
    std::wstring line = L"\"" + tar.wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + into.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(tar.wstring().c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
#else
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execlp("unzip", "unzip", "-o", "-q", zip.c_str(), "-d", into.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
#endif
}

}  // namespace

void setSoundIdEngine(SoundId* s) { gEngine = s; }
SoundId* soundIdEngine() { return gEngine; }
void setSoundIdPackUrl(const std::string& url) { gPackUrl = url; }

struct SoundId::Impl {
    fs::path dir;
    std::function<void(const std::string&)> log;

    mutable std::mutex mtx;        // the state below
    int stage = kSoundIdNone;
    uint64_t got = 0;
    std::string error;
    bool loaded = false;
    bool loadFailed = false;       // tried once and couldn't: not tried again until it's downloaded anew

    std::thread worker, warmer;
    std::atomic<bool> stop{false};

    std::mutex useMtx;             // one clip at a time; also guards loading and unloading
    LibHandle lib{};
    Api api;
    void* tagger = nullptr;

    bool filesThere() const {
        std::error_code ec;
        return fs::exists(dir / kLibrary, ec) && fs::exists(dir / kModel, ec) && fs::exists(dir / kLabels, ec);
    }

    void say(const std::string& line) const {
        if (log) log("Sound recognition: " + line);
    }

    void unload() {  // useMtx held
        if (tagger && api.destroy) api.destroy(tagger);
        tagger = nullptr;
        closeLib(lib);
        lib = LibHandle{};
        api = Api();
        std::lock_guard<std::mutex> lk(mtx);
        loaded = false;
    }

    bool load() {  // useMtx held
        if (tagger) return true;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (loadFailed || stage != kSoundIdInstalled) return false;
        }
        auto failed = [&](const std::string& why) {
            say("couldn't be loaded (" + why + "). Laughs and coughs aren't recognised on this PC.");
            closeLib(lib);
            lib = LibHandle{};
            api = Api();
            std::lock_guard<std::mutex> lk(mtx);
            loadFailed = true;
            error = "The laugh and cough recogniser couldn't be loaded on this PC.";
            return false;
        };
        lib = openLib(dir / kLibrary);
        if (!lib) return failed(libError());
        const bool bound = bind(lib, "SherpaOnnxCreateAudioTagging", api.create) &&
                           bind(lib, "SherpaOnnxDestroyAudioTagging", api.destroy) &&
                           bind(lib, "SherpaOnnxAudioTaggingCreateOfflineStream", api.createStream) &&
                           bind(lib, "SherpaOnnxAcceptWaveformOffline", api.accept) &&
                           bind(lib, "SherpaOnnxAudioTaggingCompute", api.compute) &&
                           bind(lib, "SherpaOnnxAudioTaggingFreeResults", api.freeResults) &&
                           bind(lib, "SherpaOnnxDestroyOfflineStream", api.destroyStream);
        if (!bound) return failed("the library is a different version than expected");
        const std::string model = plainPath(dir / kModel), labels = plainPath(dir / kLabels);
        TaggingConfig cfg{};
        cfg.model.zipformer.model = "";
        cfg.model.ced = model.c_str();
        cfg.model.num_threads = 2;  // a second or two of sound takes a few hundredths of a second; the game keeps the cores
        cfg.model.debug = 0;
        cfg.model.provider = "cpu";
        cfg.labels = labels.c_str();
        cfg.top_k = 8;
        tagger = api.create(&cfg);
        if (!tagger) return failed("the model didn't open");
        say("loaded.");
        std::lock_guard<std::mutex> lk(mtx);
        loaded = true;
        error.clear();
        return true;
    }

    void fetch() {
        auto fail = [&](const std::string& why) {
            std::error_code ec;
            fs::remove(dir / "pack.zip.part", ec);
            std::lock_guard<std::mutex> lk(mtx);
            stage = filesThere() ? kSoundIdInstalled : kSoundIdNone;
            error = why;
        };
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path part = dir / "pack.zip.part";
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) return fail("Couldn't save the download.");
        Http http;
        http.cancel = &stop;
        HttpRequest r;
        r.url = gPackUrl;
        r.followRedirects = true;
        r.timeoutSec = 1800;
        r.stallSec = 30;
        const ByteSink sink = [&](const char* data, size_t n) {
            out.write(data, std::streamsize(n));
            std::lock_guard<std::mutex> lk(mtx);
            got += n;
        };
        r.sink = &sink;
        long status = 0;
        std::string body, err;
        const bool ok = http.perform(r, status, body, err);
        out.close();
        if (stop) return fail("");
        if (!ok || status != 200) {
            say("the download failed (" + (err.empty() ? "HTTP " + std::to_string(status) : err) + ").");
            return fail(status == 404 ? "This version of CIMA Box has no pack for it yet." : "The download failed.");
        }
        if (!unzip(part, dir)) return fail("The download couldn't be unpacked.");
        fs::remove(part, ec);
        if (!filesThere()) return fail("The download was incomplete.");
        say("downloaded.");
        std::lock_guard<std::mutex> lk(mtx);
        stage = kSoundIdInstalled;
        loadFailed = false;
        error.clear();
    }
};

SoundId::SoundId(const fs::path& dir, std::function<void(const std::string&)> log) : p_(new Impl) {
    p_->dir = dir;
    p_->log = std::move(log);
    if (p_->filesThere()) p_->stage = kSoundIdInstalled;
}

SoundId::~SoundId() {
    p_->stop = true;
    if (p_->worker.joinable()) p_->worker.join();
    if (p_->warmer.joinable()) p_->warmer.join();
    if (gEngine == this) gEngine = nullptr;
    std::lock_guard<std::mutex> lk(p_->useMtx);
    p_->unload();
}

void SoundId::download() {
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        if (p_->stage != kSoundIdNone) return;
    }
    if (p_->worker.joinable()) p_->worker.join();
    p_->stop = false;
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->stage = kSoundIdDownloading;
        p_->got = 0;
        p_->error.clear();
    }
    p_->worker = std::thread([this] { p_->fetch(); });
}

void SoundId::warm() {
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        if (p_->loaded || p_->loadFailed || p_->stage != kSoundIdInstalled) return;
    }
    if (p_->warmer.joinable()) p_->warmer.join();
    p_->warmer = std::thread([this] {
        std::lock_guard<std::mutex> use(p_->useMtx);
        p_->load();
    });
}

SoundIdState SoundId::state() const {
    SoundIdState s;
    std::lock_guard<std::mutex> lk(p_->mtx);
    s.stage = p_->stage;
    s.gotBytes = p_->got;
    s.error = p_->error;
    s.loaded = p_->loaded;
    return s;
}

bool SoundId::installed() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->stage == kSoundIdInstalled && !p_->loadFailed;
}

bool SoundId::listen(const float* samples, size_t n, std::vector<HeardSound>& out) {
    out.clear();
    if (!samples || n < 16000 / 5) return false;
    std::lock_guard<std::mutex> use(p_->useMtx);
    if (!p_->load()) return false;
    const Api& a = p_->api;
    void* stream = a.createStream(p_->tagger);
    if (!stream) return false;
    a.accept(stream, 16000, samples, int32_t(std::min<size_t>(n, 16000 * 10)));
    if (const AudioEvent* const* events = a.compute(p_->tagger, stream, 8)) {
        for (const AudioEvent* const* e = events; *e; ++e)
            if ((*e)->name) out.push_back({(*e)->name, (*e)->prob});
        a.freeResults(events);
    }
    a.destroyStream(stream);
    return !out.empty();
}
