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

std::string markListed(const std::string& text, const std::vector<std::string>& list, std::vector<std::string>& found) {
    found.clear();
    std::vector<Span> spans;
    const std::string low = lowerAscii(text);
    for (const std::string& raw : list) {
        std::string item = raw;
        while (!item.empty() && item.back() == ' ') item.pop_back();
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        if (item.empty()) continue;
        const std::string want = lowerAscii(item);
        const bool spaceless = static_cast<unsigned char>(want[0]) >= 0xE0;  // CJK: no spaces between words
        for (size_t at = low.find(want); at != std::string::npos; at = low.find(want, at + 1)) {
            const size_t end = at + want.size();
            const bool startOk = spaceless || at == 0 || !isWordByte(static_cast<unsigned char>(text[at - 1]));
            const bool endOk = spaceless || end >= text.size() || !isWordByte(static_cast<unsigned char>(text[end]));
            if (startOk && endOk) spans.push_back({at, end});
        }
    }
    // Longest first where two overlap ("son of a" and "son"), then in reading order.
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.b != b.b ? a.b < b.b : a.e > b.e; });
    std::string out;
    size_t pos = 0;
    for (const Span& w : spans) {
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

// ---------------------------------------------------------------- sentence-aware phrases

namespace {

// Decodes the last UTF-8 code point before `end` (and moves `end` to its start).
uint32_t prevCodepoint(const std::string& t, size_t& end) {
    size_t i = end;
    if (i == 0) return 0;
    do { --i; } while (i > 0 && (static_cast<unsigned char>(t[i]) & 0xC0) == 0x80);
    const unsigned char c = static_cast<unsigned char>(t[i]);
    uint32_t cp = c;
    int more = 0;
    if (c >= 0xF0) { cp = c & 0x07; more = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; more = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; more = 1; }
    for (int k = 1; k <= more && i + k < t.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(t[i + k]) & 0x3F);
    end = i;
    return cp;
}

bool isCjk(uint32_t cp) {
    return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0xF900 && cp <= 0xFAFF);
}

// Lower-case for ASCII, Latin-1 and Cyrillic capitals: enough to compare one word with a list.
std::string lowerWord(const std::string& w) {
    std::string out;
    for (size_t i = 0; i < w.size();) {
        const unsigned char c = static_cast<unsigned char>(w[i]);
        if (c < 0x80) { out += char(std::tolower(c)); ++i; continue; }
        size_t j = i + 1;
        while (j < w.size() && (static_cast<unsigned char>(w[j]) & 0xC0) == 0x80) ++j;
        std::string ch = w.substr(i, j - i);
        if (ch.size() == 2) {
            const unsigned a = static_cast<unsigned char>(ch[0]), b = static_cast<unsigned char>(ch[1]);
            const uint32_t cp = ((a & 0x1F) << 6) | (b & 0x3F);
            uint32_t lo = cp;
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) lo = cp + 0x20;               // À..Þ
            else if (cp >= 0x410 && cp <= 0x42F) lo = cp + 0x20;                      // А..Я
            else if (cp >= 0x400 && cp <= 0x40F) lo = cp + 0x50;                      // Ѐ..Џ
            if (lo != cp) { ch.clear(); ch += char(0xC0 | (lo >> 6)); ch += char(0x80 | (lo & 0x3F)); }
        }
        out += ch;
        i = j;
    }
    return out;
}

