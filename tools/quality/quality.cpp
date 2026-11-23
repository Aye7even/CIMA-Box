// Translation quality test: runs a list of lines through the app's own translation code (the same
// instructions, the same request, the same clean-up of the answer) against a model on this PC, and
// checks what a program can check: the right alphabet, the gender set under "I am" and "Talking to",
// the speech level, names kept, nothing answered that was only meant to be said, nothing left in the
// original language. Whether a line sounds native is not something a program can tell: every line
// and its translation is written to the report for a person to read.
//
// It costs nothing (the model runs on this PC). See README.txt in this folder for how to run it.
//
//   cima_quality --url http://127.0.0.1:8765 [--key KEY] --cases cases.json [--out report.txt]
//                [--only id-part] [--model name]
#include "translate.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

int msSince(std::chrono::steady_clock::time_point t) {
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count());
}

// The next character of UTF-8 text, as its number (and how many bytes it took).
uint32_t nextChar(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    uint32_t v = n == 1 ? c : c & (0xFF >> (n + 1));
    for (int k = 1; k < n && i + size_t(k) < s.size(); ++k) v = (v << 6) | (static_cast<unsigned char>(s[i + size_t(k)]) & 0x3F);
    i += size_t(n);
    return v;
}

// Which writing a letter belongs to ("" for digits, punctuation and spaces).
const char* writingOf(uint32_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F)) return "latin";
    if (c >= 0x400 && c <= 0x52F) return "cyrillic";
    if (c >= 0x590 && c <= 0x5FF) return "hebrew";
    if ((c >= 0x600 && c <= 0x6FF) || (c >= 0x750 && c <= 0x77F) || (c >= 0xFB50 && c <= 0xFEFF)) return "arabic";
    if (c >= 0x900 && c <= 0x97F) return "devanagari";
    if (c >= 0xE00 && c <= 0xE7F) return "thai";
    if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF)) return "kana";
    if (c >= 0x4E00 && c <= 0x9FFF) return "han";
    if ((c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F)) return "hangul";
    if (c >= 0x370 && c <= 0x3FF) return "greek";
    return "";
}

// The writing a language is in (Japanese counts kana and Han; Chinese Han).
bool rightWriting(const std::string& lang, const char* w) {
    const std::string s = w;
    if (lang == "ar" || lang == "fa" || lang == "ur") return s == "arabic";
    if (lang == "ru" || lang == "uk" || lang == "bg" || lang == "sr") return s == "cyrillic";
    if (lang == "ja") return s == "kana" || s == "han";
    if (lang == "zh" || lang == "yue") return s == "han";
    if (lang == "ko") return s == "hangul";
    if (lang == "hi") return s == "devanagari";
    if (lang == "th") return s == "thai";
    if (lang == "he") return s == "hebrew";
    if (lang == "el") return s == "greek";
    return s == "latin";
}

std::string lower(const std::string& s) {  // ASCII only: enough for the words the checks look for
    std::string r = s;
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return r;
}

bool has(const std::string& text, const std::string& part) { return lower(text).find(lower(part)) != std::string::npos; }

struct Result {
    std::string id, group, line, out, via, err, setup;
    std::vector<std::string> failed;
    int ms = 0;
    bool answered = false;
};

}  // namespace

