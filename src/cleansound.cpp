#include "cleansound.h"

#include "http.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
std::string gPackUrl = "https://github.com/Aye7even/CIMA-Box/releases/latest/download/cimabox-clean-win-x64.zip";
#ifdef _WIN32
const char* const kLibrary = "sherpa-onnx-c-api.dll";
#else
const char* const kLibrary = "libsherpa-onnx-c-api.so";
#endif
const char* const kVadModel = "vad.onnx";
const char* const kCleanModel = "denoise.onnx";
std::atomic<CleanSound*> gEngine{nullptr};

// ---- the library's interface: the handful of sherpa-onnx C functions this needs (c-api.h, 2.0.3).
struct OneVadConfig {  // the same layout for both kinds of detector the library knows
    const char* model;
    float threshold;
    float min_silence_duration;
    float min_speech_duration;
    int32_t window_size;
    float max_speech_duration;
};
struct VadConfig {
    OneVadConfig silero;
    int32_t sample_rate;
    int32_t num_threads;
    const char* provider;
    int32_t debug;
    OneVadConfig ten;  // not used: left empty
};
struct SpeechSegment {
    int64_t start;
    float* samples;
    int32_t n;
};
struct GtcrnConfig { const char* model; };
struct DpdfNetConfig { const char* model; float attenuation_limit_db; };                                 // not used
struct VoxLockConfig { const char* enroll_model; const char* chunk_model; const char* voice_sample; };  // not used
struct DenoiserModelConfig {
    GtcrnConfig gtcrn;
    int32_t num_threads;
    int32_t debug;
    const char* provider;
    DpdfNetConfig dpdfnet;
    VoxLockConfig voxlock;
};
struct DenoiserConfig { DenoiserModelConfig model; };
struct DenoisedAudio {
    const float* samples;
    int32_t n;
    int32_t sample_rate;
};
struct Api {
    void* (*vadCreate)(const VadConfig*, float bufferSeconds) = nullptr;
    void (*vadDestroy)(void*) = nullptr;
    void (*vadAccept)(void*, const float* samples, int32_t n) = nullptr;
    int32_t (*vadEmpty)(void*) = nullptr;
    const SpeechSegment* (*vadFront)(void*) = nullptr;
    void (*vadFreeSegment)(const SpeechSegment*) = nullptr;
    void (*vadPop)(void*) = nullptr;
    void (*vadReset)(void*) = nullptr;
    void (*vadFlush)(void*) = nullptr;
    void* (*cleanCreate)(const DenoiserConfig*) = nullptr;
    void (*cleanDestroy)(void*) = nullptr;
    const DenoisedAudio* (*cleanRun)(void*, const float* samples, int32_t n, int32_t rate) = nullptr;
    void (*cleanFree)(const DenoisedAudio*) = nullptr;
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

void setCleanSoundEngine(CleanSound* c) { gEngine = c; }
CleanSound* cleanSoundEngine() { return gEngine; }
void setCleanSoundPackUrl(const std::string& url) { gPackUrl = url; }

float standsOutDb(const std::vector<int16_t>& pcm) {
    std::vector<double> energy;
    energy.reserve(pcm.size() / 320 + 1);
    for (size_t i = 0; i + 320 <= pcm.size(); i += 320) {
        double sum = 0;
        for (size_t k = 0; k < 320; ++k) sum += double(pcm[i + k]) * pcm[i + k];
        // Dead silence doesn't count (a program that stopped playing, the end of a phrase, a mic
        // with a gate on it): it says nothing about what is under the voice while it talks.
        if (sum / 320.0 >= 4.0) energy.push_back(sum / 320.0);
    }
    if (energy.size() < 10) return 99.f;  // under a fifth of a second: nothing to go by
    std::sort(energy.begin(), energy.end());
    return float(10.0 * std::log10((energy[energy.size() * 9 / 10] + 1.0) / (energy[energy.size() / 10] + 1.0)));
}

struct CleanSound::Impl {
    fs::path dir;
    std::function<void(const std::string&)> log;

    mutable std::mutex mtx;        // the state below
    int stage = kCleanSoundNone;
    uint64_t got = 0;
    std::string error;
    bool loaded = false;
    bool loadFailed = false;       // tried once and couldn't: not tried again until it's downloaded anew

    std::thread worker, warmer;
    std::atomic<bool> stop{false};

