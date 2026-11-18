#include "i18n.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>

#include <nlohmann/json.hpp>

// kBuiltIn: the built-in languages; kStrings: each row is the English text followed by its
// translation in every built-in language, in the same order ("" = not translated).
#include "i18n_data.h"

namespace {

struct State {
    std::string code = "en";
    std::unordered_map<std::string, std::string> table;  // English -> current language
    std::unordered_map<std::string, std::string> withId;  // "Label##id" -> "Etikett##id"
    std::vector<UiLanguage> langs;
    std::map<std::string, std::map<std::string, std::string>> files;  // from the "lang" folder, by code
    bool scanned = false;
};

State& st() {
    static State s;
    return s;
}

constexpr size_t kBuiltInCount = sizeof(kBuiltIn) / sizeof(kBuiltIn[0]);
constexpr size_t kStringCount = sizeof(kStrings) / sizeof(kStrings[0]);

int builtInIndex(const std::string& code) {
    for (size_t i = 0; i < kBuiltInCount; ++i)
        if (code == kBuiltIn[i].code) return int(i);
    return -1;
}

// Translation files: lang/<anything>.json = {"code": "tr", "name": "Türkçe", "strings": {"English": "..."}}.
// A file for a built-in language fills in or overrides its texts.
void scan() {
    State& s = st();
    if (s.scanned) return;
    s.scanned = true;
    s.langs.push_back({"en", "English"});
    for (size_t i = 0; i < kBuiltInCount; ++i) s.langs.push_back({kBuiltIn[i].code, kBuiltIn[i].name});
    // Translation files: in "lang" next to the program, or in the settings folder's "lang"
    // (%APPDATA%\CIMA Box\lang), which updates never touch.
    std::vector<std::filesystem::path> folders = {"lang"};
#ifdef _WIN32
    if (const wchar_t* a = _wgetenv(L"APPDATA"); a && *a) folders.push_back(std::filesystem::path(a) / L"CIMA Box" / L"lang");
#else
    if (const char* h = std::getenv("HOME"); h && *h) folders.push_back(std::filesystem::path(h) / ".config" / "cimabox" / "lang");
#endif
    std::error_code ec;
    for (const auto& folder : folders)
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec || !entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream f(entry.path());
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (!j.is_object() || !j.contains("code") || !j["code"].is_string() || !j.contains("strings") || !j["strings"].is_object())
            continue;
        const std::string code = j["code"].get<std::string>();
        if (code.empty() || code == "en") continue;
        auto& table = s.files[code];
        for (auto it = j["strings"].begin(); it != j["strings"].end(); ++it)
            if (it.value().is_string() && !it.value().get<std::string>().empty()) table[it.key()] = it.value().get<std::string>();
        bool known = false;
        for (const UiLanguage& l : s.langs) known = known || l.code == code;
        if (!known) s.langs.push_back({code, j.contains("name") && j["name"].is_string() ? j["name"].get<std::string>() : code});
    }
}

// The % conversions of a printf format, e.g. "%.1f %s" -> "fs".
std::string conversions(const char* f) {
    std::string out;
    for (const char* p = f; *p; ++p) {
        if (*p != '%') continue;
        ++p;
        if (*p == '%') continue;
        while (*p && std::strchr("-+ #0123456789.*hlLzjt", *p)) ++p;
        if (!*p) break;
        out += *p;
    }
    return out;
}

}  // namespace

const std::vector<UiLanguage>& uiLanguages() {
    scan();
    return st().langs;
}

void setUiLanguage(const std::string& code) {
    scan();
    State& s = st();
    s.table.clear();
    s.withId.clear();
    s.code = "en";
    bool known = code == "en";
    for (const UiLanguage& l : s.langs) known = known || l.code == code;
    if (!known || code == "en") return;
    s.code = code;
    const int b = builtInIndex(code);
    if (b >= 0)
        for (size_t i = 0; i < kStringCount; ++i) {
            const char* t = kStrings[i][size_t(b) + 1];
            if (t && *t) s.table[kStrings[i][0]] = t;
        }
    const auto f = s.files.find(code);
    if (f != s.files.end())
        for (const auto& kv : f->second) s.table[kv.first] = kv.second;
}

const std::string& uiLanguage() { return st().code; }

const char* T(const char* english) {
    State& s = st();
    if (!english || s.table.empty()) return english;
    const char* hash = std::strstr(english, "##");
    if (!hash) {
        const auto it = s.table.find(english);
        return it == s.table.end() ? english : it->second.c_str();
    }
    const auto done = s.withId.find(english);
    if (done != s.withId.end()) return done->second.c_str();
    const auto it = s.table.find(std::string(english, size_t(hash - english)));
    if (it == s.table.end()) return english;
    return s.withId.emplace(english, it->second + hash).first->second.c_str();
}

const char* TF(const char* englishFormat) {
    const char* t = T(englishFormat);
    if (t == englishFormat) return t;
    return conversions(t) == conversions(englishFormat) ? t : englishFormat;
}

size_t builtInStringCount() { return kStringCount; }

size_t builtInMissing(const std::string& code) {
    const int b = builtInIndex(code);
    if (b < 0) return kStringCount;
    size_t n = 0;
    for (size_t i = 0; i < kStringCount; ++i) {
        const char* t = kStrings[i][size_t(b) + 1];
        if (!t || !*t) ++n;
    }
    return n;
}
