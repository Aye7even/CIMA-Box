#include "applog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <set>

namespace {

std::mutex gMtx;
std::filesystem::path gPath;
std::set<std::string> gSecrets;
bool gFirstOpen = true, gLastClosed = true;

constexpr long kMaxBytes = 1024 * 1024;  // past this, keep only the newest part
constexpr long kKeepBytes = 256 * 1024;

bool keyChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':'; }

std::string stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    return b;
}

}  // namespace

std::string logRedact(const std::string& text) {
    std::string s = text;
    {
        std::lock_guard<std::mutex> lk(gMtx);
        for (const std::string& k : gSecrets) {
            if (k.size() < 8) continue;
            for (size_t at; (at = s.find(k)) != std::string::npos;) s.replace(at, k.size(), "[key]");
        }
    }
    // Anything key-shaped: a long unbroken run of letters, digits, _ - : that mixes letters and digits
    // (ElevenLabs sk_..., Claude sk-ant-..., Gemini AIza..., DeepL ...:fx).
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (!keyChar(s[i])) { out += s[i++]; continue; }
        size_t j = i;
        bool alpha = false, digit = false;
        while (j < s.size() && keyChar(s[j])) {
            alpha = alpha || std::isalpha(static_cast<unsigned char>(s[j]));
            digit = digit || std::isdigit(static_cast<unsigned char>(s[j]));
            ++j;
        }
        if (j - i >= 28 && alpha && digit) out += "[hidden]";
        else out.append(s, i, j - i);
        i = j;
    }
    for (char& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    return out;
}

void logOpen(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lk(gMtx);
    gPath = path;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) { gFirstOpen = false; return; }
    const long size = long(in.tellg());
    if (gFirstOpen) {  // how did the last run end? (its final line)
        gFirstOpen = false;
        const long tailSize = std::min<long>(size, 512);
        std::string tail(size_t(tailSize), '\0');
        in.seekg(size - tailSize);
        in.read(&tail[0], tailSize);
        while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r')) tail.pop_back();
        const size_t nl = tail.find_last_of('\n');
        const std::string last = nl == std::string::npos ? tail : tail.substr(nl + 1);
        gLastClosed = last.empty() || last.find("Closed.") != std::string::npos;
        in.clear();
    }
    if (size <= kMaxBytes) return;
    in.seekg(size - kKeepBytes);
    std::string tail((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const size_t nl = tail.find('\n');  // start on a whole line
    if (nl != std::string::npos) tail.erase(0, nl + 1);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "(older lines removed)\n" << tail;
}

void logSecret(const std::string& secret) {
    std::string k = secret;
    while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
    while (!k.empty() && std::isspace(static_cast<unsigned char>(k.front()))) k.erase(k.begin());
    if (k.size() < 8) return;
    std::lock_guard<std::mutex> lk(gMtx);
    gSecrets.insert(k);
}

void logLine(const std::string& text) {
    const std::string line = stamp() + "  " + logRedact(text) + "\n";
    std::lock_guard<std::mutex> lk(gMtx);
    if (gPath.empty()) return;
    std::ofstream f(gPath, std::ios::binary | std::ios::app);
    f << line;
}

bool logLastRunClosed() {
    std::lock_guard<std::mutex> lk(gMtx);
    return gLastClosed;
}

std::filesystem::path logPath() {
    std::lock_guard<std::mutex> lk(gMtx);
    return gPath;
}
