#include "emotion.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace {

constexpr int kRate = 16000;
constexpr int kHop = 160;  // 10 ms
constexpr int kWin = 512;  // 32 ms
const float kNaN = std::numeric_limits<float>::quiet_NaN();

// ---------------------------------------------------------------- small helpers

float median(std::vector<float> v) {
    if (v.empty()) return kNaN;
    const size_t k = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + long(k), v.end());
    return v[k];
}

float percentile(std::vector<float> v, float q) {
    if (v.empty()) return kNaN;
    const size_t k = std::min(v.size() - 1, size_t(q * float(v.size() - 1) + 0.5f));
    std::nth_element(v.begin(), v.begin() + long(k), v.end());
    return v[k];
}

float powerMeanDb(const std::vector<float>& db) {
    if (db.empty()) return -100;
    double p = 0;
    for (float d : db) p += std::pow(10.0, d / 10.0);
    return float(10.0 * std::log10(p / double(db.size())));
}

// UTF-8 code points
std::vector<unsigned> decode(const std::string& s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c, n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (unsigned k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

std::string encode(const std::vector<unsigned>& cps) {
    std::string s;
    for (unsigned cp : cps) {
        if (cp < 0x80) s += char(cp);
        else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
        else { s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 0x3F)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
    }
    return s;
}

bool isLetter(unsigned cp) {
    if (cp < 0x80) return std::isalnum(int(cp)) != 0;
    return cp >= 0xC0 && !(cp >= 0x2000 && cp <= 0x206F) && !(cp >= 0x3000 && cp <= 0x303F) && !(cp >= 0xFF00 && cp <= 0xFF0F);
}

// Scripts with capital letters: Latin and Cyrillic.
bool hasCase(unsigned cp) { return (cp < 0x80 && std::isalpha(int(cp))) || (cp >= 0xC0 && cp <= 0x24F) || (cp >= 0x400 && cp <= 0x4FF); }

unsigned upper(unsigned cp) {
    if (cp < 0x80) return unsigned(std::toupper(int(cp)));
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;    // à..þ
    if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;                // а..я
    if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;                // ѐ..џ
    return cp;
}

bool isVowel(unsigned cp) {
    static const unsigned v[] = {'a', 'e', 'i', 'o', 'u', 'y', 'A', 'E', 'I', 'O', 'U', 'Y',
                                 0x430, 0x435, 0x438, 0x43E, 0x443, 0x44B, 0x44D, 0x44E, 0x44F, 0x451};
    for (unsigned x : v)
        if (x == cp) return true;
    return false;
}

bool isCjk(unsigned cp) { return (cp >= 0x3040 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF); }

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws), b = s.find_last_not_of(ws);
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Splits a word into leading punctuation, the letters, and trailing punctuation.
void splitWord(const std::string& w, std::string& pre, std::vector<unsigned>& core, std::string& post) {
    const std::vector<unsigned> cps = decode(w);
    size_t a = 0, b = cps.size();
    while (a < b && !isLetter(cps[a])) ++a;
    while (b > a && !isLetter(cps[b - 1])) --b;
    pre = encode(std::vector<unsigned>(cps.begin(), cps.begin() + long(a)));
    core.assign(cps.begin() + long(a), cps.begin() + long(b));
    post = encode(std::vector<unsigned>(cps.begin() + long(b), cps.end()));
}

int letterCount(const std::string& w) {
    int n = 0;
    for (unsigned cp : decode(w)) n += isLetter(cp) ? 1 : 0;
    return n;
}

bool endsSentence(const std::string& w) {
    const std::vector<unsigned> c = decode(trim(w));
    if (c.empty()) return false;
    const unsigned last = c.back();
    return last == '.' || last == '!' || last == '?' || last == 0x3002 || last == 0xFF01 || last == 0xFF1F || last == 0x2026;
}

bool endsWithAny(const std::string& s, std::initializer_list<const char*> tails) {
    for (const char* t : tails) {
        const std::string tail(t);
        if (s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0) return true;
    }
    return false;
}

// Sets how a part ends: "?", "!", "," or "..." (full-width for Chinese and Japanese).
std::string setEnding(std::string s, bool question, bool exclaim, const char* otherwise) {
    s = trim(s);
    if (s.empty()) return s;
    if (endsWithAny(s, {"..."}) && !question) return s;  // trailing off: keep it
    const bool asks = endsWithAny(s, {"?", "\xEF\xBC\x9F"});
    if (asks && !question) return s;                     // the words already ask something
    while (!s.empty()) {
        const std::vector<unsigned> c = decode(s);
        const unsigned last = c.back();
        if (last == '.' || last == '!' || last == '?' || last == ',' || last == ';' || last == 0x3002 || last == 0xFF01 ||
            last == 0xFF1F || last == 0x3001 || last == 0xFF0C) {
            if (c.size() > 1) s = encode(std::vector<unsigned>(c.begin(), c.end() - 1));
            else s.clear();
        } else {
            break;
        }
    }
    if (s.empty()) return s;
    const bool cjk = isCjk(decode(s).back());
    if (question) return s + (cjk ? "\xEF\xBC\x9F" : "?");
    if (exclaim) return s + (cjk ? "\xEF\xBC\x81" : "!");
    if (std::string(otherwise) == "...") return s + (cjk ? "\xE2\x80\xA6" : "...");
    if (std::string(otherwise) == ",") return s + (cjk ? "\xE3\x80\x81" : ",");
    return s + (cjk ? "\xE3\x80\x82" : ".");
}

// Pitch of one frame (8 kHz samples), by normalised autocorrelation; NaN if there is no clear pitch.
float framePitch(const float* x, int n) {
    constexpr int sr = kRate / 2, minLag = sr / 400, maxLag = sr / 70;  // 70..400 Hz
    double r[maxLag + 2] = {};
    for (int lag = minLag - 1; lag <= maxLag + 1 && lag < n; ++lag) {
        double num = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) {
            num += double(x[i]) * x[i + lag];
            e1 += double(x[i]) * x[i];
            e2 += double(x[i + lag]) * x[i + lag];
        }
        r[lag] = (e1 > 0 && e2 > 0) ? num / std::sqrt(e1 * e2) : 0;
    }
    int best = 0;
    double bestR = 0.55;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (r[lag] > bestR && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) { bestR = r[lag]; best = lag; }
    if (best == 0) return kNaN;
    if (best / 2 >= minLag && r[best / 2] > bestR * 0.85) best /= 2;  // octave-down error
    const double a = r[best - 1], b = r[best], c = r[best + 1], den = a - 2 * b + c;
    const double shift = std::fabs(den) > 1e-12 ? std::max(-0.5, std::min(0.5, 0.5 * (a - c) / den)) : 0.0;
    return float(12.0 * std::log2(sr / (best + shift) / 100.0));
}

