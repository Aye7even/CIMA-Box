#include "extras.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace {

bool isWordByte(unsigned char c) { return c >= 0x80 || std::isalnum(c) || c == '\''; }

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// A word in the text: [begin, end) bytes.
struct Span { size_t b, e; };
std::vector<Span> words(const std::string& s) {
    std::vector<Span> out;
    for (size_t i = 0; i < s.size();) {
        if (!isWordByte(static_cast<unsigned char>(s[i]))) { ++i; continue; }
        size_t j = i;
        while (j < s.size() && isWordByte(static_cast<unsigned char>(s[j]))) ++j;
        out.push_back({i, j});
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------- swearing

enum Match { kExact, kPrefix, kInside };
struct Swear { const char* word; Match how; };
const Swear kSwears[] = {
    // English
    {"fuck", kInside}, {"shit", kInside}, {"bitch", kInside}, {"asshole", kInside}, {"bastard", kPrefix},
    {"dick", kExact}, {"dickhead", kExact}, {"cunt", kInside}, {"pussy", kExact}, {"whore", kPrefix},
    {"slut", kPrefix}, {"cock", kExact}, {"wanker", kPrefix}, {"bollocks", kExact}, {"twat", kPrefix},
    {"motherfucker", kInside}, {"stfu", kExact}, {"wtf", kExact},
    // Spanish
    {"puta", kPrefix}, {"puto", kPrefix}, {"mierda", kInside}, {"joder", kPrefix}, {"jodido", kPrefix},
    {"coño", kExact}, {"cabrón", kPrefix}, {"cabron", kPrefix}, {"pendejo", kPrefix}, {"pendeja", kPrefix},
    {"chinga", kInside}, {"verga", kExact}, {"culero", kPrefix}, {"gilipollas", kExact}, {"hijueputa", kExact},
    // Portuguese
    {"porra", kExact}, {"caralho", kInside}, {"merda", kInside}, {"foda", kPrefix}, {"foder", kPrefix},
    {"fodase", kExact}, {"buceta", kPrefix}, {"cacete", kExact}, {"arrombado", kPrefix},
    // French
    {"putain", kExact}, {"merde", kInside}, {"connard", kPrefix}, {"connasse", kPrefix}, {"salope", kPrefix},
    {"enculé", kPrefix}, {"encule", kPrefix}, {"nique", kPrefix}, {"bordel", kExact},
    // German
    {"scheiße", kInside}, {"scheisse", kInside}, {"arschloch", kInside}, {"fick", kPrefix}, {"hure", kPrefix},
    {"wichser", kPrefix}, {"fotze", kPrefix},
    // Italian
    {"cazzo", kInside}, {"stronzo", kPrefix}, {"stronza", kPrefix}, {"vaffanculo", kInside}, {"puttana", kPrefix},
    {"troia", kExact}, {"minchia", kExact},
    // Indonesian
    {"anjing", kExact}, {"anjir", kExact}, {"bangsat", kExact}, {"kontol", kPrefix}, {"memek", kExact},
    {"ngentot", kPrefix}, {"bajingan", kExact}, {"goblok", kExact}, {"tolol", kExact},
    // Russian (as written: no case folding for Cyrillic, so the lower-case forms)
    {"\xD0\xB1\xD0\xBB\xD1\x8F", kPrefix},                 // бля
    {"\xD1\x81\xD1\x83\xD0\xBA", kPrefix},                 // сук
    {"\xD1\x85\xD1\x83\xD0\xB9", kPrefix},                 // хуй
    {"\xD1\x85\xD1\x83\xD0\xB5", kPrefix},                 // хуе
    {"\xD0\xBF\xD0\xB8\xD0\xB7\xD0\xB4", kInside},         // пизд
    {"\xD0\xB5\xD0\xB1\xD0\xB0", kPrefix},                 // еба
    {"\xD0\xBC\xD1\x83\xD0\xB4\xD0\xB0\xD0\xBA", kPrefix}, // мудак
    {"\xD0\xB3\xD0\xBE\xD0\xB2\xD0\xBD\xD0\xBE", kExact},  // говно
    // Arabic (whole words only: short ones are parts of ordinary words)
    {"\xD9\x83\xD8\xB3", kExact},                          // كس
    {"\xD8\xB4\xD8\xB1\xD9\x85\xD9\x88\xD8\xB7", kPrefix}, // شرموط
    {"\xD9\x85\xD9\x86\xD9\x8A\xD9\x88\xD9\x83", kPrefix}, // منيوك
    {"\xD8\xB2\xD8\xA8", kExact},                          // زب
    // Korean, Chinese (no spaces between words: found anywhere)
    {"\xEC\x94\xA8\xEB\xB0\x9C", kInside},                 // 씨발
    {"\xEC\x8B\x9C\xEB\xB0\x9C", kInside},                 // 시발
    {"\xEB\xB3\x91\xEC\x8B\xA0", kInside},                 // 병신
    {"\xEA\xB0\x9C\xEC\x83\x88\xEB\x81\xBC", kInside},     // 개새끼
    {"\xEC\xA2\x86", kInside},                             // 좆
    {"\xE4\xBB\x96\xE5\xA6\x88\xE7\x9A\x84", kInside},     // 他妈的
    {"\xE5\xA6\x88\xE7\x9A\x84", kInside},                 // 妈的
    {"\xE5\x82\xBB\xE9\x80\xBC", kInside},                 // 傻逼
    {"\xE6\x93\x8D\xE4\xBD\xA0", kInside},                 // 操你
};

bool isSwear(const std::string& lowered) {
    for (const Swear& w : kSwears) {
        const size_t n = std::strlen(w.word);
        if (w.how == kExact && lowered == w.word) return true;
        if (w.how == kPrefix && lowered.compare(0, n, w.word) == 0) return true;
        if (w.how == kInside && lowered.find(w.word) != std::string::npos) return true;
    }
    return false;
}

// Every swear in the text, as byte spans (words, or the swear inside a run of CJK text).
std::vector<Span> swearSpans(const std::string& s) {
    std::vector<Span> out;
    for (const Span& w : words(s)) {
        const std::string low = lowerAscii(s.substr(w.b, w.e - w.b));
        if (!isSwear(low)) continue;
        // CJK runs have no spaces: mark just the swear itself, not the whole sentence.
        bool inner = false;
        for (const Swear& sw : kSwears) {
            if (static_cast<unsigned char>(sw.word[0]) < 0xE0 || sw.how != kInside) continue;
            for (size_t at = low.find(sw.word); at != std::string::npos; at = low.find(sw.word, at + 1)) {
                out.push_back({w.b + at, w.b + at + std::strlen(sw.word)});
                inner = true;
            }
        }
        if (!inner) out.push_back(w);
    }
    std::sort(out.begin(), out.end(), [](const Span& a, const Span& b) { return a.b < b.b; });
    return out;
}

size_t utf8Len(unsigned char c) { return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4; }

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

// ---------------------------------------------------------------- word list

std::string applyWordFixes(const std::string& text, const std::vector<WordFix>& list) {
    std::string out = text;
    for (const WordFix& f : list) {
        const std::string from = f.heard.empty() ? f.write : f.heard;
        if (from.empty() || f.write.empty()) continue;
        const std::string lowFrom = lowerAscii(from);
        std::string low = lowerAscii(out);
        for (size_t at = low.find(lowFrom); at != std::string::npos; at = low.find(lowFrom, at + f.write.size())) {
            const bool startOk = at == 0 || !isWordByte(static_cast<unsigned char>(out[at - 1]));
            const size_t end = at + from.size();
            const bool endOk = end >= out.size() || !isWordByte(static_cast<unsigned char>(out[end]));
            if (!startOk || !endOk) {
                low = lowerAscii(out);
                if (at + 1 >= low.size()) break;
                continue;
            }
            out.replace(at, from.size(), f.write);
            low = lowerAscii(out);
        }
    }
    return out;
}

std::string wordListNote(const std::vector<WordFix>& list) {
    std::string names;
    for (const WordFix& f : list) {
        if (f.write.empty()) continue;
        if (!names.empty()) names += ", ";
        names += f.write;
    }
    if (names.empty()) return {};
    return "- Keep these names and words exactly as written (don't translate them): " + names + ".\n";
}

// ---------------------------------------------------------------- swearing

bool hasSwear(const std::string& text) { return !swearSpans(text).empty(); }

std::string maskSwears(const std::string& text) {
    std::string out;
    size_t pos = 0;
    for (const Span& w : swearSpans(text)) {
        if (w.b < pos) continue;
        out.append(text, pos, w.b - pos);
        const size_t first = utf8Len(static_cast<unsigned char>(text[w.b]));
        out.append(text, w.b, first);
        for (size_t i = w.b + first; i < w.e; i += utf8Len(static_cast<unsigned char>(text[i]))) out += '*';
        pos = w.e;
    }
    out.append(text, pos, std::string::npos);
    return out;
}

std::string replaceSwears(const std::string& text, const std::string& with) {
    std::string out;
    size_t pos = 0;
    for (const Span& w : swearSpans(text)) {
        if (w.b < pos) continue;
        out.append(text, pos, w.b - pos);
        if (with.empty()) {  // drop the word and one space next to it
            while (!out.empty() && out.back() == ' ') out.pop_back();
            if (!out.empty()) out += ' ';
            pos = w.e;
            while (pos < text.size() && text[pos] == ' ') ++pos;
            continue;
        }
        out += with;
        pos = w.e;
    }
    out.append(text, pos, std::string::npos);
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

std::vector<std::string> swearWords(const std::string& text) {
    std::vector<std::string> out;
    for (const Span& w : swearSpans(text)) out.push_back(text.substr(w.b, w.e - w.b));
    return out;
}

std::string markSwears(const std::string& text, std::vector<std::string>& found) {
    found.clear();
    std::string out;
    size_t pos = 0;
    for (const Span& w : swearSpans(text)) {
        if (w.b < pos) continue;
        out.append(text, pos, w.b - pos);
        found.push_back(text.substr(w.b, w.e - w.b));
        out += "[[" + std::to_string(found.size()) + "]]";
        pos = w.e;
    }
    out.append(text, pos, std::string::npos);
    return out;
}

// ---------------------------------------------------------------- typed lines

void countSentLine(FeatureSettings& f, const std::string& raw) {
    std::string line = raw;
    while (!line.empty() && (line.back() == ' ' || line.back() == '\n')) line.pop_back();
    if (line.empty() || line.size() > 200) return;
    ++f.sentCounts[line];
    while (f.sentCounts.size() > 40) {  // forget the least used
        auto least = f.sentCounts.begin();
        for (auto it = f.sentCounts.begin(); it != f.sentCounts.end(); ++it)
            if (it->second < least->second) least = it;
        f.sentCounts.erase(least);
    }
}

std::vector<std::string> commonLines(const FeatureSettings& f, size_t n) {
    std::vector<std::pair<int, std::string>> v;
    for (const auto& kv : f.sentCounts)
        if (kv.second >= 2 && std::find(f.quickPhrases.begin(), f.quickPhrases.end(), kv.first) == f.quickPhrases.end())
            v.push_back({kv.second, kv.first});
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < v.size() && i < n; ++i) out.push_back(v[i].second);
    return out;
}

// ---------------------------------------------------------------- setup codes

std::string setupToCode(const std::string& in) {
    std::string out = "CIMA1:";
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : in) {
        buf = (buf << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += kB64[(buf >> bits) & 0x3F];
        }
    }
    if (bits > 0) out += kB64[(buf << (6 - bits)) & 0x3F];
    while ((out.size() - 6) % 4) out += '=';
    return out;
}

bool codeToSetup(const std::string& raw, std::string& out) {
    std::string code;
    for (char c : raw)
        if (!std::isspace(static_cast<unsigned char>(c))) code += c;
    const size_t at = code.find("CIMA1:");
    if (at == std::string::npos) return false;
    out.clear();
    uint32_t buf = 0;
    int bits = 0;
    for (size_t i = at + 6; i < code.size(); ++i) {
        const char* p = std::strchr(kB64, code[i]);
        if (code[i] == '=' || !p || !*p) break;
        buf = (buf << 6) | uint32_t(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((buf >> bits) & 0xFF);
        }
    }
    return !out.empty() && out.front() == '{';
}