    std::mutex useMtx;             // one phrase at a time; also guards loading and unloading
    LibHandle lib{};
    Api api;
    void* vad = nullptr;
    void* cleaner = nullptr;

    bool filesThere() const {
        std::error_code ec;
        return fs::exists(dir / kLibrary, ec) && fs::exists(dir / kVadModel, ec) && fs::exists(dir / kCleanModel, ec);
    }

    void say(const std::string& line) const {
        if (log) log("Voice detection: " + line);
    }

    void drop() {  // useMtx held: the models and the library, out of memory
        if (vad && api.vadDestroy) api.vadDestroy(vad);
        if (cleaner && api.cleanDestroy) api.cleanDestroy(cleaner);
        vad = cleaner = nullptr;
        closeLib(lib);
        lib = LibHandle{};
        api = Api();
    }

    void unload() {  // useMtx held
        drop();
        std::lock_guard<std::mutex> lk(mtx);
        loaded = false;
    }

    bool load() {  // useMtx held
        if (vad && cleaner) return true;
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (loadFailed || stage != kCleanSoundInstalled) return false;
        }
        auto failed = [&](const std::string& why) {
            say("couldn't be loaded (" + why + "). Every phrase is sent to the speech service, as before.");
            drop();
            std::lock_guard<std::mutex> lk(mtx);
            loadFailed = true;
            error = "Voice detection couldn't be loaded on this PC.";
            return false;
        };
        lib = openLib(dir / kLibrary);
        if (!lib) return failed(libError());
        const bool bound = bind(lib, "SherpaOnnxCreateVoiceActivityDetector", api.vadCreate) &&
                           bind(lib, "SherpaOnnxDestroyVoiceActivityDetector", api.vadDestroy) &&
                           bind(lib, "SherpaOnnxVoiceActivityDetectorAcceptWaveform", api.vadAccept) &&
                           bind(lib, "SherpaOnnxVoiceActivityDetectorEmpty", api.vadEmpty) &&
                           bind(lib, "SherpaOnnxVoiceActivityDetectorFront", api.vadFront) &&
                           bind(lib, "SherpaOnnxDestroySpeechSegment", api.vadFreeSegment) &&
                           bind(lib, "SherpaOnnxVoiceActivityDetectorPop", api.vadPop) &&
                           bind(lib, "SherpaOnnxVoiceActivityDetectorReset", api.vadReset) &&
                           bind(lib, "SherpaOnnxVoiceActivityDetectorFlush", api.vadFlush) &&
                           bind(lib, "SherpaOnnxCreateOfflineSpeechDenoiser", api.cleanCreate) &&
                           bind(lib, "SherpaOnnxDestroyOfflineSpeechDenoiser", api.cleanDestroy) &&
                           bind(lib, "SherpaOnnxOfflineSpeechDenoiserRun", api.cleanRun) &&
                           bind(lib, "SherpaOnnxDestroyDenoisedAudio", api.cleanFree);
        if (!bound) return failed("the library is a different version than expected");
        const std::string vadFile = plainPath(dir / kVadModel), cleanFile = plainPath(dir / kCleanModel);
        VadConfig vc{};
        vc.silero.model = vadFile.c_str();
        // Lenient on purpose: a real voice turned away costs more than a stray noise let through
        // (the speech service still says "no words" for that, as it always did).
        vc.silero.threshold = 0.35f;
        vc.silero.min_silence_duration = 0.2f;
        vc.silero.min_speech_duration = 0.1f;
        vc.silero.window_size = 512;
        vc.silero.max_speech_duration = 30.f;
        vc.sample_rate = 16000;
        vc.num_threads = 1;  // a phrase takes a few hundredths of a second; the game keeps the cores
        vc.provider = "cpu";
        vad = api.vadCreate(&vc, 40.f);
        if (!vad) return failed("the voice detection model didn't open");
        DenoiserConfig dc{};
        dc.model.gtcrn.model = cleanFile.c_str();
        dc.model.num_threads = 1;
        dc.model.provider = "cpu";
        cleaner = api.cleanCreate(&dc);
        if (!cleaner) return failed("the noise removal model didn't open");
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
            stage = filesThere() ? kCleanSoundInstalled : kCleanSoundNone;
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
        stage = kCleanSoundInstalled;
        loadFailed = false;
        error.clear();
    }
};