// ---------------------------------------------------------------- per-word and per-part measurements

struct Span {
    float loud = -100, peak = kNaN, mid = kNaN, range = 0, tilt = -100, voiced = 0, seconds = 0, endRise = 0;
    int pitched = 0;
};

Span measure(const VoiceTrack& t, float start, float end) {
    Span s;
    const int n = int(t.db.size());
    const int a = std::max(0, int(start * 100)), b = std::min(n, int(std::ceil(end * 100)));
    std::vector<float> db, pitch, tilt;
    for (int f = a; f < b; ++f) {
        if (t.db[size_t(f)] <= t.floorDb) continue;
        db.push_back(t.db[size_t(f)]);
        tilt.push_back(t.tilt[size_t(f)]);
        if (!std::isnan(t.pitch[size_t(f)])) pitch.push_back(t.pitch[size_t(f)]);
    }
    s.seconds = float(db.size()) / 100.f;
    if (db.empty()) return s;
    s.loud = powerMeanDb(db);
    s.tilt = median(tilt);
    s.pitched = int(pitch.size());
    s.voiced = float(pitch.size()) / float(db.size());
    if (pitch.size() >= 3) {
        s.peak = percentile(pitch, 0.8f);
        s.mid = median(pitch);
        s.range = percentile(pitch, 0.9f) - percentile(pitch, 0.1f);
        const size_t tail = std::max<size_t>(4, std::min<size_t>(15, pitch.size() / 4));
        if (pitch.size() > tail + 4) {
            std::vector<float> endPart(pitch.end() - long(tail), pitch.end()), rest(pitch.begin(), pitch.end() - long(tail));
            s.endRise = median(endPart) - median(rest);
        }
    }
    return s;
}

