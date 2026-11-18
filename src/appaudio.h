// Sound of one running program (Windows "process loopback"), plus the list of programs to pick from.
// On other systems the functions exist but report that the feature is unavailable.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct AppInfo {
    uint32_t pid = 0;
    std::string exe;    // "VRChat.exe"
    std::string title;  // window title
};

extern const bool kAppCaptureSupported;

std::vector<AppInfo> listApps();                 // programs that have a visible window
uint32_t findAppPid(const std::string& exe);     // 0 if it isn't running
std::string exeOfPid(uint32_t pid);              // empty if the process is gone

class AppCapture {
public:
    // Called from the capture thread with interleaved 16-bit frames.
    using Sink = std::function<void(const int16_t* frames, size_t frameCount, int channels, int rate)>;

    AppCapture();
    ~AppCapture();
    AppCapture(const AppCapture&) = delete;
    AppCapture& operator=(const AppCapture&) = delete;

    // Captures the process and its child processes. Returns once the stream runs, or with `err` set.
    bool start(uint32_t pid, Sink sink, std::string& err);
    void stop();
    bool running() const;

private:
    struct Impl;
    Impl* p_;
};