// Words a sentence doesn't end on, by language: "and", "because", "the", "my"... A transcript that
// stops on one of these was cut off by a pause, even when the service put a "." after it.
const std::vector<std::string>& openEnders(const std::string& lang) {
    static const std::vector<std::string> none;
    // Only words a sentence practically never ends on: "that", "to", "her" or "so" can end one ("I
    // know that.", "I want to.", "I saw her.", "I think so."), so they're not here.
    static const std::vector<std::pair<const char*, std::vector<std::string>>> lists = {
        {"en", {"and", "but", "or", "because", "cause", "cuz", "the", "an", "my", "your", "their", "our", "its", "if",
                "than", "which", "gonna", "wanna", "um", "uh", "umm", "uhh", "erm"}},
        {"es", {"y", "o", "pero", "porque", "de", "del", "el", "los", "las", "un", "con", "para", "mi", "su", "cuando",
                "e", "ni", "al", "eh"}},
        {"pt", {"e", "ou", "mas", "porque", "de", "do", "da", "os", "com", "para", "pra", "em", "no", "na"}},
        {"fr", {"et", "ou", "mais", "parce", "de", "du", "des", "les", "pour", "mon", "ma", "mes", "euh", "je", "j'"}},
        {"de", {"und", "oder", "aber", "weil", "dass", "ein", "für", "wenn", "äh", "ähm"}},
        {"it", {"e", "o", "ma", "di", "del", "della", "il", "gli", "un", "con", "per", "ehm"}},
        {"nl", {"en", "of", "maar", "omdat", "de", "een", "als"}},
        {"id", {"dan", "atau", "tapi", "karena", "yang", "di", "ke", "dari", "untuk", "dengan", "kalau"}},
        {"ms", {"dan", "atau", "tapi", "kerana", "sebab", "yang", "di", "ke", "dari", "untuk", "dengan", "kalau"}},
        {"fil", {"at", "pero", "ng", "sa", "ang", "mga", "para", "kung", "pag", "si", "ni"}},
        {"tl", {"at", "pero", "ng", "sa", "ang", "mga", "para", "kung", "pag", "si", "ni"}},
        {"ru", {"и", "или", "но", "потому", "чтобы", "в", "с", "к", "по", "для", "если", "ну"}},
        {"uk", {"і", "й", "або", "але", "тому", "щоб", "в", "у", "з", "до", "для", "якщо"}},
        {"pl", {"i", "lub", "albo", "ale", "bo", "że", "żeby", "w", "na", "z", "do", "dla", "jeśli"}},
        {"tr", {"ve", "veya", "ama", "çünkü", "ile", "eğer"}},
        {"ar", {"و", "او", "أو", "لكن", "لكنّ", "لأن", "لان", "عشان", "علشان", "إن", "ان", "أن", "اللي", "الذي", "التي",
                "في", "على", "إلى", "الى", "لما", "إذا", "اذا", "لو"}},
        {"hi", {"और", "या", "लेकिन", "क्योंकि", "कि", "अगर", "जब"}},
        {"vi", {"và", "hoặc", "nhưng", "vì", "của", "với", "nếu", "khi", "thì", "là"}},
        {"ko", {"그리고", "그래서", "근데", "그런데", "하지만", "왜냐면", "만약"}},
        {"zh", {"和", "跟", "但是", "可是", "因为", "所以", "如果", "然后", "而且", "还有"}},
        {"ja", {"そして", "でも", "けど", "それで", "あの", "えっと"}},
    };
    std::string code = lang;
    if (code.size() > 2 && code != "fil") {  // three-letter codes from the speech-to-text service
        static const std::pair<const char*, const char*> three[] = {
            {"eng", "en"}, {"spa", "es"}, {"por", "pt"}, {"fra", "fr"}, {"fre", "fr"}, {"deu", "de"}, {"ger", "de"},
            {"ita", "it"}, {"nld", "nl"}, {"dut", "nl"}, {"ind", "id"}, {"msa", "ms"}, {"may", "ms"}, {"tgl", "tl"},
            {"rus", "ru"}, {"ukr", "uk"}, {"pol", "pl"}, {"tur", "tr"}, {"ara", "ar"}, {"hin", "hi"}, {"vie", "vi"},
            {"kor", "ko"}, {"zho", "zh"}, {"chi", "zh"}, {"cmn", "zh"}, {"yue", "zh"}, {"jpn", "ja"}};
        for (const auto& t : three)
            if (code == t.first) { code = t.second; break; }
    }
    if (code.size() > 2 && code.compare(0, 3, "fil") != 0) code = code.substr(0, 2);  // "pt-BR" -> "pt"
    for (const auto& l : lists)
        if (code == l.first) return l.second;
    return none;
}

}  // namespace