struct Word {
    size_t token = 0;
    float start = 0, end = 0;
    int letters = 0;
    Span m;
    float loudRes = 0, peakRes = 0, stretch = 1;  // compared with the words around it
    bool stressed = false, drawnOut = false;
};

// Least-squares line y = a + b*x; voices naturally drift down over a sentence, and that drift is
// not emphasis.
void fitLine(const std::vector<float>& x, const std::vector<float>& y, float& a, float& b) {
    a = 0;
    b = 0;
    if (x.size() < 3) {
        for (float v : y) a += v;
        a = y.empty() ? 0 : a / float(y.size());
        return;
    }
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double n = double(x.size());
    for (size_t i = 0; i < x.size(); ++i) { sx += x[i]; sy += y[i]; sxx += x[i] * x[i]; sxy += x[i] * y[i]; }
    const double den = n * sxx - sx * sx;
    b = std::fabs(den) > 1e-9 ? float((n * sxy - sx * sy) / den) : 0.f;
    a = float((sy - b * sx) / n);
}

struct Thresholds {
    float stressScore, stressLoud, stressPitch, stretch;
    float shoutDb, shoutEffort, excitedPitch, softDb, flatRange, rise;
};
Thresholds thresholds(int strength) {
    if (strength == kEmotionStrong) return {1.6f, 2.0f, 1.5f, 1.9f, 6.f, 2.0f, 2.0f, -4.f, 3.0f, 2.0f};
    return {2.2f, 3.0f, 2.0f, 2.2f, 8.f, 2.5f, 2.5f, -6.f, 2.5f, 3.0f};
}

std::string twoLetterOk(const std::string& lower) {
    static const char* ok[] = {"no", "so", "oh", "go", "me", "my", "we", "he", "ok", "hi", "be", "do", "to", "up"};
    for (const char* w : ok)
        if (lower == w) return lower;
    return std::string();
}

}  // namespace

// ---------------------------------------------------------------- public

VoiceTrack trackVoice(const std::vector<int16_t>& pcm) {
    VoiceTrack t;
    if (pcm.size() < size_t(kWin)) return t;
    std::vector<float> x(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) x[i] = pcm[i] / 32768.f;
    // 8 kHz copy for pitch: a quarter of the work, and voices stay well below 4 kHz.
    std::vector<float> half(x.size() / 2);
    for (size_t i = 0; i + 1 < x.size(); i += 2) half[i / 2] = 0.5f * (x[i] + x[i + 1]);

    const int frames = int((x.size() - size_t(kWin)) / size_t(kHop)) + 1;
    t.db.resize(size_t(frames));
    t.tilt.resize(size_t(frames));
    t.pitch.assign(size_t(frames), kNaN);
    for (int f = 0; f < frames; ++f) {
        const float* w = &x[size_t(f) * kHop];
        double e = 0, d = 0;
        for (int i = 0; i < kWin; ++i) {
            e += double(w[i]) * w[i];
            if (i > 0) { const double diff = w[i] - 0.95 * w[i - 1]; d += diff * diff; }
        }
        t.db[size_t(f)] = float(10.0 * std::log10(e / kWin + 1e-12));
        t.tilt[size_t(f)] = float(10.0 * std::log10((d + 1e-12) / (e + 1e-12)));
        t.topDb = std::max(t.topDb, t.db[size_t(f)]);
    }
    t.floorDb = std::max(-60.f, t.topDb - 30.f);
    for (int f = 0; f < frames; ++f) {
        if (t.db[size_t(f)] < t.topDb - 25.f) continue;
        const size_t at = size_t(f) * kHop / 2;
        if (at + kWin / 2 > half.size()) break;
        t.pitch[size_t(f)] = framePitch(&half[at], kWin / 2);
    }
    // Drop isolated pitch frames (clicks, breath noise).
    for (int f = 1; f + 1 < frames; ++f)
        if (!std::isnan(t.pitch[size_t(f)]) && std::isnan(t.pitch[size_t(f - 1)]) && std::isnan(t.pitch[size_t(f + 1)]))
            t.pitch[size_t(f)] = kNaN;
    return t;
}