CleanSound::CleanSound(const fs::path& dir, std::function<void(const std::string&)> log) : p_(new Impl) {
    p_->dir = dir;
    p_->log = std::move(log);
    if (p_->filesThere()) p_->stage = kCleanSoundInstalled;
}

CleanSound::~CleanSound() {
    p_->stop = true;
    if (p_->worker.joinable()) p_->worker.join();
    if (p_->warmer.joinable()) p_->warmer.join();
    if (gEngine == this) gEngine = nullptr;
    std::lock_guard<std::mutex> lk(p_->useMtx);
    p_->unload();
}

void CleanSound::download() {
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        if (p_->stage != kCleanSoundNone) return;
    }
    if (p_->worker.joinable()) p_->worker.join();
    p_->stop = false;
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->stage = kCleanSoundDownloading;
        p_->got = 0;
        p_->error.clear();
    }
    p_->worker = std::thread([this] { p_->fetch(); });
}

void CleanSound::warm() {
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        if (p_->loaded || p_->loadFailed || p_->stage != kCleanSoundInstalled) return;
    }
    if (p_->warmer.joinable()) p_->warmer.join();
    p_->warmer = std::thread([this] {
        std::lock_guard<std::mutex> use(p_->useMtx);
        p_->load();
    });
}

CleanSoundState CleanSound::state() const {
    CleanSoundState s;
    std::lock_guard<std::mutex> lk(p_->mtx);
    s.stage = p_->stage;
    s.gotBytes = p_->got;
    s.error = p_->error;
    s.loaded = p_->loaded;
    return s;
}

bool CleanSound::installed() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->stage == kCleanSoundInstalled && !p_->loadFailed;
}

bool CleanSound::speechSeconds(const std::vector<int16_t>& pcm, float& seconds) {
    seconds = 0;
    if (pcm.size() < 512) return false;
    // Brought to an ordinary level first: the detector was trained on voices at a normal volume,
    // and hears nothing in a faint one (someone far away, a low mic).
    int top = 0;
    for (int16_t v : pcm) top = std::max(top, std::abs(int(v)));
    if (top < 40) return true;  // silence: no voice, and nothing to ask the model
    const float scale = 0.5f / float(top);
    std::lock_guard<std::mutex> use(p_->useMtx);
    if (!p_->load()) return false;
    const Api& a = p_->api;
    a.vadReset(p_->vad);
    size_t samples = 0;
    auto take = [&] {
        while (!a.vadEmpty(p_->vad)) {
            if (const SpeechSegment* s = a.vadFront(p_->vad)) {
                samples += size_t(std::max(0, s->n));
                a.vadFreeSegment(s);
            }
            a.vadPop(p_->vad);
        }
    };
    float window[512];
    const size_t n = std::min(pcm.size(), size_t(16000) * 30);
    for (size_t i = 0; i + 512 <= n; i += 512) {
        for (size_t k = 0; k < 512; ++k) window[k] = float(pcm[i + k]) * scale;
        a.vadAccept(p_->vad, window, 512);
        take();
    }
    a.vadFlush(p_->vad);  // a phrase that ends while someone is still talking counts too
    take();
    seconds = float(samples) / 16000.f;
    return true;
}

bool CleanSound::clean(std::vector<int16_t>& pcm) {
    if (pcm.size() < 1600) return false;
    std::vector<float> in(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) in[i] = float(pcm[i]) / 32768.f;
    std::lock_guard<std::mutex> use(p_->useMtx);
    if (!p_->load()) return false;
    const Api& a = p_->api;
    const DenoisedAudio* out = a.cleanRun(p_->cleaner, in.data(), int32_t(in.size()), 16000);
    if (!out) return false;
    // Same rate and (to within a few samples) the same length: anything else would move the words'
    // times against the sound, so it is not used.
    const bool fits = out->samples && out->sample_rate == 16000 && std::abs(long(out->n) - long(pcm.size())) <= 512;
    if (fits) {
        const size_t n = std::min(pcm.size(), size_t(out->n));
        for (size_t i = 0; i < n; ++i) pcm[i] = int16_t(std::lrint(std::max(-1.f, std::min(1.f, out->samples[i])) * 32767.f));
        for (size_t i = n; i < pcm.size(); ++i) pcm[i] = 0;
    }
    a.cleanFree(out);
    return fits;
}
