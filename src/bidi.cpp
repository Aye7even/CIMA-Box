#include "bidi.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace bidi {
namespace {

// ---------------------------------------------------------------- UTF-8

size_t decode(const char* s, const char* e, uint32_t& cp) {
    const unsigned char c = static_cast<unsigned char>(*s);
    auto cont = [&](int i) { return s + i < e ? static_cast<unsigned char>(s[i]) & 0x3F : 0; };
    if (c < 0x80) { cp = c; return 1; }
    if ((c >> 5) == 6 && s + 1 < e) { cp = ((c & 0x1F) << 6) | cont(1); return 2; }
    if ((c >> 4) == 14 && s + 2 < e) { cp = ((c & 0x0F) << 12) | (cont(1) << 6) | cont(2); return 3; }
    if ((c >> 3) == 30 && s + 3 < e) { cp = ((c & 0x07) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3); return 4; }
    cp = 0xFFFD;
    return 1;
}

void encode(uint32_t cp, std::string& out) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

std::vector<uint32_t> codepoints(const char* b, const char* e) {
    std::vector<uint32_t> v;
    v.reserve(size_t(e - b));
    for (const char* s = b; s < e;) {
        uint32_t cp;
        s += decode(s, e, cp);
        v.push_back(cp);
    }
    return v;
}

// ---------------------------------------------------------------- shaping

// Joined forms of one letter: isolated, final, initial, medial (0 = this letter has no such form:
// it joins only to the letter before it).
struct Forms { uint32_t letter, iso, fin, ini, med; };
const Forms kForms[] = {
    {0x0621, 0xFE80, 0, 0, 0},            // hamza (doesn't join)
    {0x0622, 0xFE81, 0xFE82, 0, 0},       {0x0623, 0xFE83, 0xFE84, 0, 0},
    {0x0624, 0xFE85, 0xFE86, 0, 0},       {0x0625, 0xFE87, 0xFE88, 0, 0},
    {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C}, {0x0627, 0xFE8D, 0xFE8E, 0, 0},
    {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92}, {0x0629, 0xFE93, 0xFE94, 0, 0},
    {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98}, {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},
    {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0}, {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},
    {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8}, {0x062F, 0xFEA9, 0xFEAA, 0, 0},
    {0x0630, 0xFEAB, 0xFEAC, 0, 0},       {0x0631, 0xFEAD, 0xFEAE, 0, 0},
    {0x0632, 0xFEAF, 0xFEB0, 0, 0},       {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},
    {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8}, {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},
    {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0}, {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},
    {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8}, {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC},
    {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0}, {0x0640, 0x0640, 0x0640, 0x0640, 0x0640},  // tatweel
    {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4}, {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},
    {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC}, {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},
    {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4}, {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},
    {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC}, {0x0648, 0xFEED, 0xFEEE, 0, 0},
    {0x0649, 0xFEEF, 0xFEF0, 0, 0},       {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},
    // Persian, Urdu and others
    {0x0671, 0xFB50, 0xFB51, 0, 0},       {0x0679, 0xFB66, 0xFB67, 0xFB68, 0xFB69},
    {0x067E, 0xFB56, 0xFB57, 0xFB58, 0xFB59}, {0x0686, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D},
    {0x0688, 0xFB88, 0xFB89, 0, 0},       {0x0691, 0xFB8C, 0xFB8D, 0, 0},
    {0x0698, 0xFB8A, 0xFB8B, 0, 0},       {0x06A4, 0xFB6A, 0xFB6B, 0xFB6C, 0xFB6D},
    {0x06A9, 0xFB8E, 0xFB8F, 0xFB90, 0xFB91}, {0x06AF, 0xFB92, 0xFB93, 0xFB94, 0xFB95},
    {0x06BA, 0xFB9E, 0xFB9F, 0, 0},       {0x06BE, 0xFBAA, 0xFBAB, 0xFBAC, 0xFBAD},
    {0x06C1, 0xFBA6, 0xFBA7, 0xFBA8, 0xFBA9}, {0x06CC, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF},
    {0x06D2, 0xFBAE, 0xFBAF, 0, 0},
};

const Forms* formsOf(uint32_t cp) {
    if (cp < 0x0621 || cp > 0x06D2) return nullptr;
    for (const Forms& f : kForms)
        if (f.letter == cp) return &f;
    return nullptr;
}

// Vowel marks and other marks that sit on a letter: left out of what's drawn.
bool isMark(uint32_t cp) {
    return (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED) || (cp >= 0x0591 && cp <= 0x05C7);
}

bool joinsForward(const Forms* f) { return f && f->ini != 0; }        // can connect to the letter after it
bool joinsBackward(const Forms* f) { return f && f->fin != 0; }       // can connect to the letter before it

std::vector<uint32_t> shapeCodepoints(const std::vector<uint32_t>& in) {
    std::vector<uint32_t> v;  // without marks
    v.reserve(in.size());
    for (uint32_t cp : in)
        if (!isMark(cp)) v.push_back(cp);
    std::vector<uint32_t> out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const Forms* f = formsOf(v[i]);
        if (!f) { out.push_back(v[i]); continue; }
        const Forms* prev = i > 0 ? formsOf(v[i - 1]) : nullptr;
        const bool fromPrev = joinsForward(prev) && joinsBackward(f);
        // lam + alef: one ligature
        if (v[i] == 0x0644 && i + 1 < v.size()) {
            uint32_t lig = 0;
            switch (v[i + 1]) {
                case 0x0622: lig = 0xFEF5; break;
                case 0x0623: lig = 0xFEF7; break;
                case 0x0625: lig = 0xFEF9; break;
                case 0x0627: lig = 0xFEFB; break;
            }
            if (lig) {
                out.push_back(fromPrev ? lig + 1 : lig);
                ++i;
                continue;
            }
        }
        const Forms* next = i + 1 < v.size() ? formsOf(v[i + 1]) : nullptr;
        const bool toNext = joinsForward(f) && joinsBackward(next);
        uint32_t g = f->iso;
        if (fromPrev && toNext) g = f->med;
        else if (fromPrev) g = f->fin;
        else if (toNext) g = f->ini;
        out.push_back(g ? g : f->iso);
    }
    return out;
}