int guessVoiceGender(const std::vector<int16_t>& pcm16k, float startS, float endS, float* medianHz) {
    if (medianHz) *medianHz = 0;
    const size_t n = pcm16k.size();
    size_t a = startS > 0.1f ? size_t((startS - 0.1f) * kRate) : 0;
    size_t b = endS > 0 ? size_t((endS + 0.1f) * kRate) : n;
    a = std::min(a, n);
    b = std::min(std::max(b, a), n);
    if (b - a < size_t(kRate) / 4) return 0;
    const std::vector<int16_t> part(pcm16k.begin() + long(a), pcm16k.begin() + long(b));
    const VoiceTrack t = trackVoice(part);
    std::vector<float> voiced;
    for (float p : t.pitch)
        if (!std::isnan(p)) voiced.push_back(p);
    if (voiced.size() < 15) return 0;  // under ~0.15 s of clear voice
    std::nth_element(voiced.begin(), voiced.begin() + long(voiced.size() / 2), voiced.end());
    const float hz = 100.f * std::pow(2.f, voiced[voiced.size() / 2] / 12.f);
    if (medianHz) *medianHz = hz;
    if (hz < 150.f) return 1;  // kGenderMan
    if (hz > 178.f) return 2;  // kGenderWoman
    return 0;
}