int main(int argc, char** argv) {
    std::string url, key, casesFile, outFile, only, modelName = "local";
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i], v = argv[i + 1];
        if (a == "--url") url = v;
        else if (a == "--key") key = v;
        else if (a == "--cases") casesFile = v;
        else if (a == "--out") outFile = v;
        else if (a == "--only") only = v;
        else if (a == "--model") modelName = v;
    }
    if (url.empty() || casesFile.empty()) {
        std::printf("usage: cima_quality --url http://127.0.0.1:PORT [--key KEY] --cases cases.json [--out report.txt] [--only id-part]\n");
        return 2;
    }
    std::ifstream in(casesFile, std::ios::binary);
    const json cases = json::parse(in, nullptr, false);
    if (!cases.is_array()) {
        std::printf("can't read %s\n", casesFile.c_str());
        return 2;
    }

    Translator tr;
    std::vector<Result> results;
    std::string lastSetup;
    int firstMs = -1, warmedFirstMs = -1;
    for (const json& c : cases) {
        Result r;
        r.id = c.value("id", "");
        if (!only.empty() && r.id.find(only) == std::string::npos) continue;
        r.group = c.value("group", "other");
        r.line = c.value("line", "");
        const std::string toCode = c.value("to", "");
        const Language* to = findLanguage(toCode);
        const Language* from = findLanguage(c.value("from", "en"));
        if (!to || r.line.empty()) {
            r.err = "the case has no line or an unknown language";
            r.failed.push_back(r.err);
            results.push_back(r);
            continue;
        }
        TranslateSetup st;
        st.service = kLocal;
        st.localUrl = url;
        st.localKey = key;
        st.localModel = modelName;
        st.timeoutMs = 120000;  // the test is about what it says, not how fast: a model on the processor is given time
        const std::string level = c.value("level", "casual");
        st.level = level == "polite" ? kLevelPolite : level == "very casual" ? kLevelVeryCasual : kLevelCasual;
        const std::string me = c.value("i_am", ""), them = c.value("talking_to", "");
        st.speaker = me == "woman" ? kGenderWoman : me == "man" ? kGenderMan : me == "nonbinary" ? kGenderNonbinary : kGenderUnknown;
        st.listener = them == "woman" ? kListenerWoman : them == "man" ? kListenerMan : them == "group" ? kListenerGroup : kListenerUnknown;
        st.faithful = c.value("listen", false);  // Listen's side: someone else's speech, into your language
        st.context = c.value("context", "");
        st.marked = c.value("marked", "");
        st.persona = c.value("sound_like", "");
        const std::string dialect = c.value("dialect", "");
        if (!dialect.empty()) {
            const Variety* v = findVariety(dialect);
            if (!v) {
                r.err = "unknown dialect " + dialect;
                r.failed.push_back(r.err);
                results.push_back(r);
                continue;
            }
            st.variety = v->ai;
            st.varietyName = v->name;
        }
        r.setup = std::string(to->name) + (dialect.empty() ? "" : " (" + st.varietyName + ")") + ", " + level +
                  (me.empty() ? "" : ", I am a " + me) + (them.empty() ? "" : ", talking to a " + them) + (st.faithful ? ", Listen" : "");

        // How long the first line of a new setup takes, as it is and after the warm-up the app does
        // while you're still talking. (Measured once each, on the first two setups.)
        const bool newSetup = r.setup != lastSetup;
        const bool timeWarm = newSetup && firstMs >= 0 && warmedFirstMs < 0 && !st.faithful;
        if (timeWarm) {
            TranslateSetup w = st;
            tr.warm(w, from, to);
        }
        const auto t0 = std::chrono::steady_clock::now();
        r.answered = tr.translate(st, r.line, from, *to, r.out, r.err);
        r.ms = msSince(t0);
        r.via = tr.via;
        if (newSetup && firstMs < 0 && !st.faithful) firstMs = r.ms;
        else if (timeWarm) warmedFirstMs = r.ms;
        lastSetup = r.setup;

        // ---- what can be checked
        if (!r.answered || r.out.empty()) {
            r.failed.push_back("no answer (" + r.err + ")");
        } else {
            if (r.via.find("Local") != 0 || r.via.find('(') != std::string::npos) r.failed.push_back("not the local model's own answer (" + r.via + ")");
            // The alphabet: most of the letters, apart from names that stay as they are.
            int right = 0, wrong = 0;
            std::string kept = r.out;
            if (c.contains("keeps"))
                for (const json& k : c["keeps"]) {
                    const std::string name = k.get<std::string>();
                    for (size_t at = lower(kept).find(lower(name)); at != std::string::npos; at = lower(kept).find(lower(name)))
                        kept.erase(at, name.size());
                }
            for (size_t i = 0; i < kept.size();) {
                const char* w = writingOf(nextChar(kept, i));
                if (!*w) continue;
                (rightWriting(to->code, w) ? right : wrong)++;
            }
            const bool mixedOnPurpose = dialect == "hi-EN" || dialect == "fil-EN";
            if (!mixedOnPurpose && right + wrong > 0 && wrong * 4 > right + wrong)
                r.failed.push_back("a quarter or more of the letters are not " + std::string(to->name) + " writing");
            // Not simply left as it was (when the languages differ).
            if (from && from != to && lower(r.out) == lower(r.line)) r.failed.push_back("left in the original language");
            // Much longer than what was said: something was added or explained.
            if (r.out.size() > r.line.size() * 4 + 40) r.failed.push_back("far longer than the line");
            if (c.contains("keeps"))
                for (const json& k : c["keeps"])
                    if (!has(r.out, k.get<std::string>())) r.failed.push_back("lost \"" + k.get<std::string>() + "\"");
            if (c.contains("has_any")) {
                bool any = false;
                std::string list;
                for (const json& k : c["has_any"]) {
                    any = any || has(r.out, k.get<std::string>());
                    list += (list.empty() ? "" : " / ") + k.get<std::string>();
                }
                if (!any) r.failed.push_back(c.value("why_any", "expected one of") + ": " + list);
            }
            if (c.contains("has_none"))
                for (const json& k : c["has_none"])
                    if (has(r.out, k.get<std::string>())) r.failed.push_back(c.value("why_none", "must not have") + ": " + k.get<std::string>());
            if (c.value("is_question", false) && r.out.find('?') == std::string::npos && r.out.find("\xEF\xBC\x9F") == std::string::npos &&
                r.out.find("\xD8\x9F") == std::string::npos)
                r.failed.push_back("a question was answered or turned into a statement");
        }
        std::printf("%s  %-28s %s\n", r.failed.empty() ? "ok  " : "FAIL", r.id.c_str(), r.out.c_str());
        std::fflush(stdout);
        results.push_back(r);
    }

    // ---- the report
    std::string rep;
    int passed = 0;
    std::map<std::string, std::pair<int, int>> byGroup;  // passed, total
    std::vector<int> times;
    for (const Result& r : results) {
        passed += r.failed.empty();
        byGroup[r.group].second++;
        byGroup[r.group].first += r.failed.empty();
        if (r.answered) times.push_back(r.ms);
    }
    std::sort(times.begin(), times.end());
    char line[512];
    std::snprintf(line, sizeof(line), "CIMA Box translation quality test, model on this PC (%s)\n%d of %zu lines passed every check a program can make.\n",
                  modelName.c_str(), passed, results.size());
    rep += line;
    for (const auto& g : byGroup) {
        std::snprintf(line, sizeof(line), "  %-22s %d of %d\n", g.first.c_str(), g.second.first, g.second.second);
        rep += line;
    }
    if (!times.empty()) {
        std::snprintf(line, sizeof(line), "Time per line: half under %d ms, slowest %d ms.\n", times[times.size() / 2], times.back());
        rep += line;
    }
    if (firstMs >= 0 && warmedFirstMs >= 0) {
        std::snprintf(line, sizeof(line), "First line of a new setup: %d ms as it is, %d ms after the warm-up the app does while you talk.\n", firstMs, warmedFirstMs);
        rep += line;
    }
    rep += "\nThe checks only catch what shows in the letters. Read the lines below for the rest: does it sound like a native speaker?\n";
    std::string group;
    for (const Result& r : results) {
        if (r.group != group) {
            group = r.group;
            rep += "\n== " + group + "\n";
        }
        rep += (r.failed.empty() ? "ok    " : "FAIL  ") + r.id + "  [" + r.setup + "]\n        said: " + r.line + "\n        out:  " +
               (r.out.empty() ? "(nothing)" : r.out) + "   (" + std::to_string(r.ms) + " ms)\n";
        for (const std::string& f : r.failed) rep += "        ! " + f + "\n";
    }
    if (!outFile.empty()) std::ofstream(outFile, std::ios::binary) << rep;
    std::printf("\n%d of %zu passed%s\n", passed, results.size(), outFile.empty() ? "" : (". Report: " + outFile).c_str());
    return passed == int(results.size()) ? 0 : 1;
}