std::string guessLanguage(const std::string& text, bool* sure) {
    if (sure) *sure = false;
    // Count letters by script.
    int latin = 0, cyr = 0, ukr = 0, hangul = 0, kana = 0, han = 0, arabic = 0, persian = 0, hebrew = 0, thai = 0,
        deva = 0, greek = 0, bengali = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = text[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (i + len > text.size()) break;
        unsigned cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        i += len;
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0x24F) || (cp >= 0x1E00 && cp <= 0x1EFF)) ++latin;
        else if (cp >= 0x400 && cp <= 0x4FF) {
            ++cyr;
            if (cp == 0x456 || cp == 0x457 || cp == 0x454 || cp == 0x491 || cp == 0x406 || cp == 0x407 || cp == 0x404 || cp == 0x490) ++ukr;
        } else if (cp >= 0xAC00 && cp <= 0xD7AF) ++hangul;
        else if (cp >= 0x3040 && cp <= 0x30FF) ++kana;
        else if (cp >= 0x4E00 && cp <= 0x9FFF) ++han;
        else if (cp >= 0x600 && cp <= 0x6FF) {
            ++arabic;
            if (cp == 0x67E || cp == 0x686 || cp == 0x698 || cp == 0x6AF || cp == 0x6CC) ++persian;
        } else if (cp >= 0x590 && cp <= 0x5FF) ++hebrew;
        else if (cp >= 0xE00 && cp <= 0xE7F) ++thai;
        else if (cp >= 0x900 && cp <= 0x97F) ++deva;
        else if (cp >= 0x370 && cp <= 0x3FF) ++greek;
        else if (cp >= 0x980 && cp <= 0x9FF) ++bengali;
    }
    if (kana) {  // Japanese mixes kanji in; any kana means Japanese
        if (sure) *sure = true;
        return "ja";
    }
    const std::pair<int, const char*> scripts[] = {{hangul, "ko"}, {han, "zh"}, {cyr, ukr ? "uk" : "ru"},
                                                   {arabic, persian ? "fa" : "ar"}, {hebrew, "he"}, {thai, "th"},
                                                   {deva, "hi"}, {greek, "el"}, {bengali, "bn"}};
    const std::pair<int, const char*>* best = nullptr;
    for (const auto& sc : scripts)
        if (sc.first > 0 && (!best || sc.first > best->first)) best = &sc;
    if (best && best->first >= latin) {
        if (sure) *sure = best->first >= 2;
        return best->second;
    }
    if (!latin) return {};
    // Latin script: the language whose everyday little words show up most.
    static const std::pair<const char*, std::vector<std::string>> kWords[] = {
        {"en", {"the", "and", "you", "is", "are", "i", "it", "to", "of", "that", "what", "this", "my", "we", "do", "don't", "i'm", "was", "have", "not", "with"}},
        {"es", {"el", "la", "que", "de", "y", "es", "no", "un", "una", "por", "con", "para", "qué", "yo", "lo", "pero", "está", "como", "muy", "eso"}},
        {"pt", {"o", "a", "que", "de", "não", "é", "um", "uma", "eu", "você", "com", "para", "isso", "tá", "muito", "mas", "como", "aqui", "vamos", "tudo"}},
        {"fr", {"le", "la", "les", "et", "est", "je", "tu", "pas", "que", "un", "une", "c'est", "de", "des", "on", "vous", "ça", "mais", "il", "avec",
                "sa", "son", "ses", "mon", "ma", "pour", "dans", "qui", "au", "du", "ne", "plus", "j'ai", "suis", "oui", "non", "quoi"}},
        {"de", {"der", "die", "das", "und", "ist", "ich", "du", "nicht", "ein", "eine", "was", "mit", "auf", "es", "wir", "auch", "aber", "sie", "ja", "zu"}},
        {"it", {"il", "la", "che", "di", "e", "è", "non", "un", "una", "io", "tu", "per", "con", "sono", "ma", "come", "questo", "anche", "lo", "cosa"}},
        {"nl", {"de", "het", "een", "en", "is", "ik", "je", "niet", "dat", "wat", "van", "op", "met", "maar", "ook", "jij", "zijn", "we", "hoe", "nee"}},
        {"fil", {"ang", "ng", "sa", "mga", "ko", "mo", "ako", "ikaw", "ka", "na", "hindi", "ba", "naman", "lang", "po", "kasi", "yung", "siya", "ito", "talaga"}},
        {"id", {"yang", "dan", "di", "ini", "itu", "aku", "kamu", "tidak", "nggak", "gak", "ada", "apa", "saya", "dengan", "untuk", "udah", "sudah", "juga", "banget", "aja"}},
        {"pl", {"i", "w", "nie", "to", "się", "jest", "że", "na", "co", "jak", "ale", "tak", "ja", "ty", "mi", "z", "do", "czy", "już", "jestem"}},
        {"tr", {"bir", "ve", "bu", "ne", "da", "de", "ben", "sen", "çok", "var", "yok", "mi", "mı", "için", "ama", "değil", "evet", "hayır", "nasıl", "şey"}},
        {"vi", {"tôi", "bạn", "là", "không", "có", "và", "của", "được", "này", "một", "những", "cái", "đi", "rồi", "với", "người", "gì", "thì", "mình", "nhé"}},
        {"sv", {"och", "att", "det", "är", "jag", "du", "inte", "en", "som", "på", "med", "har", "vad", "för", "men", "så", "vi", "den", "hej", "nej"}},
    };
    // Lowercase ASCII words (accented letters stay as they are, which is what the lists hold).
    std::vector<std::string> words;
    std::string w;
    auto flush = [&] { if (!w.empty()) { words.push_back(w); w.clear(); } };
    for (unsigned char c : text) {
        if (c >= 'A' && c <= 'Z') w += char(c + 32);
        else if ((c >= 'a' && c <= 'z') || c >= 0x80 || c == '\'') w += char(c);
        else flush();
    }
    flush();
    const char* pick = nullptr;
    int top = 0, second = 0;
    for (const auto& l : kWords) {
        int n = 0;
        for (const auto& word : words)
            for (const auto& x : l.second)
                if (word == x) { ++n; break; }
        if (n > top) { second = top; top = n; pick = l.first; }
        else if (n > second) second = n;
    }
    if (sure) *sure = top >= 2 && top >= second * 2;
    return pick ? pick : "en";  // nothing matched: English is the likeliest guess in a voice chat
}