Direction directSpeech(const VoiceTrack& track, const std::vector<HeardToken>& tokens, VoiceBaseline& base, int strength,
                       bool tagsAllowed) {
    Direction out;
    const Thresholds th = thresholds(strength);
    const bool timed = !track.db.empty();

    // ---- words and their measurements
    std::vector<Word> words;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const HeardToken& tk = tokens[i];
        if (tk.kind != kTokWord) continue;
        Word w;
        w.token = i;
        w.start = tk.start;
        w.end = tk.end;
        w.letters = letterCount(tk.text);
        if (timed && tk.start >= 0 && tk.end > tk.start) w.m = measure(track, tk.start, tk.end);
        words.push_back(w);
    }

    // ---- stressed and drawn-out words, against the words around them (trend removed)
    std::vector<float> xs, loud, peak, perLetter;
    for (const Word& w : words) {
        if (w.m.seconds <= 0 || w.letters == 0) continue;
        perLetter.push_back((w.end - w.start) / float(w.letters));
        if (w.m.pitched < 3) continue;
        xs.push_back(0.5f * (w.start + w.end));
        loud.push_back(w.m.loud);
        peak.push_back(w.m.peak);
    }
    const float medPerLetter = perLetter.empty() ? 0.f : median(perLetter);
    if (xs.size() >= 3) {
        float la, lb, pa, pb;
        fitLine(xs, loud, la, lb);
        fitLine(xs, peak, pa, pb);
        // Residuals spread: a stressed word must stand out from the usual word-to-word wobble.
        std::vector<float> lr, pr;
        for (size_t i = 0; i < xs.size(); ++i) {
            lr.push_back(std::fabs(loud[i] - (la + lb * xs[i])));
            pr.push_back(std::fabs(peak[i] - (pa + pb * xs[i])));
        }
        const float lSpread = std::max(1.5f, median(lr) * 1.5f), pSpread = std::max(1.0f, median(pr) * 1.5f);

        struct Cand { size_t word; float score; };
        std::vector<Cand> cands;
        for (size_t i = 0; i < words.size(); ++i) {
            Word& w = words[i];
            if (w.m.pitched < 3 || w.letters == 0) continue;
            const float x = 0.5f * (w.start + w.end);
            w.loudRes = w.m.loud - (la + lb * x);
            w.peakRes = w.m.peak - (pa + pb * x);
            w.stretch = medPerLetter > 0 ? ((w.end - w.start) / float(w.letters)) / medPerLetter : 1.f;
            const float score = std::max(0.f, w.loudRes) / lSpread + std::max(0.f, w.peakRes) / pSpread +
                                std::max(0.f, w.stretch - 1.2f) / 0.5f;
            const bool standsOut = (w.loudRes >= th.stressLoud && w.peakRes >= 1.0f) || w.peakRes >= th.stressPitch + 1.0f ||
                                   w.loudRes >= th.stressLoud + 2.0f;
            if (score >= th.stressScore && standsOut) cands.push_back({i, score});
        }
        // At most one stressed word in five, the strongest first.
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
        const size_t limit = std::max<size_t>(1, xs.size() / 5);
        for (size_t k = 0; k < cands.size() && k < limit; ++k) words[cands[k].word].stressed = true;
    }
    for (Word& w : words) {
        const float dur = w.end - w.start;
        w.drawnOut = medPerLetter > 0 && w.letters > 0 && dur >= 0.45f && w.letters <= 6 &&
                     dur / float(w.letters) >= std::max(0.13f, th.stretch * medPerLetter) && w.m.voiced >= 0.5f;
    }

    // ---- split into parts at sentence ends and pauses
    struct PartSpan { size_t firstWord, lastWord; float pauseBefore; };
    std::vector<PartSpan> spans;
    for (size_t i = 0; i < words.size(); ++i) {
        float gap = 0;
        if (i > 0 && words[i].start >= 0 && words[i - 1].end >= 0) gap = words[i].start - words[i - 1].end;
        const bool cut = i == 0 || endsSentence(tokens[words[i - 1].token].text) || gap >= 0.35f;
        if (cut) spans.push_back({i, i, i == 0 ? 0.f : gap});
        else spans.back().lastWord = i;
    }

    // Reference voice: the learned usual voice, or (until it is learned) the phrase itself.
    Span whole;
    if (timed && !words.empty() && words.front().start >= 0) whole = measure(track, words.front().start, words.back().end);
    const bool useBase = base.ready();
    const float refLoud = useBase ? base.loudDb : whole.loud;
    const float refPitch = useBase ? base.pitch : whole.mid;
    const float refRange = useBase ? base.range : whole.range;
    const float refTilt = useBase ? base.tilt : whole.tilt;

    std::vector<std::string> partTags;
    for (const PartSpan& ps : spans) {
        SpokenPart part;
        part.pauseBefore = ps.pauseBefore;
        const Word& first = words[ps.firstWord];
        const Word& last = words[ps.lastWord];
        Span m;
        if (timed && first.start >= 0) m = measure(track, first.start, last.end);
        const float dLoud = m.loud - refLoud, dPitch = m.mid - refPitch, dTilt = m.tilt - refTilt;
        const bool enough = m.seconds >= 0.3f && m.pitched >= 6;
        // Comparing a part with its own phrase only means something when there are other parts.
        const bool canCompare = useBase || spans.size() >= 2;
        std::string why;

        if (m.seconds >= 0.4f && m.voiced < 0.2f) {
            part.tag = "whispers";
        } else if (enough && canCompare && dLoud >= th.shoutDb &&
                   (dTilt >= th.shoutEffort || (dPitch >= 4.f && dLoud >= th.shoutDb + 3.f))) {
            part.tag = "shouts";        // loud AND pushed hard: leaning into the mic alone doesn't count
            part.exclaim = true;
        } else if (enough && canCompare && dPitch >= th.excitedPitch &&
                   (m.range >= refRange * 1.3f || dTilt >= 1.5f || dLoud >= 3.f)) {
            part.tag = "excited";
            part.exclaim = true;
        } else if (enough && canCompare && dLoud <= th.softDb && dTilt <= -1.5f) {
            part.tag = "softly";
        } else if (enough && m.seconds >= 0.8f && m.range < th.flatRange && std::fabs(dPitch) < 2.f && dLoud < 4.f) {
            part.tag = "flatly";        // monotone
        }
        // Question: the voice rises over the last word.
        if (timed && last.start >= 0 && part.tag != "shouts") {
            const Span lastWord = measure(track, std::max(first.start, last.end - 0.6f), last.end);
            if (lastWord.pitched >= 6 && (lastWord.endRise >= th.rise || (m.endRise >= th.rise && lastWord.endRise >= 1.f))) {
                part.question = true;
                part.exclaim = false;
            }
        }

        // ---- the part's text
        std::string plain, marked;
        bool soundsAtStart = true;
        size_t from = first.token, to = last.token;
        // Sounds just before the first word or just after the last belong to this part.
        while (from > 0 && tokens[from - 1].kind != kTokWord) --from;
        if (&ps == &spans.back()) to = tokens.size() - 1;
        else while (to + 1 < tokens.size() && tokens[to + 1].kind == kTokSpace) ++to;
        for (size_t i = from; i <= to && i < tokens.size(); ++i) {
            const HeardToken& tk = tokens[i];
            if (tk.kind == kTokSpace) {
                plain += tk.text;
                marked += tk.text;
                continue;
            }
            if (tk.kind == kTokSound) {
                const std::string tag = eventTag(tk.text);
                if (tag.empty()) continue;
                (soundsAtStart ? part.leadSounds : part.trailSounds).push_back(tag);
                if (tagsAllowed) marked += " [" + tag + "] ";
                continue;
            }
            soundsAtStart = false;  // sounds from here on (between words, or after) go at the end once translated
            plain += tk.text;
            const Word* w = nullptr;
            for (const Word& cand : words)
                if (cand.token == i) w = &cand;
            std::string pre, post;
            std::vector<unsigned> core;
            splitWord(tk.text, pre, core, post);
            std::string lowerAscii;
            for (unsigned cp : core) lowerAscii += cp < 0x80 ? char(std::tolower(int(cp))) : '#';
            const bool caseScript = !core.empty() && hasCase(core[0]);
            if (w && w->drawnOut && caseScript) {
                // Repeat the last vowel: "no" -> "nooo", "so" -> "sooo".
                for (size_t k = core.size(); k-- > 0;)
                    if (isVowel(core[k])) { core.insert(core.begin() + long(k), 2, core[k]); break; }
                out.drawnOut.push_back(encode(std::vector<unsigned>(core.begin(), core.end())));
            }
            if (w && w->stressed && caseScript && tagsAllowed && (core.size() > 2 || !twoLetterOk(lowerAscii).empty())) {
                for (unsigned& cp : core) cp = upper(cp);  // v3 and v4 read capitals as emphasis
                out.stressed.push_back(encode(core));
            } else if (w && w->stressed) {
                out.stressed.push_back(encode(core));
            }
            marked += pre + encode(core) + post;
        }
        part.plain = trim(plain);
        part.marked = trim(marked);
        if (part.plain.empty() && part.leadSounds.empty()) continue;
        partTags.push_back(part.tag.empty() ? (part.question ? "question" : "usual") : part.tag);
        out.parts.push_back(std::move(part));

        // ---- learn the usual voice from ordinary parts (and from anything until it is learned)
        if (m.pitched >= 20 && m.voiced >= 0.4f && (!base.ready() || out.parts.back().tag.empty() || out.parts.back().tag == "flatly")) {
            const float a = std::max(0.1f, 1.f / float(base.phrases + 1));
            const float rate = float(ps.lastWord - ps.firstWord + 1) / std::max(0.3f, last.end - first.start);
            if (base.phrases == 0) {
                base.loudDb = m.loud; base.pitch = m.mid; base.range = std::max(1.f, m.range); base.tilt = m.tilt; base.rate = rate;
            } else {
                base.loudDb += a * (m.loud - base.loudDb);
                base.pitch += a * (m.mid - base.pitch);
                base.range += a * (std::max(1.f, m.range) - base.range);
                base.tilt += a * (m.tilt - base.tilt);
                base.rate += a * (rate - base.rate);
            }
            ++base.phrases;
        }
    }

    // ---- a summary for the app
    std::string s;
    auto list = [](const std::vector<std::string>& v) {
        std::string r;
        for (size_t i = 0; i < v.size(); ++i) r += (i ? ", " : "") + v[i];
        return r;
    };
    if (!out.stressed.empty()) s += "Stressed: " + list(out.stressed) + ". ";
    if (!out.drawnOut.empty()) s += "Drawn out: " + list(out.drawnOut) + ". ";
    bool anyTag = false;
    for (const std::string& t : partTags) anyTag = anyTag || t != "usual";
    if (anyTag) {
        s += partTags.size() > 1 ? "Parts: " : "Delivery: ";
        for (size_t i = 0; i < partTags.size(); ++i) s += (i ? ", then " : "") + partTags[i];
        s += ".";
    }
    if (s.empty()) s = base.ready() ? "Said in your usual way." : "Learning your usual voice.";
    out.summary = trim(s);
    return out;
}

