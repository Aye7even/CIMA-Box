#include "appaudio.h"

#ifdef _WIN32
// ---------------------------------------------------------------- Windows

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <thread>

const bool kAppCaptureSupported = true;

namespace {

// Same layout as AUDIOCLIENT_ACTIVATION_PARAMS in audioclientactivationparams.h. Declared here so
// the file also builds with Windows SDKs older than 10.0.20348, which don't ship that header.
struct LoopbackTarget {
    DWORD processId;
    DWORD mode;  // 0 = this process and its children, 1 = everything except them
};
struct ActivationParams {
    DWORD type;  // 1 = process loopback
    LoopbackTarget target;
};
const wchar_t kProcessLoopbackDevice[] = L"VAD\\Process_Loopback";

const int kRate = 44100, kChannels = 2;  // the format Microsoft's own sample asks for

std::string utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(size_t(n) - 1);
    return s;
}

std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return std::wstring();
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(size_t(n) - 1);
    return w;
}

bool sameName(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

std::wstring exeOfPidW(DWORD pid) {
    std::wstring name;
    const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return name;
    wchar_t path[1024];
    DWORD len = 1024;
    if (QueryFullProcessImageNameW(h, 0, path, &len)) {
        const wchar_t* slash = std::wcsrchr(path, L'\\');
        name = slash ? slash + 1 : path;
    }
    CloseHandle(h);
    return name;
}

BOOL CALLBACK onWindow(HWND w, LPARAM lp) {
    auto* out = reinterpret_cast<std::vector<AppInfo>*>(lp);
    if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER) != nullptr) return TRUE;
    if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    wchar_t title[256];
    if (GetWindowTextW(w, title, 256) <= 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return TRUE;
    const std::wstring exe = exeOfPidW(pid);
    if (exe.empty() || sameName(exe, L"explorer.exe")) return TRUE;
    const std::string exe8 = utf8(exe.c_str());
    for (const AppInfo& a : *out)
        if (sameName(wide(a.exe), exe)) return TRUE;  // one entry per program
    AppInfo info;
    info.pid = pid;
    info.exe = exe8;
    info.title = utf8(title);
    out->push_back(info);
    return TRUE;
}

// Tells us when ActivateAudioInterfaceAsync has finished. Windows requires the object to be
// "agile" (callable from any thread), which it signals by answering for IAgileObject.
class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler {
public:
    ActivateHandler() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    HANDLE done() const { return done_; }

    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler)) ||
            IsEqualIID(riid, __uuidof(IAgileObject))) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return ULONG(n);
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(done_);
        return S_OK;
    }

private:
    ~ActivateHandler() { CloseHandle(done_); }
    LONG refs_ = 1;
    HANDLE done_;
};

HRESULT activate(DWORD pid, IAudioClient** out) {
    ActivationParams params = {};
    params.type = 1;
    params.target.processId = pid;
    params.target.mode = 0;
    PROPVARIANT pv;
    std::memset(&pv, 0, sizeof(pv));
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ActivateHandler* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(kProcessLoopbackDevice, __uuidof(IAudioClient), &pv, handler, &op);
    if (SUCCEEDED(hr)) {
        if (WaitForSingleObject(handler->done(), 5000) != WAIT_OBJECT_0) {
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        } else {
            HRESULT result = E_FAIL;
            IUnknown* unk = nullptr;
            hr = op->GetActivateResult(&result, &unk);
            if (SUCCEEDED(hr)) hr = result;
            if (SUCCEEDED(hr) && unk) hr = unk->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(out));
            if (unk) unk->Release();
        }
    }
    if (op) op->Release();
    handler->Release();
    return hr;
}

}  // namespace

std::vector<AppInfo> listApps() {
    std::vector<AppInfo> out;
    EnumWindows(onWindow, reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(), [](const AppInfo& a, const AppInfo& b) { return _stricmp(a.exe.c_str(), b.exe.c_str()) < 0; });
    return out;
}

std::string exeOfPid(uint32_t pid) { return utf8(exeOfPidW(pid).c_str()); }