// ---------------------------------------------------------------- reordering

enum Type : uint8_t { L, R, EN, AN, ES, CS, ET, WS, ON, NSM };

bool isRtlLetter(uint32_t cp) {
    return (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF) ||
           (cp >= 0x10800 && cp <= 0x10FFF) || (cp >= 0x1E800 && cp <= 0x1EFFF);
}

Type typeOf(uint32_t cp) {
    if (cp >= '0' && cp <= '9') return EN;
    if (cp >= 0x06F0 && cp <= 0x06F9) return EN;  // Persian digits
    if ((cp >= 0x0660 && cp <= 0x0669) || cp == 0x066B || cp == 0x066C) return AN;
    if (cp == ' ' || cp == '\t' || cp == 0x3000 || cp == 0x2009 || cp == 0x200A) return WS;
    if (cp == '+' || cp == '-' || cp == 0x2212) return ES;
    if (cp == ',' || cp == '.' || cp == ':' || cp == '/' || cp == 0xA0 || cp == 0x060C) return CS;
    if (cp == '#' || cp == '$' || cp == '%' || cp == 0xB0 || cp == 0xA3 || cp == 0xA5 || cp == 0x20AC || cp == 0x066A) return ET;
    if ((cp >= 0x0300 && cp <= 0x036F) || isMark(cp)) return NSM;
    if (cp == 0x061F || cp == 0x061B) return ON;  // Arabic ? and ;
    if (isRtlLetter(cp)) return R;
    if (cp < 0x80) return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') ? L : ON;
    if (cp >= 0x2000 && cp <= 0x2BFF) return ON;  // punctuation, symbols, arrows
    if (cp == 0xAB || cp == 0xBB || cp == 0xA1 || cp == 0xBF || cp == 0xB7) return ON;
    return L;  // other letters: Latin, Cyrillic, CJK, ...
}