std::string renderLine(const Direction& d, bool tagsAllowed) {
    std::string line;
    for (size_t i = 0; i < d.parts.size(); ++i) {
        const SpokenPart& p = d.parts[i];
        const bool lastPart = i + 1 == d.parts.size();
        const float nextPause = lastPart ? 0.f : d.parts[i + 1].pauseBefore;
        std::string text = p.marked;
        if (!tagsAllowed) {  // drop "[laughs]" kept inline for models that follow tags
            std::string clean;
            bool in = false;
            for (char c : text) {
                if (c == '[') in = true;
                else if (c == ']') in = false;
                else if (!in) clean += c;
            }
            text = clean;
        }
        std::string collapsed;
        for (char c : text)
            if (!(c == ' ' && !collapsed.empty() && collapsed.back() == ' ')) collapsed += c;
        text = trim(collapsed);
        if (!text.empty() && text.back() != ']') text = setEnding(text, p.question, p.exclaim, lastPart ? "." : nextPause >= 0.6f ? "..." : ",");
        if (text.empty()) continue;
        if (!line.empty()) line += " ";
        if (tagsAllowed && !p.tag.empty()) line += "[" + p.tag + "] ";
        line += text;
    }
    return line;
}

std::string renderTranslatedPart(const SpokenPart& part, const std::string& translated, bool tagsAllowed, bool first) {
    std::string s;
    if (!first && part.pauseBefore >= 0.6f) s += "... ";
    if (tagsAllowed && !part.tag.empty()) s += "[" + part.tag + "] ";
    if (tagsAllowed)
        for (const std::string& t : part.leadSounds) s += "[" + t + "] ";
    const std::string body = setEnding(translated, part.question, part.exclaim, ".");
    s += body;
    if (tagsAllowed)
        for (const std::string& t : part.trailSounds) s += " [" + t + "]";
    return trim(s);
}

bool partsDiffer(const Direction& d) {
    for (size_t i = 1; i < d.parts.size(); ++i)
        if (d.parts[i].tag != d.parts[0].tag || d.parts[i].question != d.parts[0].question) return true;
    return false;
}

std::string eventTag(const std::string& event) {
    std::string e;
    for (unsigned char c : event) e += char(std::tolower(c));
    if (e.find("laugh") != std::string::npos || e.find("giggl") != std::string::npos) return "laughs";
    if (e.find("chuckl") != std::string::npos) return "chuckles";
    if (e.find("sigh") != std::string::npos) return "sighs";
    if (e.find("gasp") != std::string::npos) return "gasps";
    if (e.find("throat") != std::string::npos) return "clears throat";
    if (e.find("sob") != std::string::npos || e.find("cry") != std::string::npos) return "crying";
    return std::string();
}

bool modelTakesTags(const std::string& modelId) {
    return modelId.find("eleven_v3") != std::string::npos || modelId.find("eleven_v4") != std::string::npos;
}