int roughWords(const std::string& text) {
    int words = 0, cjk = 0;
    bool in = false;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        size_t end = std::min(text.size(), i + n);
        uint32_t cp = 0;
        { size_t e = end; cp = prevCodepoint(text, e); }
        if (isCjk(cp)) { ++cjk; in = false; }
        else if (c == ' ' || c == '\t' || c == '\n') in = false;
        else if (!in) { ++words; in = true; }
        i = end;
    }
    return words + (cjk + 1) / 2;
}

bool sentenceUnfinished(const std::string& text, const std::string& lang) {
    size_t end = text.size();
    // Closing quotes and brackets don't count; spaces neither.
    for (;;) {
        size_t at = end;
        const uint32_t cp = prevCodepoint(text, at);
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '"' || cp == '\'' || cp == ')' || cp == ']' || cp == 0x201D ||
            cp == 0x2019 || cp == 0x300D || cp == 0x300F || cp == 0xFF09) {
            end = at;
            continue;
        }
        break;
    }
    if (end == 0) return false;
    size_t at = end;
    const uint32_t last = prevCodepoint(text, at);
    // Trailing off ("so...", "and then…"), or a comma, dash or colon: more is coming.
    if (last == 0x2026 || (last == '.' && end >= 2 && text[end - 2] == '.')) return true;
    if (last == ',' || last == 0xFF0C || last == 0x3001 || last == 0x060C || last == '-' || last == 0x2013 ||
        last == 0x2014 || last == ':' || last == ';' || last == 0xFF1A)
        return true;
    // Questions and exclamations are finished.
    if (last == '?' || last == '!' || last == 0xFF1F || last == 0xFF01 || last == 0x061F) return false;
    const bool closed = last == '.' || last == 0x3002 || last == 0x0964 || last == 0xFF0E;

    // The last word, without its punctuation.
    size_t wordEnd = closed ? at : end;
    size_t wordStart = wordEnd;
    bool cjkTail = false;
    for (;;) {
        size_t p = wordStart;
        const uint32_t cp = prevCodepoint(text, p);
        if (wordStart == 0 || cp == ' ' || cp == '\t' || cp == '\n' || cp == ',' || cp == '"' || cp == '(') break;
        if (isCjk(cp)) cjkTail = true;
        wordStart = p;
    }
    const std::string word = lowerWord(text.substr(wordStart, wordEnd - wordStart));
    for (const std::string& w : openEnders(lang)) {
        if (cjkTail) {
            // Chinese and Japanese: the text ends with that word (no spaces to find the last word by).
            if (word.size() >= w.size() && word.compare(word.size() - w.size(), w.size(), w) == 0) return true;
        } else if (word == w) {
            return true;
        }
    }
    if (closed) return false;
    // No closing punctuation at all. Japanese and Korean pieces that end on a joining form (て, けど,
    // から...; 고, 는데, 지만...) are mid-sentence; otherwise a longer stretch with nothing to close it
    // was most likely cut off, and a few words ("okay", "yeah sure") are a whole reply.
    static const char* const kJa[] = {"て", "で", "が", "けど", "けれど", "から", "し", "と", "を", "に", "は", "ので", "のに", "たら", "ば", "ながら"};
    static const char* const kKo[] = {"고", "서", "는데", "은데", "지만", "면", "니까", "랑", "하고", "를", "을", "에서", "에"};
    const std::string base = lang.substr(0, 2);
    if (base == "ja" || lang == "jpn")
        for (const char* e : kJa) {
            const size_t n = std::strlen(e);
            if (word.size() >= n && word.compare(word.size() - n, n, e) == 0) return true;
        }
    if (base == "ko" || lang == "kor")
        for (const char* e : kKo) {
            const size_t n = std::strlen(e);
            if (word.size() >= n && word.compare(word.size() - n, n, e) == 0) return true;
        }
    return roughWords(text) >= 4;
}