uint32_t mirrored(uint32_t cp) {
    switch (cp) {
        case '(': return ')'; case ')': return '(';
        case '[': return ']'; case ']': return '[';
        case '{': return '}'; case '}': return '{';
        case '<': return '>'; case '>': return '<';
        case 0xAB: return 0xBB; case 0xBB: return 0xAB;
    }
    return cp;
}

bool gPreferRtl = false;

}  // namespace

void setPreferRtl(bool prefer) { gPreferRtl = prefer; }

bool hasRtl(const char* b, const char* e) {
    for (const unsigned char* s = reinterpret_cast<const unsigned char*>(b); s < reinterpret_cast<const unsigned char*>(e); ++s) {
        if (*s < 0xD6) continue;
        if (*s <= 0xDF) return true;                                      // U+0580-07FF
        if (*s == 0xE0 && s + 1 < reinterpret_cast<const unsigned char*>(e) && s[1] >= 0xA2 && s[1] <= 0xA3) return true;  // U+0880-08FF
        if (*s == 0xEF && s + 1 < reinterpret_cast<const unsigned char*>(e) && s[1] >= 0xAC && s[1] <= 0xBB) return true;  // U+FB00-FEFF
    }
    return false;
}

std::string shape(const std::string& logical) {
    if (!hasRtl(logical)) return logical;
    std::string out;
    for (uint32_t cp : shapeCodepoints(codepoints(logical.data(), logical.data() + logical.size()))) encode(cp, out);
    return out;
}

