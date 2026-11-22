#include "voiceid.h"

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
// app always asks its own project for it: one zip with the three files below.
std::string gPackUrl = "https://github.com/Aye7even/CIMA-Box/releases/latest/download/cimabox-voiceid-win-x64.zip";
#ifdef _WIN32
const char* const kLibrary = "sherpa-onnx-c-api.dll";
#else
const char* const kLibrary = "libsherpa-onnx-c-api.so";
#endif
const char* const kModel = "voice.onnx";

std::atomic<VoiceId*> gEngine{nullptr};

// ---- the library's interface: the handful of sherpa-onnx C functions this needs (c-api.h).
struct ExtractorConfig {
    const char* model;
    int32_t num_threads;
    int32_t debug;
    const char* provider;
};
struct Api {
    void* (*create)(const ExtractorConfig*) = nullptr;
    void (*destroy)(void*) = nullptr;
    int32_t (*dim)(void*) = nullptr;
    void* (*createStream)(void*) = nullptr;
    void (*accept)(void* stream, int32_t rate, const float* samples, int32_t n) = nullptr;
    void (*finished)(void* stream) = nullptr;
    int32_t (*ready)(void*, void* stream) = nullptr;
    const float* (*compute)(void*, void* stream) = nullptr;
    void (*destroyEmbedding)(const float*) = nullptr;
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

uint64_t folderBytes(const fs::path& dir) {
    uint64_t total = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) total += uint64_t(it->file_size(ec));
    return total;
}

}  // namespace

float likeness(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) return 0.f;
    double dot = 0;
    for (size_t i = 0; i < a.size(); ++i) dot += double(a[i]) * b[i];
    return float(dot);  // both are of length 1
}

void setVoiceIdEngine(VoiceId* v) { gEngine = v; }
VoiceId* voiceIdEngine() { return gEngine; }
void setVoiceIdPackUrl(const std::string& url) { gPackUrl = url; }

struct VoiceId::Impl {
    fs::path dir;
    std::function<void(const std::string&)> log;

    mutable std::mutex mtx;        // the state below
    int stage = kVoiceIdNone;
    uint64_t got = 0;
    std::string error;
    bool loaded = false;
    bool loadFailed = false;       // tried once and couldn't: not tried again until it's downloaded anew

    std::thread worker;
    std::atomic<bool> stop{false};

    std::mutex useMtx;             // one fingerprint at a time; also guards loading and unloading
    LibHandle lib{};
    Api api;
    void* extractor = nullptr;
    int dim = 0;

    bool filesThere() const {
        std::error_code ec;
        return fs::exists(dir / kLibrary, ec) && fs::exists(dir / kModel, ec);
    }

    void say(const std::string& line) const {
        if (log) log("Voice fingerprints: " + line);
    }

    void unload() {  // useMtx held
        if (extractor && api.destroy) api.destroy(extractor);
        extractor = nullptr;
        closeLib(lib);
        lib = LibHandle{};
        api = Api();
        dim = 0;
        std::lock_guard<std::mutex> lk(mtx);
        loaded = false;
    }