uint32_t findAppPid(const std::string& exe) {
    const std::wstring want = wide(exe);
    if (want.empty()) return 0;
    for (const AppInfo& a : listApps())  // the process that owns the window is the one to follow
        if (sameName(wide(a.exe), want)) return a.pid;

    // No window (yet): take the process with that name whose parent has a different name.
    struct Proc { DWORD pid, parent; };
    std::vector<Proc> matches;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e = {};
    e.dwSize = sizeof(e);
    if (Process32FirstW(snap, &e)) {
        do {
            if (sameName(e.szExeFile, want)) matches.push_back({e.th32ProcessID, e.th32ParentProcessID});
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    for (const Proc& p : matches) {
        bool childOfSame = false;
        for (const Proc& q : matches) childOfSame = childOfSame || q.pid == p.parent;
        if (!childOfSame) return p.pid;
    }
    return matches.empty() ? 0 : matches[0].pid;
}

struct AppCapture::Impl {
    std::thread thread;
    std::atomic<bool> stop{false}, alive{false};
    HANDLE started = nullptr;  // set once the stream runs or has failed
    HRESULT result = E_FAIL;
    const char* stage = "";
    Sink sink;

    void run(DWORD pid) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IAudioClient* client = nullptr;
        IAudioCaptureClient* capture = nullptr;
        const HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        WAVEFORMATEX fmt = {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = WORD(kChannels);
        fmt.nSamplesPerSec = DWORD(kRate);
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = WORD(fmt.nChannels * fmt.wBitsPerSample / 8);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

        stage = "connect to the program";
        HRESULT hr = activate(pid, &client);
        if (SUCCEEDED(hr)) {
            stage = "open its audio stream";
            const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
            const REFERENCE_TIME oneSecond = 10000000;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, oneSecond, 0, &fmt, nullptr);
            if (FAILED(hr))
                hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, oneSecond, 0,
                                        &fmt, nullptr);
        }
        if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
        if (SUCCEEDED(hr)) hr = client->SetEventHandle(ready);
        if (SUCCEEDED(hr)) {
            stage = "start capturing";
            hr = client->Start();
        }
        result = hr;
        alive = SUCCEEDED(hr);
        SetEvent(started);

        std::vector<int16_t> silence;
        while (SUCCEEDED(hr) && !stop) {
            WaitForSingleObject(ready, 100);  // no events arrive while the program is silent
            UINT32 packet = 0;
            while (!stop && SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr))) break;
                if (frames > 0) {
                    if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        silence.assign(size_t(frames) * kChannels, 0);
                        sink(silence.data(), frames, kChannels, kRate);
                    } else {
                        sink(reinterpret_cast<const int16_t*>(data), frames, kChannels, kRate);
                    }
                }
                capture->ReleaseBuffer(frames);
            }
        }

        if (client && SUCCEEDED(hr)) client->Stop();
        if (capture) capture->Release();
        if (client) client->Release();
        CloseHandle(ready);
        if (SUCCEEDED(co)) CoUninitialize();
        alive = false;
    }
};

AppCapture::AppCapture() : p_(new Impl) {}
AppCapture::~AppCapture() {
    stop();
    delete p_;
}

bool AppCapture::start(uint32_t pid, Sink sink, std::string& err) {
    stop();
    p_->sink = std::move(sink);
    p_->stop = false;
    p_->result = E_FAIL;
    p_->started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    p_->thread = std::thread([this, pid] { p_->run(DWORD(pid)); });
    if (WaitForSingleObject(p_->started, 10000) != WAIT_OBJECT_0) stop();  // joins, so the event is no longer used
    CloseHandle(p_->started);
    p_->started = nullptr;
    if (SUCCEEDED(p_->result)) return true;

    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(p_->result));
    err = std::string("Couldn't ") + p_->stage + " (" + code +
          "). Listening to one application needs Windows 11 or an up-to-date Windows 10; try Speakers instead.";
    stop();
    return false;
}

void AppCapture::stop() {
    p_->stop = true;
    if (p_->thread.joinable()) p_->thread.join();
}

bool AppCapture::running() const { return p_->alive; }

#else
// ---------------------------------------------------------------- everything else

const bool kAppCaptureSupported = false;

std::vector<AppInfo> listApps() { return {}; }
uint32_t findAppPid(const std::string&) { return 0; }
std::string exeOfPid(uint32_t) { return std::string(); }

struct AppCapture::Impl {};
AppCapture::AppCapture() : p_(nullptr) {}
AppCapture::~AppCapture() {}
bool AppCapture::start(uint32_t, Sink, std::string& err) {
    err = "Listening to one application needs Windows. Use an input device instead.";
    return false;
}
void AppCapture::stop() {}
bool AppCapture::running() const { return false; }

#endif