std::string visualLine(const char* b, const char* e, bool* rtlOut) {
    if (rtlOut) *rtlOut = false;
    if (!hasRtl(b, e)) return std::string(b, e);
    const std::vector<uint32_t> v = shapeCodepoints(codepoints(b, e));
    const size_t n = v.size();
    std::vector<Type> t(n);
    for (size_t i = 0; i < n; ++i) t[i] = typeOf(v[i]);
    // Which way the line reads: by words, more right-to-left words than others, and on a tie the
    // first word decides. A caption in English with one Arabic name still reads left to right; an
    // Arabic sentence that mentions "ElevenLabs" or "VRChat" doesn't.
    int wordsL = 0, wordsR = 0;
    Type first = ON;
    for (size_t i = 0; i < n;) {
        if (t[i] != L && t[i] != R) { ++i; continue; }
        const Type w = t[i];
        if (first == ON) first = w;
        (w == L ? wordsL : wordsR)++;
        while (i < n && t[i] == w) ++i;
    }
    const bool rtl = gPreferRtl ? wordsR > 0 : wordsR > wordsL || (wordsR > 0 && wordsR == wordsL && first == R);
    if (rtlOut) *rtlOut = rtl;
    const Type base = rtl ? R : L;

    // W1: a mark takes the type of what's before it.
    for (size_t i = 0; i < n; ++i)
        if (t[i] == NSM) t[i] = i > 0 ? t[i - 1] : base;
    // W2: European digits after Arabic letters are Arabic numbers (they still read left to right).
    {   Type lastStrong = base;
        for (size_t i = 0; i < n; ++i) {
            if (t[i] == L || t[i] == R) lastStrong = t[i];
            else if (t[i] == EN && lastStrong == R) t[i] = AN;
        }
    }
    // W4-W6: 1,000 / 3.5 / 10:30 / +5 / 50% stay part of the number.
    for (size_t i = 1; i + 1 < n; ++i)
        if ((t[i] == CS || t[i] == ES) && t[i - 1] == t[i + 1] && (t[i - 1] == EN || (t[i - 1] == AN && t[i] == CS))) t[i] = t[i - 1];
    for (size_t i = 0; i < n; ++i)
        if (t[i] == ET) {
            size_t j = i;
            while (j < n && t[j] == ET) ++j;
            const bool touches = (i > 0 && t[i - 1] == EN) || (j < n && t[j] == EN);
            for (size_t k = i; k < j; ++k) t[k] = touches ? EN : ON;
            i = j - 1;
        }
    for (size_t i = 0; i < n; ++i)
        if (t[i] == ES || t[i] == CS || t[i] == ET) t[i] = ON;
    // W7: digits after a left-to-right word are part of it.
    {   Type lastStrong = base;
        for (size_t i = 0; i < n; ++i) {
            if (t[i] == L || t[i] == R) lastStrong = t[i];
            else if (t[i] == EN && lastStrong == L) t[i] = L;
        }
    }
    // N1-N2: spaces and punctuation between two runs of the same direction go with them;
    // otherwise they follow the line's direction.
    auto dirOf = [](Type x) { return x == L ? L : (x == R || x == EN || x == AN) ? R : ON; };
    for (size_t i = 0; i < n; ++i) {
        if (t[i] != WS && t[i] != ON) continue;
        size_t j = i;
        while (j < n && (t[j] == WS || t[j] == ON)) ++j;
        const Type before = i == 0 ? base : dirOf(t[i - 1]);
        const Type after = j == n ? base : dirOf(t[j]);
        const Type d = before == after ? before : base;
        for (size_t k = i; k < j; ++k) t[k] = d == L ? L : R;
        // keep the trailing spaces on the line's own side (L1)
        if (j == n)
            for (size_t k = i; k < j; ++k)
                if (dirOf(t[k]) != dirOf(base)) t[k] = base;
        i = j - 1;
    }
    // I1-I2: embedding levels.
    std::vector<uint8_t> level(n);
    const uint8_t b0 = rtl ? 1 : 0;
    uint8_t maxLevel = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t lv = b0;
        if (b0 == 0) lv = t[i] == R ? 1 : (t[i] == EN || t[i] == AN) ? 2 : 0;
        else lv = (t[i] == L || t[i] == EN || t[i] == AN) ? 2 : 1;
        level[i] = lv;
        maxLevel = std::max(maxLevel, lv);
    }
    // L2: reverse every run at or above each level, from the highest down to the lowest odd one.
    std::vector<uint32_t> out = v;
    std::vector<uint8_t> lv = level;
    for (int k = maxLevel; k >= 1; --k)
        for (size_t i = 0; i < n;) {
            if (lv[i] < k) { ++i; continue; }
            size_t j = i;
            while (j < n && lv[j] >= k) ++j;
            std::reverse(out.begin() + long(i), out.begin() + long(j));
            std::reverse(lv.begin() + long(i), lv.begin() + long(j));
            i = j;
        }
    // L4: brackets in right-to-left runs face the other way.
    std::string s;
    s.reserve(size_t(e - b) + 8);
    for (size_t i = 0; i < n; ++i) encode((lv[i] & 1) ? mirrored(out[i]) : out[i], s);
    return s;
}

}  // namespace bidi

// ---------------------------------------------------------------- ImGui hook

#ifndef BIDI_NO_IMGUI
#include <imgui.h>

// Set by installImGuiHook(); used by the patched ImFont::RenderText (cmake/imgui_rtl.inl).
extern bool (*GCimaTextHasRtl)(const char* b, const char* e);
extern void (*GCimaTextVisual)(const char* b, const char* e, ImVector<char>* out, bool* rtl);

namespace bidi {
void installImGuiHook() {
    GCimaTextHasRtl = [](const char* b, const char* e) { return hasRtl(b, e); };
    GCimaTextVisual = [](const char* b, const char* e, ImVector<char>* out, bool* rtl) {
        const std::string s = visualLine(b, e, rtl);
        out->resize(int(s.size()));
        if (!s.empty()) std::copy(s.begin(), s.end(), out->Data);
    };
}
}  // namespace bidi
#endif