    bool load() {  // useMtx held
        if (extractor) return true;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (loadFailed || stage != kVoiceIdInstalled) return false;
        }
        auto failed = [&](const std::string& why) {
            say("couldn't be loaded (" + why + "). Voices are matched the built-in way.");
            closeLib(lib);
            lib = LibHandle{};
            api = Api();
            std::lock_guard<std::mutex> lk(mtx);
            loadFailed = true;
            error = "Voice fingerprints couldn't be loaded on this PC. Voices are matched the built-in way.";
            return false;
        };
        lib = openLib(dir / kLibrary);
        if (!lib) return failed(libError());
        const bool bound = bind(lib, "SherpaOnnxCreateSpeakerEmbeddingExtractor", api.create) &&
                           bind(lib, "SherpaOnnxDestroySpeakerEmbeddingExtractor", api.destroy) &&
                           bind(lib, "SherpaOnnxSpeakerEmbeddingExtractorDim", api.dim) &&
                           bind(lib, "SherpaOnnxSpeakerEmbeddingExtractorCreateStream", api.createStream) &&
                           bind(lib, "SherpaOnnxOnlineStreamAcceptWaveform", api.accept) &&
                           bind(lib, "SherpaOnnxOnlineStreamInputFinished", api.finished) &&
                           bind(lib, "SherpaOnnxSpeakerEmbeddingExtractorIsReady", api.ready) &&
                           bind(lib, "SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding", api.compute) &&
                           bind(lib, "SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding", api.destroyEmbedding) &&
                           bind(lib, "SherpaOnnxDestroyOnlineStream", api.destroyStream);
        if (!bound) return failed("the library is a different version than expected");
        const std::string model = plainPath(dir / kModel);
        ExtractorConfig cfg{};
        cfg.model = model.c_str();
        cfg.num_threads = 1;  // a caption's worth of speech takes a few hundredths of a second; the game keeps the cores
        cfg.debug = 0;
        cfg.provider = "cpu";
        extractor = api.create(&cfg);
        if (!extractor) return failed("the model didn't open");
        dim = api.dim(extractor);
        if (dim <= 0 || dim > 4096) {
            api.destroy(extractor);
            extractor = nullptr;
            return failed("the model gave no fingerprint size");
        }
        say("loaded (" + std::to_string(dim) + " numbers per voice).");
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
            stage = filesThere() ? kVoiceIdInstalled : kVoiceIdNone;
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
            return fail(status == 404 ? "This version of CIMA Box has no voice fingerprint pack yet." : "The download failed. Check your connection and try again.");
        }
        if (!unzip(part, dir)) return fail("The download couldn't be unpacked.");
        fs::remove(part, ec);
        if (!filesThere()) return fail("The download was incomplete.");
        say("downloaded.");
        std::lock_guard<std::mutex> lk(mtx);
        stage = kVoiceIdInstalled;
        loadFailed = false;
        error.clear();
    }
};

VoiceId::VoiceId(const fs::path& dir, std::function<void(const std::string&)> log) : p_(new Impl) {
    p_->dir = dir;
    p_->log = std::move(log);
    if (p_->filesThere()) p_->stage = kVoiceIdInstalled;
}

VoiceId::~VoiceId() {
    p_->stop = true;
    if (p_->worker.joinable()) p_->worker.join();
    if (gEngine == this) gEngine = nullptr;
    std::lock_guard<std::mutex> lk(p_->useMtx);
    p_->unload();
}

void VoiceId::download() {
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        if (p_->stage == kVoiceIdDownloading) return;
    }
    if (p_->worker.joinable()) p_->worker.join();
    p_->stop = false;
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->stage = kVoiceIdDownloading;
        p_->got = 0;
        p_->error.clear();
    }
    p_->worker = std::thread([this] { p_->fetch(); });
}

void VoiceId::cancel() { p_->stop = true; }  // the download notices within a moment and puts the state back

void VoiceId::remove() {
    p_->stop = true;
    if (p_->worker.joinable()) p_->worker.join();
    std::lock_guard<std::mutex> use(p_->useMtx);
    p_->unload();
    std::error_code ec;
    fs::remove_all(p_->dir, ec);
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->stage = kVoiceIdNone;
    p_->loadFailed = false;
    p_->error.clear();
}

VoiceIdState VoiceId::state() const {
    VoiceIdState s;
    std::lock_guard<std::mutex> lk(p_->mtx);
    s.stage = p_->stage;
    s.gotBytes = p_->got;
    s.error = p_->error;
    s.loaded = p_->loaded;
    s.diskBytes = p_->stage == kVoiceIdInstalled ? folderBytes(p_->dir) : 0;
    return s;
}

bool VoiceId::installed() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->stage == kVoiceIdInstalled && !p_->loadFailed;
}

bool VoiceId::fingerprint(const float* samples, size_t n, std::vector<float>& out) {
    out.clear();
    if (!samples || n < 16000) return false;  // under a second says too little about a voice
    std::lock_guard<std::mutex> use(p_->useMtx);
    if (!p_->load()) return false;
    const Api& a = p_->api;
    void* stream = a.createStream(p_->extractor);
    if (!stream) return false;
    a.accept(stream, 16000, samples, int32_t(std::min<size_t>(n, 16000 * 30)));
    a.finished(stream);
    bool ok = false;
    if (a.ready(p_->extractor, stream)) {
        if (const float* v = a.compute(p_->extractor, stream)) {
            double sum = 0;
            for (int i = 0; i < p_->dim; ++i) sum += double(v[i]) * v[i];
            const double len = std::sqrt(sum);
            if (len > 1e-9 && std::isfinite(len)) {
                out.resize(size_t(p_->dim));
                for (int i = 0; i < p_->dim; ++i) out[size_t(i)] = float(v[i] / len);
                ok = true;
            }
            a.destroyEmbedding(v);
        }
    }
    a.destroyStream(stream);
    return ok;
}
