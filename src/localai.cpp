#include "localai.h"

#include "http.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

std::string gHf = "https://huggingface.co";
std::string gGh = "https://api.github.com";

// The llama.cpp release the engine comes from. Pinned, so everyone runs the same engine the models
// below were chosen for; a newer one is only used when this one can't be found any more.
const char* kEngineTag = "v1.0.0";
#ifdef _WIN32
const char* kEngineExe = "llama-server.exe";
#else
const char* kEngineExe = "llama-server";
#endif

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
uint64_t fileSize(const fs::path& p) {
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    return ec ? 0 : uint64_t(n);
}

// ---------------------------------------------------------------- SHA-256

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64];
    size_t fill = 0;
    uint64_t bits = 0;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const unsigned char* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void add(const unsigned char* p, size_t n) {
        bits += uint64_t(n) * 8;
        while (n > 0) {
            const size_t take = std::min(n, sizeof(buf) - fill);
            std::memcpy(buf + fill, p, take);
            fill += take; p += take; n -= take;
            if (fill == sizeof(buf)) { block(buf); fill = 0; }
        }
    }
    std::string hex() {
        const uint64_t total = bits;
        const unsigned char one = 0x80, zero = 0;
        add(&one, 1);
        while (fill != 56) add(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(total >> (56 - 8 * i));
        add(len, 8);
        char out[65];
        for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
        return std::string(out, 64);
    }
};

// ---------------------------------------------------------------- running the engine

#ifdef _WIN32
struct Proc {
    HANDLE process = nullptr, job = nullptr;
};
std::wstring quoted(const std::wstring& s) { return L"\"" + s + L"\""; }