size_t lastSentenceEnd(const std::string& text) {
    static const char* const kAbbrev[] = {"mr", "mrs", "ms", "dr", "st", "vs", "etc", "jr", "sr", "e.g", "i.e", "sra", "sr", "dra"};
    size_t best = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t next = std::string::npos;
        if ((c == '.' || c == '?' || c == '!') && i + 1 < text.size() && text[i + 1] == ' ') {
            if (c == '.') {
                if (i > 0 && text[i - 1] == '.') continue;  // "..." trails off, it doesn't end anything
                size_t w = i;
                while (w > 0 && text[w - 1] != ' ') --w;
                const std::string word = lowerWord(text.substr(w, i - w));
                bool abbrev = false;
                for (const char* a : kAbbrev) abbrev = abbrev || word == a;
                if (abbrev || (word.size() == 1 && std::isupper(static_cast<unsigned char>(text[w])))) continue;  // "J. Smith"
            }
            next = i + 1;
            while (next < text.size() && text[next] == ' ') ++next;
        } else if (c >= 0xE0) {
            size_t e = std::min(text.size(), i + (c >= 0xF0 ? 4 : 3));
            size_t at = e;
            const uint32_t cp = prevCodepoint(text, at);
            if (cp == 0x3002 || cp == 0xFF1F || cp == 0xFF01 || cp == 0x061F || cp == 0x0964) {
                next = e;
                while (next < text.size() && text[next] == ' ') ++next;
            }
        } else if (c == 0xD8 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x9F && i + 2 < text.size() &&
                   text[i + 2] == ' ') {  // Arabic question mark ؟
            next = i + 3;
        }
        if (next != std::string::npos && next < text.size()) best = next;
    }
    return best;
}