bool spawn(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd, const fs::path& logFile, Proc& p) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(logFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return false;
    std::wstring cmd = quoted(exe.wstring());
    for (const std::string& a : args) cmd += L" " + quoted(fs::u8path(a).wstring());
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::vector<wchar_t> line(cmd.begin(), cmd.end());
    line.push_back(L'\0');
    // Hidden, and suspended until it's in a job that ends it with CIMA Box: it can never be left
    // running (holding memory) after the app closes or crashes.
    const BOOL ok = CreateProcessW(exe.wstring().c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                   nullptr, cwd.wstring().c_str(), &si, &pi);
    CloseHandle(log);
    if (!ok) return false;
    p.job = CreateJobObjectW(nullptr, nullptr);
    if (p.job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        ZeroMemory(&info, sizeof(info));
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(p.job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        AssignProcessToJobObject(p.job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    p.process = pi.hProcess;
    return true;
}
bool alive(const Proc& p) { return p.process && WaitForSingleObject(p.process, 0) == WAIT_TIMEOUT; }
bool waitExit(Proc& p, int ms, int& code) {
    if (!p.process || WaitForSingleObject(p.process, DWORD(ms)) != WAIT_OBJECT_0) return false;
    DWORD c = 1;
    GetExitCodeProcess(p.process, &c);
    code = int(c);
    return true;
}
void endProc(Proc& p) {
    if (p.process) {
        TerminateProcess(p.process, 1);
        WaitForSingleObject(p.process, 5000);
        CloseHandle(p.process);
    }
    if (p.job) CloseHandle(p.job);
    p.process = nullptr;
    p.job = nullptr;
}
fs::path unzipTool() {
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    return fs::path(std::wstring(sys, n)) / L"tar.exe";  // part of Windows since Windows 10 (1803); it reads .zip
}
std::vector<std::string> unzipArgs(const fs::path& zip, const fs::path& into) {
    return {"-xf", zip.u8string(), "-C", into.u8string()};
}
#else
struct Proc {
    pid_t pid = -1;
};
bool spawn(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd, const fs::path& logFile, Proc& p) {
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        const int fd = open(logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        if (chdir(cwd.c_str()) != 0) _exit(127);
        std::vector<char*> argv;
        std::string name = exe.string();
        argv.push_back(name.data());
        std::vector<std::string> copy = args;
        for (std::string& a : copy) argv.push_back(a.data());
        argv.push_back(nullptr);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    p.pid = pid;
    return true;
}
bool alive(const Proc& p) {
    if (p.pid <= 0) return false;
    int st = 0;
    return waitpid(p.pid, &st, WNOHANG) == 0;
}
bool waitExit(Proc& p, int ms, int& code) {
    for (int t = 0; t <= ms; t += 20) {
        int st = 0;
        if (p.pid <= 0 || waitpid(p.pid, &st, WNOHANG) != 0) {
            code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
            p.pid = -1;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
void endProc(Proc& p) {
    if (p.pid > 0) {
        kill(p.pid, SIGTERM);
        int code = 0;
        if (!waitExit(p, 3000, code)) {
            kill(p.pid, SIGKILL);
            waitpid(p.pid, nullptr, 0);
        }
    }
    p.pid = -1;
}
fs::path unzipTool() { return "/usr/bin/unzip"; }
std::vector<std::string> unzipArgs(const fs::path& zip, const fs::path& into) { return {"-o", "-q", zip.string(), "-d", into.string()}; }
#endif

std::string randomHex(int chars) {
    std::random_device rd;
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < chars; ++i) s += digits[rd() & 15];
    return s;
}

// The last thing the engine wrote, for the message when it didn't start.
std::string lastLine(const fs::path& logFile) {
    std::ifstream f(logFile, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    const std::streamoff from = size > 2000 ? size - 2000 : 0;
    f.seekg(from);
    std::string tail(size_t(size - from), '\0');
    f.read(tail.data(), std::streamsize(tail.size()));
    std::string best, line;
    for (char c : tail + "\n") {
        if (c == '\n' || c == '\r') {
            if (line.find_first_not_of(" \t") != std::string::npos) best = line;
            line.clear();
        } else {
            line += c;
        }
    }
    if (best.size() > 200) best = best.substr(0, 200);
    return best;
}

// The graphics card the engine says it's using, from what it wrote while starting: any card that
// speaks Vulkan (NVIDIA, AMD, Intel). "" when it didn't say.
std::string deviceFromLog(const fs::path& logFile) {
    std::ifstream f(logFile, std::ios::binary);
    std::string line;
    for (int n = 0; f && n < 4000 && std::getline(f, line); ++n) {
        size_t a = std::string::npos;
        const size_t v = line.find("ggml_vulkan:");
        if (v != std::string::npos) {
            const size_t eq = line.find(" = ", v);
            if (eq != std::string::npos && line.find("Found") == std::string::npos) a = eq + 3;
        }
        if (a == std::string::npos) {
            const size_t u = line.find("using device ");
            const size_t open = u == std::string::npos ? std::string::npos : line.find('(', u);
            if (open != std::string::npos) a = open + 1;
        }
        if (a == std::string::npos) continue;
        size_t b = line.find_first_of("(|)", a);
        if (b == std::string::npos) b = line.size();
        std::string name = line.substr(a, b - a);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\r')) name.pop_back();
        if (name.size() >= 3 && name.size() <= 80) return name;
    }
    return {};
}

const char* kDiskFull = "Not enough free space on the disk.";
const char* kNoNet = "Couldn't download it. Check your connection and try again.";

}  // namespace

void setLocalAiHosts(const std::string& huggingFace, const std::string& gitHubApi) {
    gHf = huggingFace;
    gGh = gitHubApi;
}

std::string sha256Hex(const fs::path& file, const std::function<bool()>& keepGoing) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return {};
    Sha256 sha;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), std::streamsize(buf.size()));
        const std::streamsize n = f.gcount();
        if (n > 0) sha.add(reinterpret_cast<const unsigned char*>(buf.data()), size_t(n));
        if (keepGoing && !keepGoing()) return {};
    }
    return sha.hex();
}

const std::vector<LocalModel>& localCatalog() {
    // Google's Gemma 4 with its refusals removed ("uncensored"), in the sizes that leave room for a game
    // on the same graphics card. In a voice chat people swear, insult each other for fun and say crude
    // things, and the stock models soften or decline those lines; these say them as they were said.
    // They follow CIMA Box's translation instructions (tone, slang, context, the answer tags) like the
    // online AIs do, which the small translation-only models can't. All three are HauhauCS's builds
    // (4-bit, made with an importance matrix; the 12B one from Google's quantization-aware weights).
    // Its think-through is switched off per request, like every local model's (translate.cpp).
    static const std::vector<LocalModel> k = {
        {"gemma4u-e2b", "Gemma 4 E2B Uncensored", "Light", "HauhauCS/Gemma-4-E2B-Uncensored-HauhauCS-Aggressive", "Q4_K_P", 3.45, 5,
         "For most PCs, and while a game is using the graphics card. Fine for everyday lines; weakest with slang.", false, false},
        {"gemma4u-e4b", "Gemma 4 E4B Uncensored", "Balanced", "HauhauCS/Gemma-4-E4B-Uncensored-HauhauCS-Aggressive", "Q4_K_M", 5.0, 7,
         "The one to try first on a graphics card with 8 GB or more. Clearly better wording than Light.", false, false},
        {"gemma4u-12b", "Gemma 4 12B Uncensored", "Best", "HauhauCS/Gemma4-12B-QAT-Uncensored-HauhauCS-Balanced", "Q4_K_M", 7.4, 11,
         "The closest to the online AIs. Wants a graphics card with 12 GB or more, or it's slow.", false, false},
        // Alibaba's Qwen3.8-27B (Apache 2.0 licence) with its refusals removed ("abliterated"), for big
        // graphics cards. One model in three sizes of the same file (3, 4 and 5 bits per weight); the
        // edit is applied to Unsloth's builds, so mainline llama.cpp runs them.
        {"qwen38-27b-q3", "Qwen 3.8 27B Uncensored", "Light", "mrfakename/Qwen3.8-27B-abliterated-GGUF", "UD-Q3_K_XL", 13.1, 15,
         "Uncensored: translates swearing and crude lines as they were said. Wants a graphics card with 16 GB, and it's tight next to a game.", true, false},
        {"qwen38-27b-q4", "Qwen 3.8 27B Uncensored", "Balanced", "mrfakename/Qwen3.8-27B-abliterated-GGUF", "UD-Q4_K_XL", 17.7, 20,
         "Uncensored, with more natural wording than its Light size. Wants a graphics card with 24 GB.", true, false},
        {"qwen38-27b-q5", "Qwen 3.8 27B Uncensored", "Best", "mrfakename/Qwen3.8-27B-abliterated-GGUF", "UD-Q5_K_XL", 21.0, 24,
         "Uncensored, and the closest to the full model. Wants a graphics card with 32 GB; for one-line translations it's hardly better than Balanced.", true, false},
        // The stock Gemma 4 models these replaced (3.1.0 to 3.1.3). Not offered any more, but someone
        // who downloaded one keeps it: it still shows for them, to go on using or to delete.
        {"gemma4-e2b", "Gemma 4 E2B", "Light", "unsloth/gemma-4-E2B-it-qat-GGUF", "UD-Q4_K_XL", 2.6, 4,
         "For most PCs, and while a game is using the graphics card. Fine for everyday lines; weakest with slang.", false, true},
        {"gemma4-e4b", "Gemma 4 E4B", "Balanced", "unsloth/gemma-4-E4B-it-qat-GGUF", "UD-Q4_K_XL", 4.2, 6,
         "The one to try first on a graphics card with 8 GB or more. Clearly better wording than Light.", false, true},
        {"gemma4-12b", "Gemma 4 12B", "Best", "unsloth/gemma-4-12B-it-qat-GGUF", "UD-Q4_K_XL", 6.7, 10,
         "The closest to the online AIs. Wants a graphics card with 12 GB or more, or it's slow.", false, true},
    };
    return k;
}

const LocalModel* findLocalModel(const std::string& id) {
    for (const LocalModel& m : localCatalog())
        if (id == m.id) return &m;
    return nullptr;
}

bool pickModelFile(const std::string& treeJson, const std::string& quant, std::string& path, uint64_t& size, std::string& sha256) {
    const json j = json::parse(treeJson, nullptr, false);
    if (!j.is_array()) return false;
    const std::string want = lower(quant);
    bool found = false;
    for (const json& f : j) {
        if (!f.is_object() || f.value("type", "") != "file" || !f.contains("path") || !f["path"].is_string()) continue;
        const std::string p = f["path"].get<std::string>(), low = lower(p);
        if (!endsWith(low, ".gguf") || low.find('/') != std::string::npos) continue;  // not the helper files in sub-folders
        if (low.find(want) == std::string::npos) continue;
        if (low.rfind("mtp", 0) == 0 || low.rfind("mmproj", 0) == 0) continue;  // a drafter, or the picture part: not the model
        if (found && p.size() >= path.size()) continue;                            // the plainest name wins
        path = p;
        size = f.contains("size") && f["size"].is_number_unsigned() ? f["size"].get<uint64_t>() : 0;
        sha256.clear();
        if (f.contains("lfs") && f["lfs"].is_object()) {
            if (f["lfs"].contains("oid") && f["lfs"]["oid"].is_string()) sha256 = lower(f["lfs"]["oid"].get<std::string>());
            if (f["lfs"].contains("size") && f["lfs"]["size"].is_number_unsigned()) size = f["lfs"]["size"].get<uint64_t>();
        }
        found = true;
    }
    return found;
}

bool pickEngineAsset(const std::string& releaseJson, std::string& url, uint64_t& size, std::string& sha256, std::string& nightlyUrl) {
    const json j = json::parse(releaseJson, nullptr, false);
    nightlyUrl.clear();
    if (!j.is_object() || !j.contains("assets") || !j["assets"].is_array()) return false;
    // Vulkan runs on any graphics card (NVIDIA, AMD, Intel) and on the processor alone too. The
    // plain processor build is the fallback.
    for (const char* kind : {"bin-win-vulkan-x64.zip", "bin-win-cpu-x64.zip"})
        for (const json& a : j["assets"]) {
            if (!a.is_object() || !a.contains("name") || !a["name"].is_string()) continue;
            const std::string name = a["name"].get<std::string>();
            if (!endsWith(name, kind) || name.rfind("llama-", 0) != 0) continue;
            if (!a.contains("browser_download_url") || !a["browser_download_url"].is_string()) continue;
            url = a["browser_download_url"].get<std::string>();
            size = a.contains("size") && a["size"].is_number_unsigned() ? a["size"].get<uint64_t>() : 0;
            sha256.clear();
            if (a.contains("digest") && a["digest"].is_string()) {
                const std::string d = lower(a["digest"].get<std::string>());
                if (d.rfind("sha256:", 0) == 0) sha256 = d.substr(7);
            }
            return true;
        }
    for (const json& a : j["assets"])
        if (a.is_object() && a.value("name", "") == "nightly-tag.txt" && a.contains("browser_download_url") &&
            a["browser_download_url"].is_string())
            nightlyUrl = a["browser_download_url"].get<std::string>();
    return false;
}

// ---------------------------------------------------------------- LocalAi

struct LocalAi::Impl {
    fs::path dir;
    std::function<void(const std::string&)> log;

    mutable std::mutex mx;
    LocalState st;
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> cancels;
    struct Job { bool removal; std::string id; };
    std::deque<Job> jobs;
    std::condition_variable wake;
    std::string wantId;
    bool wantGpu = true;
    std::vector<std::string> removing;  // being deleted: the engine lets go of them first

    std::mutex engineMx;  // one engine install at a time
    std::atomic<bool> quit{false};
    std::thread downloader, runner;

    fs::path modelFile(const std::string& id) const { return dir / "models" / (id + ".gguf"); }
    fs::path partFile(const std::string& id) const { return dir / "models" / (id + ".gguf.part"); }
    fs::path engineDir() const { return dir / "engine"; }

    fs::path engineExe() const {
        std::error_code ec;
        if (!fs::exists(engineDir() / "ok.txt", ec)) return {};
        for (fs::recursive_directory_iterator it(engineDir(), ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().filename() == kEngineExe) return it->path();
        return {};
    }

    LocalModelState* model(const std::string& id) {
        for (LocalModelState& m : st.models)
            if (m.id == id) return &m;
        return nullptr;
    }
    void setModel(const std::string& id, const std::function<void(LocalModelState&)>& f) {
        std::lock_guard<std::mutex> lk(mx);
        if (LocalModelState* m = model(id)) f(*m);
    }
    void setEngine(int stage, const std::string& message) {
        std::lock_guard<std::mutex> lk(mx);
        st.engine = stage;
        st.engineMessage = message;
        if (stage != kEngineReady) {
            st.device.clear();
            st.running.clear();
            st.url.clear();
            st.key.clear();
        }
        if (stage != kEngineGetting) st.engineDone = st.engineTotal = 0;
    }
    void refreshDisk() {
        uint64_t total = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec)) total += fileSize(it->path());
        std::lock_guard<std::mutex> lk(mx);
        st.diskBytes = total;
    }

    void scan() {
        std::error_code ec;
        fs::create_directories(dir / "models", ec);
        std::lock_guard<std::mutex> lk(mx);
        st.models.clear();
        for (const LocalModel& m : localCatalog()) {
            LocalModelState s;
            s.id = m.id;
            s.total = uint64_t(m.sizeGb * 1e9);
            if (fs::exists(modelFile(m.id), ec)) {
                s.stage = kModelInstalled;
                s.bytes = fileSize(modelFile(m.id));
            } else {
                s.done = fileSize(partFile(m.id));  // a download to go on with
            }
            st.models.push_back(s);
        }
    }

    // ---- downloads

    // Downloads `url` to `file` (going on from what's already there). `done` is kept up to date.
    // "" when it's all there; otherwise why not ("cancelled" when stopped).
    std::string fetchFile(const std::string& url, const fs::path& file, uint64_t size, std::atomic<bool>& cancel,
                          const std::function<void(uint64_t)>& progress) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            uint64_t have = fileSize(file);
            if (size > 0 && have == size) return {};
            if (size > 0 && have > size) {  // not this file's beginning: start over
                std::error_code ec;
                fs::remove(file, ec);
                have = 0;
            }
            std::ofstream out(file, std::ios::binary | std::ios::app);
            if (!out) return "Couldn't write to " + file.filename().string() + ".";
            std::atomic<bool> writeFailed{false};
            uint64_t got = have;
            const ByteSink sink = [&](const char* p, size_t n) {
                out.write(p, std::streamsize(n));
                if (!out) writeFailed = true;
                got += n;
                progress(got);
            };
            Http http;
            http.cancel = &cancel;
            http.stop = &writeFailed;
            HttpRequest r;
            r.url = url;
            r.sink = &sink;
            r.followRedirects = true;       // both GitHub and Hugging Face hand downloads to a file server
            r.timeoutSec = 12 * 60 * 60;    // big files on slow connections
            r.stallSec = 60;                // ...but not one that stopped moving
            if (have > 0) r.headers.push_back("Range: bytes=" + std::to_string(have) + "-");
            long status = 0;
            std::string body, err;
            const bool ok = http.perform(r, status, body, err);
            out.close();
            if (cancel) return "cancelled";
            if (writeFailed) return kDiskFull;
            if (!ok) return kNoNet;
            if (status == 416 || (have > 0 && status == 200)) {
                // It wouldn't go on from the middle (200 means it sent the whole file again, after
                // what was there): start over, once.
                std::error_code ec;
                fs::remove(file, ec);
                progress(0);
                continue;
            }
            if (status != 200 && status != 206) return "The download was refused (" + std::to_string(status) + ").";
            if (size > 0 && fileSize(file) != size) return kNoNet;  // cut short: "Continue" goes on from here
            return {};
        }
        return kNoNet;
    }

    bool getJson(const std::string& url, std::atomic<bool>& cancel, std::string& body) {
        Http http;
        http.cancel = &cancel;
        HttpRequest r;
        r.url = url;
        r.headers = {"Accept: application/json"};
        r.followRedirects = true;
        r.timeoutSec = 30;
        long status = 0;
        std::string err;
        return http.perform(r, status, body, err) && status == 200;
    }

    // The engine, downloaded and unpacked. "" when it's there.
    std::string ensureEngine(std::atomic<bool>& cancel) {
        std::lock_guard<std::mutex> lk(engineMx);
        if (!engineExe().empty()) return {};
        setEngine(kEngineGetting, "Getting the engine");
        std::string url, sha, nightly, body;
        uint64_t size = 0;
        bool found = false;
        // The pinned release; the newest one only if that's gone. A release can name another one
        // that holds the files (nightly-tag.txt).
        for (const std::string& rel : {std::string("/releases/tags/") + kEngineTag, std::string("/releases/latest")}) {
            if (!getJson(gGh + "/repos/ggml-org/llama.cpp" + rel, cancel, body)) continue;
            found = pickEngineAsset(body, url, size, sha, nightly);
            if (!found && !nightly.empty()) {
                Http http;
                http.cancel = &cancel;
                HttpRequest r;
                r.url = nightly;
                r.followRedirects = true;
                r.timeoutSec = 30;
                long status = 0;
                std::string tag, err;
                if (http.perform(r, status, tag, err) && status == 200) {
                    std::string clean;
                    for (char c : tag)
                        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') clean += c;
                    if (!clean.empty() && clean.size() < 40 && getJson(gGh + "/repos/ggml-org/llama.cpp/releases/tags/" + clean, cancel, body))
                        found = pickEngineAsset(body, url, size, sha, nightly);
                }
            }
            if (found) break;
        }
        if (cancel) return "cancelled";
        if (!found) return "Couldn't find the engine to download. Check your connection and try again.";
        std::error_code ec;
        fs::remove_all(engineDir(), ec);
        fs::create_directories(engineDir(), ec);
        const fs::path zip = dir / "engine.zip";
        fs::remove(zip, ec);
        {
            std::lock_guard<std::mutex> l2(mx);
            st.engineTotal = size;
        }
        const std::string why = fetchFile(url, zip, size, cancel, [&](uint64_t got) {
            std::lock_guard<std::mutex> l2(mx);
            st.engineDone = got;
        });
        if (!why.empty()) { fs::remove(zip, ec); return why; }
        if (!sha.empty() && sha256Hex(zip, nullptr) != sha) {
            fs::remove(zip, ec);
            return "The engine didn't download correctly. Try again.";
        }
        Proc unzip;
        int code = 1;
        const bool ran = spawn(unzipTool(), unzipArgs(zip, engineDir()), dir, dir / "unzip.log", unzip);
        const bool finished = ran && waitExit(unzip, 180000, code);
        if (ran && !finished) endProc(unzip);
        fs::remove(zip, ec);
        fs::remove(dir / "unzip.log", ec);
        if (!finished || code != 0) return "Couldn't unpack the engine.";
        { std::ofstream ok(engineDir() / "ok.txt"); ok << kEngineTag << "\n"; }
        if (engineExe().empty()) {
            fs::remove_all(engineDir(), ec);
            return "The engine download didn't have what was expected in it.";
        }
        log("Local models: the engine is installed.");
        return {};
    }

    void doDownload(const std::string& id) {
        const LocalModel* m = findLocalModel(id);
        if (!m) return;
        std::shared_ptr<std::atomic<bool>> cancel;
        {
            std::lock_guard<std::mutex> lk(mx);
            cancel = cancels[id];
        }
        if (!cancel || *cancel) {
            setModel(id, [](LocalModelState& s) { if (s.stage == kModelQueued) s.stage = kModelNone; });
            return;
        }
        auto fail = [&](const std::string& why) {
            const uint64_t have = fileSize(partFile(id));
            setModel(id, [&](LocalModelState& s) {
                s.stage = why == "cancelled" ? kModelNone : kModelFailed;
                s.message = why == "cancelled" ? std::string() : why;
                s.done = have;
            });
            if (why != "cancelled") log(std::string("Local models: ") + m->name + ": " + why);
        };
        setModel(id, [&](LocalModelState& s) {
            s.stage = kModelDownloading;
            s.message.clear();
            s.done = fileSize(partFile(id));
        });
        // Which file exactly, how big, and its checksum: from Hugging Face's own list.
        std::string tree, path, sha;
        uint64_t size = 0;
        if (!getJson(gHf + "/api/models/" + m->repo + "/tree/main?recursive=true", *cancel, tree)) return fail(*cancel ? "cancelled" : kNoNet);
        if (!pickModelFile(tree, m->quant, path, size, sha)) return fail("Couldn't find this model's file to download.");
        setModel(id, [&](LocalModelState& s) { if (size) s.total = size; });
        std::error_code ec;
        const uint64_t have = fileSize(partFile(id));
        const auto space = fs::space(dir, ec);
        if (!ec && size > have && space.available < size - have + (uint64_t(1) << 30)) return fail(kDiskFull);
        std::string why = fetchFile(gHf + "/" + m->repo + "/resolve/main/" + path, partFile(id), size, *cancel, [&](uint64_t got) {
            std::lock_guard<std::mutex> lk(mx);
            if (LocalModelState* s = model(id)) s->done = got;
        });
        if (!why.empty()) return fail(why);
        setModel(id, [](LocalModelState& s) { s.stage = kModelChecking; });
        if (!sha.empty()) {
            const std::string got = sha256Hex(partFile(id), [&] { return !cancel->load() && !quit.load(); });
            if (*cancel || quit) return fail("cancelled");
            if (got != sha) {  // damaged on the way: nothing of it can be trusted
                fs::remove(partFile(id), ec);
                return fail("The download was damaged on the way. Try again.");
            }
        }
        fs::rename(partFile(id), modelFile(id), ec);
        if (ec) return fail("Couldn't save the model.");
        setModel(id, [&](LocalModelState& s) {
            s.stage = kModelInstalled;
            s.bytes = fileSize(modelFile(id));
            s.done = 0;
        });
        log(std::string("Local models: ") + m->name + " is downloaded.");
        // The engine too, so the model is ready to use (and works offline from here on).
        why = ensureEngine(*cancel);
        if (!why.empty() && why != "cancelled") setEngine(kEngineFailed, why);
        else if (why.empty()) {
            std::lock_guard<std::mutex> lk(mx);
            if (st.engine == kEngineGetting) { st.engine = kEngineOff; st.engineMessage.clear(); }
        }
        refreshDisk();
    }

    void doRemove(const std::string& id) {
        // The engine has the file open while it runs: wait for it to let go.
        for (int t = 0; t < 100 && !quit; ++t) {
            {
                std::lock_guard<std::mutex> lk(mx);
                if (st.running != id && st.engine != kEngineStarting) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::error_code ec;
        fs::remove(modelFile(id), ec);
        fs::remove(partFile(id), ec);
        bool any = false;
        {
            std::lock_guard<std::mutex> lk(mx);
            if (LocalModelState* s = model(id)) {
                const uint64_t total = s->total;
                *s = LocalModelState();
                s->id = id;
                s->total = total;
            }
            removing.erase(std::remove(removing.begin(), removing.end(), id), removing.end());
            for (const LocalModelState& s : st.models) any = any || s.stage != kModelNone || s.done > 0;
        }
        if (const LocalModel* m = findLocalModel(id)) log(std::string("Local models: ") + m->name + " was deleted.");
        if (!any) {  // nothing left to run: the engine goes too, so deleting everything frees everything
            std::lock_guard<std::mutex> lk(engineMx);
            fs::remove_all(engineDir(), ec);
            fs::remove(dir / "engine.log", ec);
            setEngine(kEngineOff, "");
        }
        refreshDisk();
    }

    void downloadLoop() {
        refreshDisk();
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(mx);
                wake.wait(lk, [&] { return quit || !jobs.empty(); });
                if (quit) return;
                job = jobs.front();
                jobs.pop_front();
            }
            if (job.removal) doRemove(job.id);
            else doDownload(job.id);
        }
    }

    // ---- the engine

    void runLoop() {
        Proc proc;
        std::string runId;
        bool runGpu = true;
        std::string failedId;
        bool failedGpu = true;
        Clock::time_point retryAt{};
        std::atomic<bool> never{false};
        auto stopEngine = [&] {
            if (!runId.empty()) log("Local models: stopped.");
            endProc(proc);
            runId.clear();
        };
        while (!quit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            std::string id;
            bool gpu = true;
            {
                std::lock_guard<std::mutex> lk(mx);
                id = wantId;
                gpu = wantGpu;
                if (std::find(removing.begin(), removing.end(), id) != removing.end()) id.clear();
                const LocalModelState* s = id.empty() ? nullptr : model(id);
                if (!s || s->stage != kModelInstalled) id.clear();
            }
            if (id.empty()) {
                if (!runId.empty() || alive(proc)) stopEngine();
                std::lock_guard<std::mutex> lk(mx);
                if (st.engine == kEngineReady || st.engine == kEngineStarting || (st.engine == kEngineFailed && wantId.empty())) {
                    st.engine = kEngineOff;
                    st.engineMessage.clear();
                    st.device.clear();
                    st.running.clear();
                    st.url.clear();
                    st.key.clear();
                }
                failedId.clear();
                continue;
            }
            if (runId == id && runGpu == gpu && alive(proc)) continue;  // answering
            if (failedId == id && failedGpu == gpu && Clock::now() < retryAt) continue;  // it just failed: not in a loop
            const bool died = runId == id && runGpu == gpu;
            stopEngine();
            if (died) log("Local models: the engine stopped by itself. Starting it again.");
            auto failed = [&](const std::string& why) {
                setEngine(kEngineFailed, why);
                log("Local models: " + why);
                failedId = id;
                failedGpu = gpu;
                retryAt = Clock::now() + std::chrono::seconds(60);
            };
            const std::string why = ensureEngine(never);
            if (!why.empty()) { failed(why); continue; }
            const fs::path exe = engineExe();
            const LocalModel* m = findLocalModel(id);
            bool up = false, changed = false, onGpu = gpu;
            std::string note;
            // On the graphics card first; if it can't load there (not enough graphics memory, usually),
            // on the processor.
            for (int layers : gpu ? std::vector<int>{999, 0} : std::vector<int>{0}) {
                setEngine(kEngineStarting, "Loading the model");
                const int port = 39000 + int(std::random_device{}() % 9000);
                const std::string key = randomHex(32);
                // Only on this PC (127.0.0.1), and only with this key: nothing else on the PC or the
                // network can use it. Two lines at once (My voice and Listen).
                const std::vector<std::string> args = {"-m", modelFile(id).u8string(), "--host", "127.0.0.1", "--port", std::to_string(port),
                                                       "--api-key", key, "-ngl", std::to_string(layers), "-c", "8192", "-np", "2"};
                if (!spawn(exe, args, exe.parent_path(), dir / "engine.log", proc)) { note = "The engine couldn't be started."; break; }
                const std::string url = "http://127.0.0.1:" + std::to_string(port);
                const Clock::time_point t0 = Clock::now();
                Http http;
                while (!quit && Clock::now() - t0 < std::chrono::minutes(5)) {
                    if (!alive(proc)) break;
                    {
                        std::lock_guard<std::mutex> lk(mx);
                        if (wantId != id || wantGpu != gpu || std::find(removing.begin(), removing.end(), id) != removing.end()) changed = true;
                    }
                    if (changed) break;
                    HttpRequest r;
                    r.url = url + "/health";
                    r.timeoutMs = 1500;
                    long status = 0;
                    std::string body, err;
                    if (http.perform(r, status, body, err) && status == 200) { up = true; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                if (up) {
                    runId = id;
                    runGpu = gpu;
                    onGpu = layers > 0;
                    std::lock_guard<std::mutex> lk(mx);
                    st.engine = kEngineReady;
                    st.engineMessage = layers == 0 && gpu ? "Running on the processor (the graphics card couldn't load it), so it's slower."
                                                          : std::string();
                    st.running = id;
                    st.device = layers > 0 ? deviceFromLog(dir / "engine.log") : std::string();
                    st.url = url;
                    st.key = key;
                    break;
                }
                const std::string last = lastLine(dir / "engine.log");
                endProc(proc);
                if (changed || quit) break;
                note = "The model couldn't be loaded." + (last.empty() ? std::string() : " (" + last + ")");
            }
            if (up) log(std::string("Local models: ") + (m ? m->name : id.c_str()) + " is ready" + (onGpu ? "." : " (on the processor)."));
            else if (!changed && !quit) failed(note.empty() ? "The model couldn't be loaded." : note);
        }
        endProc(proc);
    }
};

LocalAi::LocalAi(const fs::path& dir, std::function<void(const std::string&)> log) : p_(new Impl) {
    p_->dir = dir;
    p_->log = log ? std::move(log) : [](const std::string&) {};
    p_->scan();
    p_->downloader = std::thread([this] { p_->downloadLoop(); });
    p_->runner = std::thread([this] { p_->runLoop(); });
}

LocalAi::~LocalAi() {
    {
        std::lock_guard<std::mutex> lk(p_->mx);
        p_->quit = true;
        for (auto& c : p_->cancels)
            if (c.second) *c.second = true;
    }
    p_->wake.notify_all();
    if (p_->downloader.joinable()) p_->downloader.join();
    if (p_->runner.joinable()) p_->runner.join();
}

void LocalAi::download(const std::string& id) {
    if (!findLocalModel(id)) return;
    {
        std::lock_guard<std::mutex> lk(p_->mx);
        LocalModelState* s = p_->model(id);
        if (!s || s->stage == kModelInstalled || s->stage == kModelQueued || s->stage == kModelDownloading || s->stage == kModelChecking) return;
        s->stage = kModelQueued;
        s->message.clear();
        p_->cancels[id] = std::make_shared<std::atomic<bool>>(false);
        p_->jobs.push_back({false, id});
    }
    p_->wake.notify_all();
}

void LocalAi::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lk(p_->mx);
    const auto it = p_->cancels.find(id);
    if (it != p_->cancels.end() && it->second) *it->second = true;
}

void LocalAi::remove(const std::string& id) {
    if (!findLocalModel(id)) return;
    {
        std::lock_guard<std::mutex> lk(p_->mx);
        const auto it = p_->cancels.find(id);
        if (it != p_->cancels.end() && it->second) *it->second = true;
        p_->removing.push_back(id);
        p_->jobs.push_back({true, id});
    }
    p_->wake.notify_all();
}

void LocalAi::want(const std::string& id, bool gpu) {
    std::lock_guard<std::mutex> lk(p_->mx);
    p_->wantId = id;
    p_->wantGpu = gpu;
}

LocalState LocalAi::state() const {
    std::lock_guard<std::mutex> lk(p_->mx);
    return p_->st;
}

bool LocalAi::installed(const std::string& id) const {
    std::lock_guard<std::mutex> lk(p_->mx);
    for (const LocalModelState& s : p_->st.models)
        if (s.id == id) return s.stage == kModelInstalled;
    return false;
}