std::string joinHeard(const std::string& first, const std::string& rest, const std::string& lang) {
    std::string a = first;
    while (!a.empty() && (a.back() == ' ' || a.back() == '\n')) a.pop_back();
    // The "." (or "。") the transcript closed the first piece with: the sentence goes on.
    if (a.size() >= 3 && a.compare(a.size() - 3, 3, "\xE3\x80\x82") == 0) a.erase(a.size() - 3);
    else if (!a.empty() && a.back() == '.' && !(a.size() >= 2 && a[a.size() - 2] == '.')) a.pop_back();
    std::string b = rest;
    while (!b.empty() && b.front() == ' ') b.erase(b.begin());
    if (a.empty()) return b;
    if (b.empty()) return a;
    // Chinese and Japanese are written without spaces between words.
    size_t at = a.size();
    const uint32_t lastA = prevCodepoint(a, at);
    const std::string base = lang.substr(0, 2);
    const bool cjkMark = (lastA >= 0x3000 && lastA <= 0x303F) || (lastA >= 0xFF00 && lastA <= 0xFFEF);  // 、。，
    const bool noSpace = (isCjk(lastA) || cjkMark) && (base == "zh" || base == "ja" || lang == "jpn" || lang == "zho" || lang == "cmn" ||
                                          lang == "yue" || lang.empty());
    // "Then" after a cut is the same sentence going on: lower-case its first letter (not "I", and
    // not a word in capitals like a name or "OK").
    if (b.size() >= 2 && std::isupper(static_cast<unsigned char>(b[0])) && std::islower(static_cast<unsigned char>(b[1])) &&
        a.back() != '.' && a.back() != '?' && a.back() != '!') {
        static const char* const kKeep[] = {"I", "I'm", "I'll", "I've", "I'd"};
        size_t sp = b.find_first_of(" ,.!?");
        const std::string firstWord = b.substr(0, sp);
        bool keep = false;
        for (const char* k : kKeep) keep = keep || firstWord == k;
        static const char* const kJoin[] = {"And", "But", "Or", "So", "Because", "Then", "That", "Which", "Who", "When",
                                            "Y", "Pero", "Porque", "Que", "Und", "Aber", "Weil", "Et", "Mais", "Parce", "E",
                                            "Ma", "Mas", "En", "Dan", "Tapi", "At", "Kasi", "Tapos", "The", "A", "To", "With",
                                            "Like", "Um", "Uh", "If", "Than"};
        bool joiner = false;
        for (const char* j : kJoin) joiner = joiner || firstWord == j;
        if (!keep && joiner) b[0] = char(std::tolower(static_cast<unsigned char>(b[0])));
    }
    return noSpace ? a + b : a + " " + b;
}

// A caption's words cut into sentences: at . ! ? … and their Chinese, Japanese, Arabic and Hindi
// forms, when a space, the end or (for the wide ones) anything follows. "3.5" and "e.g." inside a
// sentence don't end it. `open` says the last piece has no end mark yet.
std::vector<std::string> sentencesOf(const std::string& text, bool& open) {
    static const char* const kWide[] = {"\xE3\x80\x82", "\xEF\xBC\x81", "\xEF\xBC\x9F", "\xE2\x80\xA6", "\xD8\x9F", "\xE0\xA5\xA4"};
    std::vector<std::string> out;
    std::string cur;
    auto push = [&] {
        size_t a = cur.find_first_not_of(" \t\r\n");
        if (a != std::string::npos) out.push_back(cur.substr(a, cur.find_last_not_of(" \t\r\n") - a + 1));
        cur.clear();
    };
    open = false;
    for (size_t i = 0; i < text.size();) {
        bool wide = false;
        for (const char* w : kWide) {
            const size_t n = std::strlen(w);
            if (text.compare(i, n, w) == 0) {
                cur.append(text, i, n);
                i += n;
                wide = true;
                break;
            }
        }
        if (wide) {
            const bool dots = i >= 3 && text.compare(i - 3, 3, "\xE2\x80\xA6") == 0;  // "…" can sit inside a word
            if (!dots || i >= text.size() || text[i] == ' ') push();
            continue;
        }
        const char ch = text[i++];
        cur += ch;
        if ((ch == '.' || ch == '!' || ch == '?') && (i >= text.size() || text[i] == ' ' || text[i] == '\n')) {
            // not after a single letter or a digit ("e.g. this", "no. 5", "3. then")
            const size_t n = cur.size();
            const bool shortWord = ch == '.' && n >= 2 &&
                                   (std::isdigit((unsigned char)cur[n - 2]) ||
                                    (n >= 3 && (cur[n - 3] == ' ' || cur[n - 3] == '.') && std::isalpha((unsigned char)cur[n - 2])) ||
                                    (n == 2 && std::isalpha((unsigned char)cur[0])));
            if (!shortWord) push();
        }
    }
    const size_t before = out.size();
    push();
    open = out.size() > before;
    return out;
}
