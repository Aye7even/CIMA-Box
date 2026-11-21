#include "translate.h"

#include "extras.h"

#include <atomic>
#include <map>
#include <mutex>
#include <future>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cctype>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

const std::vector<Language>& languages() {
    // Every language Eleven v4 speaks. The 32 marked `true` are also spoken by the other models
    // (Multilingual v2, Flash v2.5, Turbo v2.5).
    static const std::vector<Language> list = {
        {"Afrikaans", "af", "afr", "af", "AF", "AF", false, ""},
        {"Amharic", "am", "amh", "am", "AM", "AM", false, ""},
        {"Arabic", "ar", "ara arb", "ar", "AR", "AR", true, ""},
        {"Armenian", "hy", "hye arm", "hy", "HY", "HY", false, ""},
        {"Assamese", "as", "asm", "as", "AS", "AS", false, ""},
        {"Asturian", "ast", "ast", "ast", "AST", "AST", false, ""},
        {"Azerbaijani", "az", "aze azj", "az", "AZ", "AZ", false, "Azeri"},
        {"Belarusian", "be", "bel", "be", "BE", "BE", false, ""},
        {"Bengali", "bn", "ben", "bn", "BN", "BN", false, "Bangla"},
        {"Bosnian", "bs", "bos", "bs", "BS", "BS", false, ""},
        {"Bulgarian", "bg", "bul", "bg", "BG", "BG", true, ""},
        {"Burmese", "my", "mya bur", "my", "MY", "MY", false, "Myanmar"},
        {"Cantonese", "yue", "yue", "zh-HK", "ZH-HANT", "ZH", false, "Chinese Hong Kong"},
        {"Catalan", "ca", "cat", "ca", "CA", "CA", false, ""},
        {"Cebuano", "ceb", "ceb", "ceb", "CEB", "CEB", false, "Bisaya Visayan"},
        {"Chinese (Mandarin)", "zh", "zho cmn chi", "zh-CN", "ZH", "ZH", true, "Mandarin Putonghua"},
        {"Croatian", "hr", "hrv", "hr", "HR", "HR", true, ""},
        {"Czech", "cs", "ces cze", "cs", "CS", "CS", true, ""},
        {"Danish", "da", "dan", "da", "DA", "DA", true, ""},
        {"Dutch", "nl", "nld dut", "nl", "NL", "NL", true, "Flemish"},
        {"English", "en", "eng", "en", "EN-US", "EN", true, ""},
        {"Estonian", "et", "est ekk", "et", "ET", "ET", false, ""},
        {"Filipino", "fil", "fil tgl tl", "tl", "TL", "TL", true, "Tagalog"},  // DeepL calls it Tagalog, "TL"
        {"Finnish", "fi", "fin", "fi", "FI", "FI", true, ""},
        {"French", "fr", "fra fre", "fr", "FR", "FR", true, ""},
        {"Fula", "ff", "ful fuc", "ff", "FF", "FF", false, "Pulaar Fulani"},
        {"Galician", "gl", "glg", "gl", "GL", "GL", false, ""},
        {"Georgian", "ka", "kat geo", "ka", "KA", "KA", false, ""},
        {"German", "de", "deu ger", "de", "DE", "DE", true, ""},
        {"Greek", "el", "ell gre", "el", "EL", "EL", true, ""},
        {"Gujarati", "gu", "guj", "gu", "GU", "GU", false, ""},
        {"Hausa", "ha", "hau", "ha", "HA", "HA", false, ""},
        {"Hebrew", "he", "heb iw", "he", "HE", "HE", false, ""},
        {"Hindi", "hi", "hin", "hi", "HI", "HI", true, ""},
        {"Hungarian", "hu", "hun", "hu", "HU", "HU", true, ""},
        {"Icelandic", "is", "isl ice", "is", "IS", "IS", false, ""},
        {"Indonesian", "id", "ind", "id", "ID", "ID", true, "Bahasa"},
        {"Italian", "it", "ita", "it", "IT", "IT", true, ""},
        {"Japanese", "ja", "jpn", "ja", "JA", "JA", true, ""},
        {"Javanese", "jv", "jav", "jv", "JV", "JV", false, ""},
        {"Kamba", "kam", "kam", "kam", "KAM", "KAM", false, "Kikamba"},
        {"Kannada", "kn", "kan", "kn", "KN", "KN", false, ""},
        {"Kazakh", "kk", "kaz", "kk", "KK", "KK", false, ""},
        {"Korean", "ko", "kor", "ko", "KO", "KO", true, ""},
        {"Kurdish (Sorani)", "ckb", "ckb", "ckb", "CKB", "CKB", false, "Sorani Kurdish"},
        {"Kyrgyz", "ky", "kir", "ky", "KY", "KY", false, "Kirghiz"},
        {"Lao", "lo", "lao", "lo", "LO", "LO", false, "Laotian"},
        {"Latvian", "lv", "lav lvs", "lv", "LV", "LV", false, ""},
        {"Lingala", "ln", "lin", "ln", "LN", "LN", false, ""},
        {"Lithuanian", "lt", "lit", "lt", "LT", "LT", false, ""},
        {"Luganda", "lg", "lug", "lg", "LG", "LG", false, "Ganda"},
        {"Luxembourgish", "lb", "ltz", "lb", "LB", "LB", false, ""},
        {"Macedonian", "mk", "mkd mac", "mk", "MK", "MK", false, ""},
        {"Malay", "ms", "msa may zsm", "ms", "MS", "MS", true, ""},
        {"Malayalam", "ml", "mal", "ml", "ML", "ML", false, ""},
        {"Maltese", "mt", "mlt", "mt", "MT", "MT", false, ""},
        {"Maori", "mi", "mri mao", "mi", "MI", "MI", false, "Te Reo"},
        {"Marathi", "mr", "mar", "mr", "MR", "MR", false, ""},
        {"Mongolian", "mn", "mon khk", "mn", "MN", "MN", false, ""},
        {"Nepali", "ne", "nep npi", "ne", "NE", "NE", false, ""},
        {"Norwegian", "no", "nor nob nno nb nn", "no", "NB", "NB", true, "Bokmal"},
        {"Occitan", "oc", "oci", "oc", "OC", "OC", false, ""},
        {"Odia", "or", "ori ory", "or", "OR", "OR", false, "Oriya"},
        {"Pashto", "ps", "pus pbt", "ps", "PS", "PS", false, "Pushto"},
        {"Persian", "fa", "fas per pes", "fa", "FA", "FA", false, "Farsi Dari"},
        {"Polish", "pl", "pol", "pl", "PL", "PL", true, ""},
        {"Portuguese", "pt", "por", "pt-BR", "PT-BR", "PT", true, "Brazilian"},
        {"Punjabi", "pa", "pan", "pa", "PA", "PA", false, "Panjabi"},
        {"Romanian", "ro", "ron rum", "ro", "RO", "RO", true, "Moldovan"},
        {"Russian", "ru", "rus", "ru", "RU", "RU", true, ""},
        {"Serbian", "sr", "srp", "sr", "SR", "SR", false, ""},
        {"Shona", "sn", "sna", "sn", "SN", "SN", false, ""},
        {"Sindhi", "sd", "snd", "sd", "SD", "SD", false, ""},
        {"Slovak", "sk", "slk slo", "sk", "SK", "SK", true, ""},
        {"Slovenian", "sl", "slv", "sl", "SL", "SL", false, "Slovene"},
        {"Somali", "so", "som", "so", "SO", "SO", false, ""},
        {"Spanish", "es", "spa", "es", "ES", "ES", true, "Castilian Espanol"},
        {"Swahili", "sw", "swa swh", "sw", "SW", "SW", false, "Kiswahili"},
        {"Swedish", "sv", "swe", "sv", "SV", "SV", true, ""},
        {"Tajik", "tg", "tgk", "tg", "TG", "TG", false, ""},
        {"Tamil", "ta", "tam", "ta", "TA", "TA", true, ""},
        {"Telugu", "te", "tel", "te", "TE", "TE", false, ""},
        {"Thai", "th", "tha", "th", "TH", "TH", false, ""},
        {"Turkish", "tr", "tur", "tr", "TR", "TR", true, ""},
        {"Ukrainian", "uk", "ukr", "uk", "UK", "UK", true, ""},
        {"Urdu", "ur", "urd", "ur", "UR", "UR", false, ""},
        {"Uzbek", "uz", "uzb uzn", "uz", "UZ", "UZ", false, ""},
        {"Vietnamese", "vi", "vie", "vi", "VI", "VI", true, ""},
        {"Welsh", "cy", "cym wel", "cy", "CY", "CY", false, "Cymraeg"},
        {"Wolof", "wo", "wol", "wo", "WO", "WO", false, ""},
        {"Zulu", "zu", "zul", "zu", "ZU", "ZU", false, "isiZulu"},
    };
    return list;
}

const Language* findLanguage(const std::string& code) {
    std::string c;
    for (char ch : code) {
        if (ch == '-' || ch == '_') break;  // "en-US" -> "en"
        c += char(std::tolower((unsigned char)ch));
    }
    if (c.empty()) return nullptr;
    for (const Language& l : languages()) {
        if (c == l.code) return &l;
        std::istringstream alts(l.iso3);
        for (std::string a; alts >> a;)
            if (c == a) return &l;
    }
    return nullptr;
}

bool languageMatches(const Language& l, const std::string& query) {
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::string q = lower(query);
    while (!q.empty() && q.back() == ' ') q.pop_back();
    while (!q.empty() && q.front() == ' ') q.erase(0, 1);
    if (q.empty()) return true;
    // The start of any word: "ja" finds Japanese and Javanese, not Gujarati.
    auto wordStarts = [&q](const std::string& text) {
        for (size_t i = text.find(q); i != std::string::npos; i = text.find(q, i + 1))
            if (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1]))) return true;
        return false;
    };
    return wordStarts(lower(l.name)) || wordStarts(lower(l.otherNames)) || q == l.code;
}

namespace {

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
}

// MyMemory sometimes returns HTML entities.
std::string decodeEntities(std::string s) {
    replaceAll(s, "&#39;", "'");
    replaceAll(s, "&apos;", "'");
    replaceAll(s, "&quot;", "\"");
    replaceAll(s, "&lt;", "<");
    replaceAll(s, "&gt;", ">");
    replaceAll(s, "&amp;", "&");
    return s;
}

// MyMemory takes at most 500 bytes per request: cut at a space when there is one, never inside a UTF-8 character.
std::vector<std::string> chunks(const std::string& text, size_t maxBytes) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = std::min(text.size(), pos + maxBytes);
        if (end < text.size()) {
            const size_t sp = text.rfind(' ', end);
            if (sp != std::string::npos && sp > pos) end = sp;
            else while (end > pos + 1 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
        }
        out.push_back(text.substr(pos, end - pos));
        pos = end;
        while (pos < text.size() && text[pos] == ' ') ++pos;
    }
    return out;
}

// The day (counted from 1970) Free ran out of its daily allowance, or -1. Until the day changes,
// Free isn't asked again: every ask would only come back "limit reached", after a wait.
std::atomic<long> gFreeOutDay{-1};
long dayNumber() {
    return long(std::chrono::duration_cast<std::chrono::hours>(std::chrono::system_clock::now().time_since_epoch()).count() / 24);
}
bool freeUsedUp() { return gFreeOutDay.load() == dayNumber(); }

bool myMemory(Http& http, const std::string& text, const Language& from, const Language& to, std::string& out,
              std::string& err) {
    out.clear();
    if (freeUsedUp()) {
        err = "Free translation limit reached for today. Switch to DeepL or try again tomorrow.";
        return false;
    }
    for (const std::string& piece : chunks(text, 450)) {
        HttpRequest r;
        r.url = "https://api.mymemory.translated.net/get?q=" + Http::urlEncode(piece) + "&langpair=" +
                Http::urlEncode(std::string(from.myMemory) + "|" + to.myMemory);
        r.timeoutSec = 20;
        long status = 0;
        std::string body;
        if (!http.perform(r, status, body, err)) return false;
        json j = json::parse(body, nullptr, false);
        std::string translated, details;
        long apiStatus = status;
        if (j.is_object()) {
            if (j.contains("responseStatus")) {
                const json& s = j["responseStatus"];
                if (s.is_number_integer()) apiStatus = s.get<long>();
                else if (s.is_string()) apiStatus = std::atol(s.get<std::string>().c_str());
            }
            if (j.contains("responseDetails") && j["responseDetails"].is_string()) details = j["responseDetails"].get<std::string>();
            if (j.contains("responseData") && j["responseData"].is_object() && j["responseData"].contains("translatedText") &&
                j["responseData"]["translatedText"].is_string())
                translated = j["responseData"]["translatedText"].get<std::string>();
        }
        const bool warning = translated.rfind("MYMEMORY WARNING", 0) == 0;
        if (status != 200 || apiStatus != 200 || translated.empty() || warning) {
            if (warning || apiStatus == 429) {
                err = "Free translation limit reached for today. Switch to DeepL or try again tomorrow.";
                gFreeOutDay = dayNumber();
            }
            else err = "Translation failed (" + std::to_string(apiStatus) + ")" + (details.empty() ? "" : ": " + details);
            return false;
        }
        if (!out.empty()) out += ' ';
        out += decodeEntities(translated);
    }
    return true;
}

}  // namespace

// Keys DeepL turned away at the address their ending points to, but took at the other one (a free
// key without ":fx", or the other way round): from then on, that other address.
std::mutex gDeeplHostMx;
std::map<std::string, bool> gDeeplFlipped;

std::string deeplHost(const std::string& key) {
    bool free = key.size() > 3 && key.compare(key.size() - 3, 3, ":fx") == 0;
    {
        std::lock_guard<std::mutex> lk(gDeeplHostMx);
        const auto it = gDeeplFlipped.find(key);
        if (it != gDeeplFlipped.end() && it->second) free = !free;
    }
    return free ? "https://api-free.deepl.com" : "https://api.deepl.com";
}

namespace {
std::atomic<long long> gTokens[3][2];  // [claude, gemini, openai][in, out] used in this run
void countTokens(int which, long long in, long long out) {
    if (in > 0) gTokens[which][0] += in;
    if (out > 0) gTokens[which][1] += out;
}
long long tokenField(const json& j, const char* key) {
    return j.is_object() && j.contains(key) && j[key].is_number() ? j[key].get<long long>() : 0;
}
}  // namespace

void aiTokens(int service, long long& in, long long& out) {
    const int w = service == kClaude ? 0 : service == kGemini ? 1 : 2;
    in = gTokens[w][0];
    out = gTokens[w][1];
}

namespace {

// DeepL can't do slang, but it does know polite from informal in many languages ("du" or "Sie").
// Set for the phrase being translated: "prefer_less" (Casual, Very casual), "prefer_more" (Polite)
// or empty. The "prefer_" forms are simply ignored for languages without the choice.
thread_local std::string tlFormality;
// The sentence a single word comes from ("Words for DeepL", swear words): DeepL reads it to pick the
// right meaning and form, but doesn't translate it. Empty for whole lines.
thread_local std::string tlContext;

bool deeplOnce(Http& http, const std::string& key, const std::string& text, const Language* from, const std::string& target,
               std::string& out, std::string& err, bool& languageRefused, bool formality = true, bool hostTried = false,
               bool nextGen = false) {
    languageRefused = false;
    json req = {{"text", json::array({text})}, {"target_lang", target}};
    // DeepL's newer languages (Tagalog, Cebuano and others) are only done by its next-gen model.
    if (nextGen) req["model_type"] = "prefer_quality_optimized";
    if (from) req["source_lang"] = from->deeplSource;
    const bool formal = formality && !tlFormality.empty();
    if (formal) req["formality"] = tlFormality;
    if (!tlContext.empty()) req["context"] = tlContext;
    const std::string payload = req.dump();
    HttpRequest r;
    r.url = deeplHost(key) + "/v2/translate";
    r.headers = {"Authorization: DeepL-Auth-Key " + key, "Content-Type: application/json"};
    r.body = &payload;
    r.timeoutSec = 20;
    long status = 0;
    std::string body;
    if (!http.perform(r, status, body, err)) return false;
    json j = json::parse(body, nullptr, false);
    if (status != 200) {
        std::string msg = j.is_object() && j.contains("message") && j["message"].is_string() ? j["message"].get<std::string>() : "";
        std::string lower;
        for (char c : msg) lower += char(std::tolower(static_cast<unsigned char>(c)));
        if (status == 400 && formal && lower.find("formality") != std::string::npos)  // not for this language after all
            return deeplOnce(http, key, text, from, target, out, err, languageRefused, false, hostTried, nextGen);
        // It can't read the spoken language as given (or it was guessed wrong): let DeepL work it out.
        if (status == 400 && from && lower.find("source_lang") != std::string::npos)
            return deeplOnce(http, key, text, nullptr, target, out, err, languageRefused, formal, hostTried, nextGen);
        // Only a target language DeepL doesn't have counts as "DeepL doesn't do this language". Any
        // other refusal is an error to show, not a reason to quietly switch to Free.
        if (status == 400 && lower.find("target_lang") != std::string::npos) {
            if (!nextGen)  // maybe a language only the next-gen model does: once more, asking for that model
                return deeplOnce(http, key, text, from, target, out, err, languageRefused, formal, hostTried, true);
            languageRefused = true;
        }
        // Refused at this address: the key may belong to the other one (free and Pro keys go to
        // different places, told apart by the ":fx" ending). One try there.
        if (status == 403 && !hostTried) {
            {
                std::lock_guard<std::mutex> lk(gDeeplHostMx);
                gDeeplFlipped[key] = !gDeeplFlipped[key];
            }
            if (deeplOnce(http, key, text, from, target, out, err, languageRefused, formal, true, nextGen)) return true;
            std::lock_guard<std::mutex> lk(gDeeplHostMx);
            gDeeplFlipped[key] = !gDeeplFlipped[key];  // not that either: back as it was
            return false;
        }
        if (status == 403) msg = "the API key was rejected";
        else if (status == 456) msg = "this month's character quota is used up";
        err = "DeepL " + std::to_string(status) + (msg.empty() ? "" : ": " + msg);
        return false;
    }
    if (!j.is_object() || !j.contains("translations") || !j["translations"].is_array() || j["translations"].empty() ||
        !j["translations"][0].contains("text") || !j["translations"][0]["text"].is_string()) {
        err = "Unexpected DeepL response";
        return false;
    }
    out = j["translations"][0]["text"].get<std::string>();
    return true;
}

// `variant`: DeepL's regional target (ES-419, PT-PT...), tried first; if DeepL refuses it, the
// standard language is used.
bool deepl(Http& http, const std::string& key, const std::string& text, const Language* from, const Language& to,
           const std::string& variant, std::string& out, std::string& err, bool& languageRefused) {
    if (key.empty()) { err = "Add your DeepL API key, or switch the translation service to Free"; languageRefused = false; return false; }
    if (!variant.empty() && variant != to.deeplTarget) {
        if (deeplOnce(http, key, text, from, variant, out, err, languageRefused)) return true;
        if (!languageRefused) return false;
    }
    return deeplOnce(http, key, text, from, to.deeplTarget, out, err, languageRefused);
}

// Microsoft Translator's name for a language, where it differs from ours.
std::string msCode(const Language& l) {
    static const std::pair<const char*, const char*> kDiff[] = {
        {"zh", "zh-Hans"}, {"no", "nb"}, {"sr", "sr-Cyrl"}, {"mn", "mn-Cyrl"}, {"iw", "he"}};
    for (const auto& d : kDiff)
        if (std::string(l.code) == d.first) return d.second;
    return l.code;
}

// Microsoft Translator (Azure), an optional backup: 2 million characters a month free, 100+ languages
// (Filipino too). `languageRefused`: it doesn't do this language, so the next backup should.
bool microsoft(Http& http, const std::string& key, const std::string& region, const std::string& text, const Language* from,
               const Language& to, std::string& out, std::string& err, bool& languageRefused) {
    languageRefused = false;
    if (key.empty()) { err = "No Microsoft Translator key"; return false; }
    const std::string payload = json::array({{{"Text", text}}}).dump();
    HttpRequest r;
    r.url = "https://api.cognitive.microsofttranslator.com/translate?api-version=3.0&to=" + Http::urlEncode(msCode(to)) +
            (from ? "&from=" + Http::urlEncode(msCode(*from)) : std::string());
    r.headers = {"Ocp-Apim-Subscription-Key: " + key, "Content-Type: application/json"};
    if (!region.empty()) r.headers.push_back("Ocp-Apim-Subscription-Region: " + region);
    r.body = &payload;
    r.timeoutSec = 15;
    long status = 0;
    std::string body;
    if (!http.perform(r, status, body, err)) return false;
    const json j = json::parse(body, nullptr, false);
    if (status != 200) {
        long code = 0;
        if (j.is_object() && j.contains("error") && j["error"].is_object() && j["error"].contains("code") &&
            j["error"]["code"].is_number_integer())
            code = j["error"]["code"].get<long>();
        // 400035 / 400036: the source / target language isn't one it has.
        if (code == 400035 && from) return microsoft(http, key, region, text, nullptr, to, out, err, languageRefused);
        if (code == 400036) languageRefused = true;
        err = code == 403001 ? "Microsoft Translator: this month's free characters are used up"
            : status == 401  ? "Microsoft Translator: the key (or its region) was refused"
            : status == 429  ? "Microsoft Translator: too many requests right now"
            : "Microsoft Translator " + std::to_string(status) + (code ? " (" + std::to_string(code) + ")" : std::string());
        return false;
    }
    if (!j.is_array() || j.empty() || !j[0].is_object() || !j[0].contains("translations") || !j[0]["translations"].is_array() ||
        j[0]["translations"].empty() || !j[0]["translations"][0].contains("text") || !j[0]["translations"][0]["text"].is_string()) {
        err = "Unexpected Microsoft Translator response";
        return false;
    }
    out = j[0]["translations"][0]["text"].get<std::string>();
    return !out.empty();
}

}  // namespace

// ---------------------------------------------------------------- AI services (Claude, Gemini, OpenAI)

namespace {

std::string trimText(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws), b = s.find_last_not_of(ws);
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// The answer should be the line itself; tidy up what models sometimes add around it.
std::string cleanAnswer(std::string s) {
    s = trimText(s);
    std::string one;  // one spoken line
    for (char c : s) one += (c == '\n' || c == '\r') ? ' ' : c;
    s = trimText(one);
    for (const char* q : {"\"", "\xE2\x80\x9C", "\xE3\x80\x8C"}) {  // "...", “...”, 「...」
        const std::string open(q);
        const std::string close = open == "\"" ? "\"" : open == "\xE2\x80\x9C" ? "\xE2\x80\x9D" : "\xE3\x80\x8D";
        if (s.size() > open.size() + close.size() && s.compare(0, open.size(), open) == 0 &&
            s.compare(s.size() - close.size(), close.size(), close) == 0)
            s = s.substr(open.size(), s.size() - open.size() - close.size());
    }
    return trimText(s);
}

// The AI is asked to put the line inside <line>...</line>. Anything outside it (a note, an
// explanation of a garbled transcript) is dropped. Without the tags, an answer that quotes the
// original and runs long is an explanation, not a line to say or show: refused, so the phrase goes
// to DeepL / Free instead of putting a paragraph in the caption or the voice.
std::string lowerCopy(const std::string& s) {
    std::string r;
    for (char c : s) r += char(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

bool extractAnswer(const std::string& raw, const std::string& source, std::string& out, int lines = 0) {
    if (raw.find("<skip") != std::string::npos) {  // it won't translate this (see kSkipRule): never spoken
        out.clear();
        return false;
    }
    if (lines > 0) {  // several numbered lines: every one must be there, each in its own tag
        std::string joined;
        for (int i = 1; i <= lines; ++i) {
            const std::string open = "<l" + std::to_string(i) + ">", close = "</l" + std::to_string(i) + ">";
            const size_t a = raw.find(open);
            const size_t b = a == std::string::npos ? std::string::npos : raw.find(close, a + open.size());
            if (b == std::string::npos) { out.clear(); return false; }
            const std::string piece = cleanAnswer(raw.substr(a + open.size(), b - a - open.size()));
            if (piece.empty()) { out.clear(); return false; }
            joined += (i > 1 ? "\x1E" : "") + piece;
        }
        out = joined;
        return true;
    }
    const size_t a = raw.find("<line>");
    if (a != std::string::npos) {
        const size_t b = raw.find("</line>", a + 6);
        out = cleanAnswer(raw.substr(a + 6, b == std::string::npos ? std::string::npos : b - a - 6));
        // Far longer than what was said: that's not a translation of it. It's a refusal or an
        // explanation written inside the tags (in any language), and must not be spoken or shown.
        if (out.size() > trimText(source).size() * 3 + 80) out.clear();
        return !out.empty();
    }
    out = cleanAnswer(raw);
    const std::string src = trimText(source);
    // A refusal instead of a line ("I'm sorry, but I can't help with that"): not something to say.
    {
        const std::string low = lowerCopy(out), lowSrc = lowerCopy(src);
        for (const char* r : {"i'm sorry", "i am sorry", "i can't help", "i cannot help", "i can't assist", "i cannot assist",
                              "i can't translate", "i cannot translate", "i won't", "i'm not able to", "i am not able to",
                              "i'm unable to", "i am unable to"})
            if (low.find(r) != std::string::npos && lowSrc.find(r) == std::string::npos) {
                out.clear();
                return false;
            }
    }
    const bool quotesSource = src.size() >= 4 && raw.find(src) != std::string::npos && out != src;
    const bool long_ = out.size() > src.size() * 3 + 80;
    const bool severalLines = trimText(raw).find('\n') != std::string::npos;
    if ((quotesSource && (out.size() > src.size() * 2 + 30 || severalLines)) || (long_ && severalLines)) {
        out.clear();
        return false;
    }
    return !out.empty();
}


std::string httpProblem(const char* service, long status, const std::string& message) {
    std::string why = message;
    if (status == 401 || status == 403) why = "the API key was rejected";
    else if (status == 429) {
        const std::string low = lowerCopy(message);
        // Gemini says "quota ... billing" for its free plan's per-minute limit too: not about credit.
        why = std::string(service) == "Gemini"
                  ? "hit the plan's request limit (per minute, or for the day)"
              : low.find("credit") != std::string::npos || low.find("billing") != std::string::npos
                  ? "out of credit"
              : low.find("per day") != std::string::npos || low.find("daily") != std::string::npos ||
                        low.find("perday") != std::string::npos
                  ? "the plan's daily limit is used up"
                  : "too many requests for the plan's limit (slowing down for a bit)";
    }
    else if (status == 529 || status == 503) why = "the service is overloaded right now";
    return std::string(service) + " " + std::to_string(status) + (why.empty() ? "" : ": " + why);
}

// Without this, a model that won't say a line writes its refusal in the target language, inside
// the tags, and the refusal itself gets spoken in the person's voice. This gives it a clean way to
// say no, which the app then handles (see extractAnswer).
const std::string kSkipRule = " If you won't translate a line, answer with exactly <skip/> and nothing else: never write a refusal, an apology or an explanation, in any language, because whatever you write is spoken aloud or shown as the speaker's own words.";
thread_local std::string tlRaw;  // the last AI answer as it came (for the <g> gender note)

// Why an AI call failed, to decide whether one more try is worth it.
// kFailBusy: overloaded for a moment (worth one more try). kFailLimited: too many requests for the
// plan (another try right away only makes it worse). kFailDead: won't work until something changes
// (key refused, out of credit, the plan can't use the model).
enum AiFail { kFailOther = 0, kFailRejected, kFailBusy, kFailLimited, kFailDead };

std::string errorMessage(const json& j) {
    if (j.is_object() && j.contains("error") && j["error"].is_object() && j["error"].contains("message") &&
        j["error"]["message"].is_string())
        return j["error"]["message"].get<std::string>();
    return {};
}

// Where the part of the instructions that changes from line to line begins (the recent conversation,
// the line said just before): everything ahead of it is the same for the whole session. The whole
// length when there is no such part.
size_t changingPartStart(const std::string& instructions) {
    size_t cut = instructions.size();
    for (const char* head : {"\nRecent conversation, for context only", "\nThis line goes on from what they said just before",
                             "\n\nEarlier lines of the conversation, for context only",
                             "\n\nThis line goes on from what the speaker said just before"})
        cut = std::min(cut, instructions.find(head));
    return cut;
}

// `source`: what was said (to tell an explanation from a translation); `text`: what is sent.
bool claude(Http& http, const TranslateSetup& st, const std::string& instructions, const std::string& source,
            const std::string& text, int timeoutMs, std::string& out, std::string& err, int& why, bool& noEffort) {
    why = kFailOther;
    if (st.claudeKey.empty()) { err = "Add your Claude (Anthropic) API key"; return false; }
    for (int attempt = 0; attempt < 2; ++attempt) {
        // The instructions are the same from line to line except for the recent conversation at the
        // end. Marked for Anthropic's prompt cache, the fixed part is read once and reused for the
        // next five minutes (each line renews it): answers start sooner and that part is billed at a
        // tenth. Too short to cache (the caption instructions): sent as always, no error.
        json system = json::array();
        const size_t cut = changingPartStart(instructions);
        system.push_back({{"type", "text"}, {"text", instructions.substr(0, cut)}, {"cache_control", {{"type", "ephemeral"}}}});
        if (cut < instructions.size()) system.push_back({{"type", "text"}, {"text", instructions.substr(cut)}});
        json req = {{"model", st.claudeModel.empty() ? "claude-haiku-5-5" : st.claudeModel},
                    {"max_tokens", 1024},  // room for any brief thinking; the answer itself is one line
                    {"system", system},
                    {"messages", json::array({{{"role", "user"}, {"content", text}}})}};
        if (!noEffort) req["output_config"] = {{"effort", "low"}};  // fastest: skips thinking on simple requests
        const std::string payload = req.dump();
        HttpRequest r;
        r.url = "https://api.anthropic.com/v1/messages";
        r.headers = {"x-api-key: " + st.claudeKey, "anthropic-version: 2023-06-01", "content-type: application/json"};
        r.body = &payload;
        r.timeoutMs = timeoutMs;
        long status = 0;
        std::string body;
        if (!http.perform(r, status, body, err)) return false;
        json j = json::parse(body, nullptr, false);
        if (status != 200) {
            const std::string msg = errorMessage(j);
            // An older model that doesn't know the effort setting: ask again without it.
            if (status == 400 && !noEffort && (lowerCopy(msg).find("effort") != std::string::npos ||
                                               lowerCopy(msg).find("output_config") != std::string::npos)) {
                noEffort = true;
                continue;
            }
            const std::string low = lowerCopy(msg);
            if (status == 401 || status == 403 || low.find("credit balance") != std::string::npos) why = kFailDead;
            else if (status == 429) why = kFailLimited;
            else if (status == 529 || status == 503 || status == 500) why = kFailBusy;
            err = httpProblem("Claude", status, msg);
            return false;
        }
        if (j.is_object() && j.contains("usage")) {
            // Counted at what they cost: writing to the cache is a quarter more than plain input,
            // reading from it a tenth.
            const json& u = j["usage"];
            countTokens(0, tokenField(u, "input_tokens") + tokenField(u, "cache_creation_input_tokens") * 5 / 4 +
                               tokenField(u, "cache_read_input_tokens") / 10,
                        tokenField(u, "output_tokens"));
        }
        std::string text2;
        if (j.is_object() && j.contains("content") && j["content"].is_array())
            for (const json& block : j["content"])
                if (block.is_object() && block.value("type", "") == "text" && block.contains("text") && block["text"].is_string())
                    text2 += block["text"].get<std::string>();
        tlRaw = text2;
        if (!extractAnswer(text2, source, out, st.multiLines)) {
            why = kFailRejected;
            err = out.empty() && trimText(text2).empty() ? "Claude gave an empty answer" : "Claude explained instead of translating";
            return false;
        }
        return true;
    }
    return false;
}

bool gemini(Http& http, const TranslateSetup& st, const std::string& instructions, const std::string& source,
            const std::string& text, int timeoutMs, std::string& out, std::string& err, int& why, int& thinking) {
    why = kFailOther;
    if (st.geminiKey.empty()) { err = "Add your Gemini (Google AI Studio) API key"; return false; }
    // Thinking takes time, and a one-line translation doesn't need it: "minimal" (Flash models) is
    // the fastest; a model that refuses it gets "low", and one with no levels at all gets nothing.
    static const char* const kLevels[] = {"minimal", "low"};
    for (int attempt = 0; attempt < 3; ++attempt) {
        json gen = {{"maxOutputTokens", 1024}};
        if (thinking < 2) gen["thinkingConfig"] = {{"thinkingLevel", kLevels[thinking]}};
        // Swearing, insults between friends and rough talk are part of real speech, and the person
        // chose to say them: Google's filters are turned off so a line isn't silently dropped.
        json safety = json::array();
        for (const char* cat : {"HARM_CATEGORY_HARASSMENT", "HARM_CATEGORY_HATE_SPEECH", "HARM_CATEGORY_SEXUALLY_EXPLICIT",
                                "HARM_CATEGORY_DANGEROUS_CONTENT"})
            safety.push_back({{"category", cat}, {"threshold", "BLOCK_NONE"}});
        json req = {{"systemInstruction", {{"parts", json::array({{{"text", instructions}}})}}},
                    {"contents", json::array({{{"role", "user"}, {"parts", json::array({{{"text", text}}})}}})},
                    {"generationConfig", gen},
                    {"safetySettings", safety}};
        const std::string payload = req.dump();
        HttpRequest r;
        r.url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                (st.geminiModel.empty() ? std::string("gemini-3.5-flash-lite") : st.geminiModel) + ":generateContent";
        r.headers = {"x-goog-api-key: " + st.geminiKey, "Content-Type: application/json"};
        r.body = &payload;
        r.timeoutMs = timeoutMs;
        long status = 0;
        std::string body;
        if (!http.perform(r, status, body, err)) return false;
        json j = json::parse(body, nullptr, false);
        if (status != 200) {
            const std::string msg = errorMessage(j);
            if (status == 400 && thinking < 2 && lowerCopy(msg).find("thinking") != std::string::npos) {
                ++thinking;  // that level isn't offered by this model: one step up, or none at all
                continue;
            }
            if (status == 401 || status == 403 || (status == 400 && lowerCopy(msg).find("api key") != std::string::npos))
                why = kFailDead;
            else if (status == 429) why = kFailLimited;  // the free plan's per-minute (or daily) limit
            else if (status == 503 || status == 500) why = kFailBusy;
            err = httpProblem("Gemini", status, msg);
            return false;
        }
        if (j.is_object() && j.contains("usageMetadata")) {  // thinking is billed as output too
            const json& u = j["usageMetadata"];
            countTokens(1, tokenField(u, "promptTokenCount"), tokenField(u, "candidatesTokenCount") + tokenField(u, "thoughtsTokenCount"));
        }
        std::string text2;
        if (j.is_object() && j.contains("candidates") && j["candidates"].is_array() && !j["candidates"].empty()) {
            const json& c = j["candidates"][0];
            if (c.is_object() && c.contains("content") && c["content"].is_object() && c["content"].contains("parts") &&
                c["content"]["parts"].is_array())
                for (const json& part : c["content"]["parts"])
                    if (part.is_object() && !part.value("thought", false) && part.contains("text") && part["text"].is_string())
                        text2 += part["text"].get<std::string>();
        }
        tlRaw = text2;
        if (!extractAnswer(text2, source, out, st.multiLines)) {
            why = kFailRejected;
            err = trimText(text2).empty() ? "Gemini gave no answer (it may have blocked the line)"
                                          : "Gemini explained instead of translating";
            return false;
        }
        return true;
    }
    return false;
}

// OpenAI's Responses API. `reasoning`: 0 asks for none (fastest), 1 for "low", 2 leaves it out; it
// moves up when a model refuses the setting.
bool openai(Http& http, const TranslateSetup& st, const std::string& instructions, const std::string& source,
            const std::string& text, int timeoutMs, std::string& out, std::string& err, int& why, int& reasoning) {
    why = kFailOther;
    if (st.openaiKey.empty()) { err = "Add your OpenAI API key"; return false; }
    for (int attempt = 0; attempt < 3; ++attempt) {
        json req = {{"model", st.openaiModel.empty() ? "gpt-6-luna" : st.openaiModel},
                    {"instructions", instructions},
                    {"input", text},
                    {"max_output_tokens", 1024},
                    {"store", false}};  // nothing kept on OpenAI's side for later
        if (reasoning < 2) req["reasoning"] = {{"effort", reasoning == 0 ? "none" : "low"}};
        const std::string payload = req.dump();
        HttpRequest r;
        r.url = "https://api.openai.com/v1/responses";
        r.headers = {"Authorization: Bearer " + st.openaiKey, "Content-Type: application/json"};
        r.body = &payload;
        r.timeoutMs = timeoutMs;
        long status = 0;
        std::string body;
        if (!http.perform(r, status, body, err)) return false;
        json j = json::parse(body, nullptr, false);
        if (status != 200) {
            const std::string msg = errorMessage(j), low = lowerCopy(msg);
            std::string code;
            if (j.is_object() && j.contains("error") && j["error"].is_object() && j["error"].contains("code") &&
                j["error"]["code"].is_string())
                code = j["error"]["code"].get<std::string>();
            if (status == 400 && reasoning < 2 && (low.find("reasoning") != std::string::npos || low.find("effort") != std::string::npos)) {
                ++reasoning;  // this model doesn't take that reasoning setting: try the next one
                continue;
            }
            if (code == "insufficient_quota" || low.find("quota") != std::string::npos) {
                why = kFailDead;
                err = "OpenAI: no credit left on the account (add credits in Billing)";
            } else if (status == 404 || code == "model_not_found") {
                why = kFailDead;
                err = "OpenAI: your plan can't use " + (st.openaiModel.empty() ? std::string("gpt-6-luna") : st.openaiModel) +
                      " (adding credit usually unlocks it)";
            } else {
                if (status == 401 || status == 403) why = kFailDead;
                else if (status == 429) why = kFailLimited;
                else if (status == 503 || status == 500) why = kFailBusy;
                err = httpProblem("OpenAI", status, msg);
            }
            return false;
        }
        if (j.is_object() && j.contains("usage"))  // output_tokens includes any reasoning
            countTokens(2, tokenField(j["usage"], "input_tokens"), tokenField(j["usage"], "output_tokens"));
        std::string text2;
        bool refused = false;
        if (j.is_object() && j.contains("output") && j["output"].is_array())
            for (const json& item : j["output"]) {
                if (!item.is_object() || item.value("type", "") != "message" || !item.contains("content") ||
                    !item["content"].is_array())
                    continue;
                for (const json& part : item["content"]) {
                    if (!part.is_object()) continue;
                    const std::string type = part.value("type", "");
                    if (type == "output_text" && part.contains("text") && part["text"].is_string())
                        text2 += part["text"].get<std::string>();
                    else if (type == "refusal")
                        refused = true;
                }
            }
        tlRaw = text2;
        if (!extractAnswer(text2, source, out, st.multiLines)) {
            why = kFailRejected;
            err = refused || trimText(text2).empty() ? "OpenAI gave no answer (it may have refused the line)"
                                                     : "OpenAI explained instead of translating";
            return false;
        }
        return true;
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------- regional varieties

namespace {
const Variety kVarieties[] = {
    {"es", "es-MX", "Mexico", "Mexican Spanish, as spoken in Mexico", "ES-419"},
    {"es", "es-ES", "Spain", "Spanish as spoken in Spain (Peninsular Spanish, with vosotros)", "ES-ES"},
    {"es", "es-AR", "Argentina (Rioplatense)", "Rioplatense Spanish, as spoken in Argentina and Uruguay (with voseo)", "ES-419"},
    {"es", "es-CO", "Colombia", "Colombian Spanish, as spoken in Colombia", "ES-419"},
    {"es", "es-PR", "Puerto Rico", "Puerto Rican Spanish, as spoken in Puerto Rico", "ES-419"},
    {"es", "es-DO", "Dominican Republic", "Dominican Spanish, as spoken in the Dominican Republic", "ES-419"},
    {"es", "es-CU", "Cuba", "Cuban Spanish, as spoken in Cuba", "ES-419"},
    {"es", "es-CL", "Chile", "Chilean Spanish, as spoken in Chile", "ES-419"},
    {"es", "es-PE", "Peru", "Peruvian Spanish, as spoken in Peru", "ES-419"},
    {"es", "es-VE", "Venezuela", "Venezuelan Spanish, as spoken in Venezuela", "ES-419"},
    {"es", "es-US", "US Latino", "the Spanish of Latinos in the United States (some English mixed in, as they really talk)", "ES-419"},
    {"es", "es-419", "Latin America (neutral)", "neutral Latin American Spanish that people from any Latin American country find natural", "ES-419"},
    {"pt", "pt-BR", "Brazil", "Brazilian Portuguese, as spoken in Brazil", "PT-BR"},
    {"pt", "pt-PT", "Portugal", "European Portuguese, as spoken in Portugal", "PT-PT"},
    {"en", "en-US", "United States", "American English", "EN-US"},
    {"en", "en-GB", "United Kingdom", "British English, as spoken in England", "EN-GB"},
    {"en", "en-AU", "Australia", "Australian English", "EN-GB"},
    {"en", "en-CA", "Canada", "Canadian English", "EN-US"},
    {"en", "en-IE", "Ireland", "Irish English, as spoken in Ireland", "EN-GB"},
    {"en", "en-SCT", "Scotland", "Scottish English, as spoken in Scotland", "EN-GB"},
    {"en", "en-IN", "India", "Indian English, as spoken in India", "EN-GB"},
    {"fr", "fr-FR", "France", "French as spoken in France", "FR"},
    {"fr", "fr-CA", "Quebec", "Quebec French, as spoken in Quebec", "FR-CA"},
    {"fr", "fr-BE", "Belgium", "Belgian French, as spoken in Belgium", "FR"},
    {"fr", "fr-CH", "Switzerland", "Swiss French, as spoken in French-speaking Switzerland", "FR"},
    {"fr", "fr-AF", "West Africa", "West African French, as spoken in Côte d'Ivoire and Senegal", "FR"},
    {"de", "de-DE", "Germany", "German as spoken in Germany", "DE"},
    {"de", "de-AT", "Austria", "Austrian German, as spoken in Austria", "DE"},
    {"de", "de-CH", "Switzerland (Standard German)", "Swiss Standard German (Hochdeutsch as written and spoken in Switzerland, with Swiss words and no ß)", "DE-CH"},
    {"de", "gsw", "Swiss German (Schweizerdeutsch)", "Swiss German dialect (Schweizerdeutsch, Zurich German), written the way Swiss people text it", ""},
    {"de", "de-BY", "Bavaria", "German with a Bavarian flavour, as spoken in Bavaria", "DE"},
    {"it", "it-IT", "Italy (standard)", "everyday Italian as spoken in Italy", "IT"},
    {"it", "it-RM", "Rome", "Italian as spoken in Rome, with Roman expressions", "IT"},
    {"it", "it-NA", "Naples", "Italian as spoken in Naples, with Neapolitan expressions", "IT"},
    {"nl", "nl-NL", "Netherlands", "Dutch as spoken in the Netherlands", "NL"},
    {"nl", "nl-BE", "Belgium (Flemish)", "Flemish, the Dutch spoken in Belgium", "NL"},
    {"ar", "ar", "Modern Standard", "Modern Standard Arabic (fusha)", "AR"},
    {"ar", "ar-KW", "Kuwait", "Kuwaiti Arabic, the dialect spoken in Kuwait", ""},
    {"ar", "ar-SA", "Saudi Arabia", "Saudi Arabic (Najdi and Hejazi everyday speech), as spoken in Saudi Arabia", ""},
    {"ar", "ar-AE", "UAE / Gulf", "Emirati and general Gulf Arabic (Khaleeji), as spoken in the UAE", ""},
    {"ar", "ar-QA", "Qatar / Bahrain", "Gulf Arabic as spoken in Qatar and Bahrain", ""},
    {"ar", "ar-IQ", "Iraq", "Iraqi Arabic, as spoken in Baghdad", ""},
    {"ar", "ar-EG", "Egypt", "Egyptian Arabic, as spoken in Cairo", ""},
    {"ar", "ar-LEV", "Levant (Syria, Lebanon, Jordan, Palestine)", "Levantine Arabic, as spoken in Syria, Lebanon, Jordan and Palestine", ""},
    {"ar", "ar-MA", "Morocco (Darija)", "Moroccan Arabic (Darija)", ""},
    {"ar", "ar-DZ", "Algeria", "Algerian Arabic (Darja)", ""},
    {"ar", "ar-TN", "Tunisia", "Tunisian Arabic (Derja)", ""},
    {"zh", "zh-CN", "Mainland China", "Mandarin Chinese as spoken in mainland China, in simplified characters", "ZH-HANS"},
    {"zh", "zh-TW", "Taiwan", "Taiwanese Mandarin, as spoken in Taiwan, in traditional characters", "ZH-HANT"},
    {"zh", "zh-SG", "Singapore / Malaysia", "Mandarin as spoken in Singapore and Malaysia, in simplified characters", "ZH-HANS"},
    {"ja", "ja-TK", "Tokyo (standard)", "standard Tokyo Japanese", "JA"},
    {"ja", "ja-KS", "Kansai (Osaka)", "Kansai dialect (Kansai-ben), as spoken in Osaka", "JA"},
    {"ja", "ja-HK", "Hakata (Fukuoka)", "Hakata dialect, as spoken in Fukuoka", "JA"},
    {"ko", "ko-SE", "Seoul (standard)", "standard Seoul Korean", "KO"},
    {"ko", "ko-GS", "Gyeongsang (Busan, Daegu)", "Gyeongsang dialect (satoori), as spoken in Busan and Daegu", "KO"},
    {"ko", "ko-JL", "Jeolla (Gwangju)", "Jeolla dialect (satoori), as spoken in Gwangju", "KO"},
    {"hi", "hi-IN", "Hindi", "everyday Hindi", "HI"},
    {"hi", "hi-EN", "Hinglish", "Hinglish, the everyday mix of Hindi and English that young Indians speak, written in Devanagari for the Hindi words", "HI"},
    {"fil", "fil-PH", "Filipino", "everyday Filipino (Tagalog)", ""},
    {"fil", "fil-EN", "Taglish", "Taglish, the everyday mix of Tagalog and English spoken in Manila", ""},
    {"ru", "ru-RU", "Russia", "Russian as spoken in Russia", "RU"},
    {"ru", "ru-UA", "Ukraine / Belarus", "Russian as spoken in Ukraine and Belarus", "RU"},
};
}  // namespace

std::vector<const Variety*> varietiesOf(const std::string& langCode) {
    std::vector<const Variety*> out;
    for (const Variety& v : kVarieties)
        if (langCode == v.lang) out.push_back(&v);
    return out;
}

const Variety* findVariety(const std::string& id) {
    for (const Variety& v : kVarieties)
        if (id == v.id) return &v;
    return nullptr;
}

namespace {
// What casual speech actually looks like, for languages where the models tend to stay textbook.
std::string casualHint(const std::string& code) {
    static const std::pair<const char*, const char*> kHints[] = {
        {"es", "tú (or vos where the dialect uses it), dropped subject pronouns, short forms like pa' and the dialect's "
               "everyday fillers and slang"},
        {"pt", "a gente instead of nós, tá, tô, cê, pra, and everyday fillers like tipo and né"},
        {"fr", "drop the ne in negations, on instead of nous, t'es, y'a, ouais, and everyday words like trop and grave"},
        {"de", "du, dropped endings (hab, ist's), ne instead of eine, and fillers like halt, echt and krass"},
        {"it", "tu, and everyday fillers like tipo, cioè, dai and boh"},
        {"ja", "タメ口: no です/ます, contractions like じゃん and てる, and particles like よ, ね, な at the end"},
        {"ko", "반말: no 요, endings like -야, -어, -냐, -지, and words friends really use"},
        {"zh", "no 您, particles like 啊, 吧, 呢, 嘛, and short everyday words"},
        {"yue", "everyday spoken Cantonese with particles like 啦, 喎, 囉, not written Chinese"},
        {"ru", "ты, short forms like щас, чё, ну, короче, and everyday slang"},
        {"uk", "ти, short everyday forms and fillers like ну, короче"},
        {"pl", "ty, and everyday words like spoko, no, serio"},
        {"nl", "je instead of u, and everyday words like gewoon, echt, man"},
        {"tr", "sen, and everyday words like ya, kanka, falan"},
        {"ar", "the spoken dialect's everyday words and grammar instead of fusha (unless Modern Standard was chosen)"},
        {"id", "aku/kamu or gue/lo, nggak, udah, banget, and particles like sih, dong, kok"},
        {"ms", "aku/kau, tak, dah, and particles like lah, kot"},
        {"fil", "no po, ho or opo; spoken Manila Tagalog, not the deep bookish kind (pero, kasi, pwede, tsaka, 'yung, 'di: never ngunit, subalit, sapagkat, maaari, at saka), with particles like naman, eh, nga, and only the few English words Filipinos use without thinking (okay, sorry, game); everything else in Tagalog, never the speaker's own English phrases"},
        {"hi", "tu or tum (never aap), words like yaar, and only the few English words people use without thinking (okay, sorry, phone); everything else in Hindi, never the speaker's own English phrases"},
        {"vi", "pronouns friends use (tao/mày between close friends, mình/bạn otherwise) and particles like nhé, nha, á"},
        {"th", "pronouns friends use (เรา, แก, มึง/กู only for very close friends) and particles like อะ, ว่ะ, นะ"},
        {"en", "contractions, dropped words, and everyday slang and fillers like gonna, kinda, like, ya know"},
    };
    for (const auto& h : kHints)
        if (code == h.first) return h.second;
    return {};
}
}  // namespace

namespace {
// For a model on this PC: each language's informal and polite level in a few concrete words, so a
// small model has the forms of this language in front of it and not a list for every language.
struct LocalLevel { const char* code; const char* casual; const char* polite; };
const LocalLevel kLocalLevels[] = {
    {"ko", "반말 (해체): endings like -아/어, -야, -지, -네; never -요 and never -습니다/-ㅂ니다",
           "해요체: sentences end in -요; never 반말, and not the stiff -습니다"},
    {"ja", "タメ口: plain forms (だ, する, ～ない, ～た); never です/ます",
           "です/ます on every sentence; never plain タメ口, and no heavy keigo"},
    {"es", "tú (or vos where the dialect uses it); never usted", "usted; never tú or vos"},
    {"fr", "tu; never vous for one person", "vous; never tu"},
    {"de", "du; never Sie", "Sie; never du"},
    {"it", "tu; never Lei", "Lei; never tu"},
    {"ru", "ты; never вы for one person", "вы; never ты"},
    {"uk", "ти; never ви for one person", "ви; never ти"},
    {"pl", "ty and second-person verbs; never Pan/Pani", "Pan/Pani; never ty"},
    {"nl", "je/jij; never u", "u; never je/jij"},
    {"tr", "sen; never siz for one person", "siz; never sen"},
    {"zh", "你; never 您", "您 where it fits, with polite wording; no slang"},
    {"hi", "तुम (or तू between close friends); never आप", "आप; never तुम or तू"},
    {"id", "kamu or lo, aku or gue, short spoken forms (nggak, udah); never Anda", "Anda or kak, saya; never lo/gue"},
    {"fil", "no po and no ho, ikaw/ka; the everyday spoken forms", "with po and ho, and kayo for \"you\""},
    {"th", "no ครับ/ค่ะ/คะ at the end of sentences; friends' pronouns", "sentences end in ครับ (a man speaking) or ค่ะ/คะ (a woman speaking)"},
};
const LocalLevel* localLevel(const std::string& code) {
    for (const LocalLevel& l : kLocalLevels)
        if (code == l.code) return &l;
    return nullptr;
}
}  // namespace

std::string Translator::aiInstructions(const TranslateSetup& st, const Language* from, const Language& to) {
    const std::string target = to.name;
    const std::string source = from ? std::string(from->name) : std::string("another language");
    std::string s;
    std::string genders;  // only for languages whose words change with it
    if (st.faithful) {
        if (st.speaker == kGenderMan || st.speaker == kGenderWoman)
            genders = std::string("\nThe person speaking sounds like a ") + (st.speaker == kGenderMan ? "man" : "woman") +
                      " (a guess from their voice, it may be wrong). Where " + target +
                      " needs the speaker's gender (words about themselves), use that.";
    } else {
        if (st.speaker == kGenderMan || st.speaker == kGenderWoman)
            genders += std::string("- The speaker is a ") + (st.speaker == kGenderMan ? "man" : "woman") +
                       ": wherever the language marks the speaker's gender (adjectives, participles, verb forms about "
                       "themselves), use the " + (st.speaker == kGenderMan ? "masculine" : "feminine") + " forms.\n";
        else if (st.speaker == kGenderNonbinary)
            genders += "- The speaker is nonbinary: where the language marks the speaker's gender, choose wording that "
                       "avoids it if that sounds natural; otherwise use the most neutral natural option.\n";
        if (st.listener == kListenerMan || st.listener == kListenerWoman)
            genders += std::string("- They are talking to a ") + (st.listener == kListenerMan ? "man" : "woman") +
                       ": use the " + (st.listener == kListenerMan ? "masculine" : "feminine") +
                       " forms for \"you\" and for words about the listener, where the language has them.\n";
        else if (st.listener == kListenerGroup)
            genders += "- They are talking to a group: use the plural forms for \"you\" (and, where gender is marked, "
                       "the usual form for a mixed group).\n";
        else
            genders += "- You don't know who they are talking to: where the language marks the listener's gender, prefer "
                       "wording that avoids it; if that would sound unnatural, use the language's usual default.\n";
    }
    if (st.faithful) {
        s = "You translate what another player said in live voice chat (for example in VRChat or a game) into natural " +
            target + ", for the listener to read.\n"
            "The text is a speech-to-text transcript of spoken " + source + ". It may contain recognition mistakes, slang, "
            "abbreviations and filler words: translate what the person meant. Turn slang into the equivalent everyday " +
            target + ", and keep the tone (jokes, sarcasm, excitement, swearing).\n"
            "If the transcript is garbled, unclear or cut off, translate your best guess of what was said (or the words as "
            "they are). Never explain, comment, give alternatives or talk about the transcript." + genders;
        if (!st.wordNote.empty()) s += "\n" + st.wordNote;
        if (!st.context.empty())
            s += "\n\nEarlier lines of the conversation, for context only (don't translate them):\n" + st.context;
        if (!st.before.empty()) {
            s += "\n\nThis line goes on from what the speaker said just before (already translated; for context only, don't "
                 "translate it again):\n";
            for (const auto& b : st.before) s += "- " + b.first + "\n  (translated: " + b.second + ")\n";
        }
        if (st.multiLines > 0)
            return s + "\n\nThe text is " + std::to_string(st.multiLines) + " numbered lines said one after another in the voice chat, "
                       "maybe by different people and in different languages. A [man] or [woman] before a line is a guess "
                       "from that voice, for words that change with the speaker's gender; don't translate it. Translate each "
                       "line on its own into " + target + "; a line already in " + target + " stays as it is. Answer with "
                       "each line's translation in its own tag: <l1>...</l1>, <l2>...</l2> and so on, one for every line, "
                       "and nothing else. If you won't translate one of the lines, put exactly <skip/> in its tag: never a "
                       "refusal or an explanation, in any language, because whatever you write is shown as that person's words.";
        if (st.readGender)
            return s + "\n\nAnswer with only the translation, inside <line></line>. Then, only if the speaker's own words in "
                       "the original clearly show their gender (like Spanish \"estoy cansada\", or \"I'm a girl\"), add "
                       "<g>f</g> or <g>m</g>. Nothing else outside the tags." + kSkipRule;
        return s + "\n\nAnswer with only the translation, inside <line></line>, and nothing outside it." + kSkipRule;
    }
    // A dialect replaces the language name everywhere below ("Kuwaiti Arabic, the dialect spoken in Kuwait").
    const std::string spoken = st.variety.empty() ? target : st.variety;
    const std::string who = st.persona.empty() ? "a native speaker in their 20s whose everyday language is " + spoken
                                               : st.persona + ", a native speaker whose everyday language is " + spoken;
    const std::string dialect =
        st.variety.empty() ? std::string()
                           : "- Speak that variety, not the textbook standard: its own everyday words, expressions and grammar, "
                             "written the way people from there normally type it.\n";
    // The style is the part people notice most, and models drift back to neat, polite sentences
    // unless it is spelled out: who you're talking to, what that sounds like in this language, what
    // is not allowed, examples, and a last check right before the answer. A neatly worded original
    // must not pull the result back to polite: the setting decides, not the source sentence.
    // Arabic with no dialect picked: one widely understood spoken Arabic, never a mix. Left to itself a
    // model jumps between Egyptian, Gulf and Levantine words in one line, which sounds wrong to everyone.
    // Taglish and Hinglish are a mix of the language and English on purpose. The usual rules ("every
    // word in Filipino", "never the speaker's English") say the opposite, and a model handed both
    // stops to work out which one wins: slower, and the line comes out unsure of itself.
    const bool mixed = st.variety.find("mix of") != std::string::npos;
    const std::string hint = to.code == std::string("ar") && st.variety.empty()
        ? "a neutral, widely understood spoken Arabic (close to the \"white dialect\", اللهجة البيضاء): everyday words "
          "most Arabs understand, no fusha formality, and never mix words from different dialects in one line"
        : mixed ? std::string() : casualHint(to.code);
    const bool custom = st.level == kLevelCustom && !trimText(st.customStyle).empty();
    const bool polite = !custom && st.level == kLevelPolite;
    const bool veryCasual = !custom && st.level == kLevelVeryCasual;
    const bool casual = !custom && !polite;  // Casual and Very casual
    std::string level, check;
    if (custom) {
        level = "HOW TO SOUND (the most important rule): exactly the way they describe it: \"" + trimText(st.customStyle) +
                "\". Follow that closely; it matters more than sounding neutral, and more than how the original was worded. "
                "Their description changes how it sounds, never what is said: no added sentences, jokes, names or details.";
        check = "Before you answer, check the line against their description (\"" + trimText(st.customStyle) +
                "\"). If it sounds more neutral or more polite than that, rewrite it. Then check you added nothing they didn't say.";
    } else if (polite) {
        level = "HOW TO SOUND: you're talking to someone you just met in a lobby and want to come across as friendly and "
                "respectful. Use the polite speech level (for example Korean 해요체, Japanese です/ます, or vous/usted/Sie/вы "
                "where the language has them). Warm and relaxed, like a friendly stranger in a game: never stiff, never "
                "like written text, and no slang or swearing the original didn't have.";
    } else {
        level = std::string("HOW TO SOUND (the most important rule, it outranks staying close to the original wording): ") +
                (veryCasual
                     ? "VERY CASUAL. You're talking to your closest friend, someone you've known for years, joking around "
                       "with no filter. Talk the loosest way young native speakers talk to their best friend: slang, "
                       "shortened and slurred words, and a rough, joking tone."
                     : "CASUAL. You're talking to a friend you hang out with a lot. Talk the relaxed way friends talk to "
                       "each other: contractions, dropped words, everyday slang and filler words.") +
                "\n  - Do NOT translate the sentence as it is worded. First get what they mean, then say that the way a "
                "native would say it to " + (veryCasual ? "their best friend" : "a friend") + ". A tidy, complete or polite "
                "original does NOT make the result tidy or polite: the style setting decides, not the original's wording.\n"
                "  - Use the informal speech level and informal \"you\" (Korean 반말, Japanese タメ口, tu/tú/du/ты and so on). "
                "Polite and formal forms are forbidden: no 요/습니다 endings, no です/ます, no usted/vous/Sie/вы/您/aap/Anda, no po/opo, no ครับ/ค่ะ, "
                "no polite request formulas (the equivalents of \"could you please\", \"would you mind\", \"I would like "
                "to\", \"excuse me\", \"thank you very much\"), and no full textbook sentences where people would say half of it.\n"
                "  - Shorten it the way speech is shortened: drop subjects, particles and words natives drop, and use the "
                "contracted and slurred forms. Casual comes from HOW the words are said (the forms, the slang, the "
                "shortening), never from extra words put in front or at the end.\n"
                "  - Say ONLY what they said. Loosen the wording, never the content: no added sentences, questions, "
                "greetings, reactions, names, nicknames (\"bro\", \"dude\", \"girl\"), jokes or details, and nothing taken from "
                "the recent conversation. No added openers or filler words either (the equivalents of \"wait\", \"ugh\", "
                "\"hey\", \"man\", \"like\", \"you know\", \"honestly\"): if they didn't say it, you don't. What they "
                "said themselves is theirs: an \"um\" or \"ugh\" they really said may stay. If they said one short thing, "
                "you say one short thing.\n" +
                (veryCasual
                     ? "  - Slang is expected, not optional: wherever an everyday slang word exists for what they mean, use it "
                       "instead of the dictionary word. " +
                       std::string(st.swear == 0 ? "If they swore, swear the same way. " : "Their swearing follows the swear-word rule below. ") +
                       "But NEVER add swear words, insults, crude or sexual words they didn't say: in many languages (Arabic "
                       "especially) a word that sounds \"rough\" in English is a real insult or vulgar there.\n"
                     : "  - Prefer the everyday word over the dictionary word, and use light slang where friends would.\n") +
                (hint.empty() ? std::string()
                             : "  - In " + target + " that means: " + hint +
                                   (st.variety.empty() || st.varietyName.empty()
                                        ? std::string(".\n")
                                        : ", but always with " + st.varietyName + "'s own words and endings wherever they differ "
                                          "from the standard language.\n")) +
                "  - How far to go (shown in English, do the same in " + spoken + "): \"Could you tell me where the mirror "
                "is?\" becomes " + (veryCasual ? "\"where's the mirror at\"" : "\"where's the mirror?\"") +
                "; \"I am very tired today\" becomes " + (veryCasual ? "\"I'm so dead today\"" : "\"I'm so tired today\"") +
                "; \"Thank you very much, that was fun\" becomes " + (veryCasual ? "\"thanks, that was sick\"" : "\"thanks, that was fun\"") +
                "; \"I do not understand\" becomes " + (veryCasual ? "\"don't get it\"" : "\"I don't get it\"") +
                ". Notice what did not happen: no word was added. Same content, looser form.";
        check = std::string("Before you answer, read your line again. Would a native really say this out loud to ") +
                (veryCasual ? "their best friend" : "a friend") + "? If it could be a line from a textbook, a subtitle, "
                "a customer service chat or a polite stranger, it is wrong: rewrite it shorter, looser and more " +
                (veryCasual ? "slangy" : "relaxed") + " in the informal speech level. Then go through your line word by "
                "word against what they said: an opener, a reaction, a filler or a detail they didn't say gets deleted." +
                (st.variety.empty() || mixed
                     ? std::string()
                     : " Then check the dialect: is this really " + (st.varietyName.empty() ? spoken : st.varietyName) +
                           ", the way people from there talk? Wherever it has its own word, ending or spelling, the "
                           "standard " + target + " one is wrong.") +
                " Then answer.";
    }
    // Polite and "My own" get the same two last looks: nothing added, and the dialect really used.
    if (polite || custom) {
        if (polite)
            check = "Before you answer, go through your line word by word against what they said: anything they "
                    "didn't say gets deleted.";
        if (!st.variety.empty() && !mixed)
            check += " Then check the dialect: is this really " + (st.varietyName.empty() ? spoken : st.varietyName) +
                     ", the way people from there talk? Wherever it has its own word, ending or spelling, the standard " +
                     target + " one is wrong.";
        check += " Then answer.";
    }
    const std::string keep =
        st.swear == 1 ? "- Keep the meaning and the attitude (jokes, sarcasm, excitement), but say any swear word or insult "
                        "with a milder word that keeps the feeling. Don't add new information or explain anything.\n"
      : st.swear == 2 ? "- Keep the meaning and the attitude (jokes, sarcasm, excitement), but say the word \"beep\" instead "
                        "of each swear word. Don't add new information or explain anything.\n"
      : casual        ? "- Keep what they mean and their attitude (jokes, sarcasm, excitement, swearing). Don't add new "
                        "information, don't explain, don't soften or censor. The wording is yours to loosen; the meaning is not.\n"
                      : "- Keep the meaning and the attitude (jokes, sarcasm, excitement, swearing). Don't add, explain, soften "
                        "or censor anything, and keep it about as long as the original.\n";
    s = "You are the voice of a person in live voice chat (for example in VRChat or a game). They said something in " +
        source + "; you say it for them in " + spoken + ".\n"
        "Write exactly what " + who + " would actually say out loud here.\n"
        "- " + level + "\n" + dialect + genders + keep + st.wordNote +
        "- A text-to-speech voice will speak it: write only spoken words. No emoji, hashtags, texting abbreviations or "
        "laughter written as letters (no ㅋㅋ, lol, www, jaja, хаха), no quotation marks, no notes.\n" +
        (mixed ? "- Mix in English the way people who speak " + spoken.substr(0, spoken.find(',')) + " really do: the everyday "
                 "English words and short phrases they switch to without thinking, with the rest in " + target + ". Names "
                 "stay exactly as they are: people, places, games, apps, brands and products (like VRChat, Discord, "
                 "ElevenLabs, Claude), and any words you were told to keep.\n"
               : "- Every word you say must be " + target + ". Don't keep words or phrases from the original language just "
                 "because they were there: someone who only speaks " + target +
                 " has to understand all of it. Names stay exactly as they are: people, places, games, apps, brands and "
                 "products (like VRChat, Discord, ElevenLabs, Claude), and any words you were told to keep.\n") +
        "- If it is already in " + spoken + ", just make it sound natural in that style.\n"
        "- If what they said is garbled or unclear, say your best guess of what they meant. Never explain or comment.\n"
        "- If there is a word you won't say, just leave that word out and translate the rest as it is. Never replace "
        "the line with a different, friendlier message: saying something the person never said is worse than a gap.\n";
    if (!st.context.empty())
        s += std::string("\nRecent conversation, for context only: don't translate it, don't answer it and don't take anything "
             "from it into your line. Use it only to understand what's being talked about and to reuse the words the "
             "others use. ") +
             (casual ? "Keep the style described above even if the others talk politely or formally: never become more "
                       "polite because of them. If they are even more casual than you, you may match them:\n"
              : polite ? "Keep the style described above: if the others are clearly more casual with you, you may move one "
                         "small step toward them, never further:\n"
                       : "Keep the style described above:\n") +
             st.context;
    if (!st.before.empty()) {
        // A story or a long explanation, sentence by sentence: the line before, so this one fits on.
        s += "\nThis line goes on from what they said just before, which you already said for them (for context only). "
             "Don't say it again and don't add to it: say only the new line, so it follows on naturally, with the same "
             "words for the same things and the same style:\n";
        for (const auto& b : st.before) s += "- They said: " + b.first + "\n  You said: " + b.second + "\n";
    }
    if (!check.empty()) s += "\n" + check;
    // A model on this PC is small: it reads the long description above and still slides back to the
    // standard language and a neutral, polite tone. For it only, the two things that go wrong are said
    // once more at the very end, short and concrete for this language, as rules with no way around them.
    if (st.service == kLocal && !custom) {
        const LocalLevel* ll = localLevel(to.code);
        s += "\n\nSTRICT RULES. Your line is wrong if it breaks one of them:\n";
        if (polite)
            s += "1. Speech level: POLITE, in every sentence" +
                 (ll ? std::string(": ") + ll->polite : std::string(": the polite forms and the polite \"you\"")) +
                 ". Not casual, not slang, and not stiff or official either.\n";
        else
            s += "1. Speech level: INFORMAL, in every sentence" +
                 (ll ? std::string(": ") + ll->casual : std::string(": the informal forms and the informal \"you\"")) +
                 ". Even when the original sentence is polite or formal. One polite form makes the whole line wrong.\n";
        if (!st.variety.empty() && !mixed)
            s += "2. Dialect: " + spoken + ". Wherever it has its own word, ending or spelling, use that one and not the "
                 "standard " + target + " one. A line a newsreader could say in standard " + target + " is wrong.\n";
        else if (mixed)
            s += "2. Language: " + spoken + ", mixed the way people there really talk. Not pure " + target + " and not pure English.\n";
        else
            s += "2. Language: " + target + " only, the everyday spoken kind. No words left in " + source + ".\n";
        s += "3. Same content: nothing added, nothing explained, nothing left out except a word you won't say.\n"
             "4. One answer: the line itself. No second version, no notes, no brackets.";
    }
    return s + "\nAnswer with only the line to say, inside <line></line>, and nothing outside it." + kSkipRule;
}

namespace {

// What each model accepts, learned once and shared by every translator, per model (so a newly
// picked model is checked again). Claude: 0 effort "low", 1 no effort setting. Gemini: 0 thinking
// "minimal", 1 "low", 2 no thinking setting. OpenAI: 0 reasoning "none", 1 "low", 2 no setting.
std::mutex gLearnMx;
std::map<std::string, int> gLearned;

std::string modelKey(const TranslateSetup& st) {
    if (st.service == kLocal) return "local:" + st.localModel;
    return st.service == kClaude   ? "claude:" + (st.claudeModel.empty() ? std::string("claude-haiku-5-5") : st.claudeModel)
           : st.service == kGemini ? "gemini:" + (st.geminiModel.empty() ? std::string("gemini-3.5-flash-lite") : st.geminiModel)
                                   : "openai:" + (st.openaiModel.empty() ? std::string("gpt-6-luna") : st.openaiModel);
}
int learned(const std::string& key) {
    std::lock_guard<std::mutex> lk(gLearnMx);
    const auto it = gLearned.find(key);
    return it == gLearned.end() ? 0 : it->second;
}
void learn(const std::string& key, int level) {
    std::lock_guard<std::mutex> lk(gLearnMx);
    int& x = gLearned[key];
    x = std::max(x, level);
}

// One request to the chosen AI service, on connection `h`.
// A model on this PC, through llama.cpp's server (the same chat format OpenAI-style servers use).
bool localModel(Http& http, const TranslateSetup& st, const std::string& instructions, const std::string& source,
                const std::string& text, int timeoutMs, std::string& out, std::string& err, int& why) {
    why = kFailOther;
    if (st.localUrl.empty()) {  // not loaded (yet): a backup does the line meanwhile
        why = kFailBusy;
        err = st.localNote.empty() ? "The local model isn't running" : st.localNote;
        return false;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        json req = {{"messages", json::array({{{"role", "system"}, {"content", instructions}}, {{"role", "user"}, {"content", text}}})},
                    {"max_tokens", 1024}, {"temperature", 0.25}, {"top_p", 0.9}, {"top_k", 40}, {"stream", false},
                    {"cache_prompt", true}};  // the instructions are the same from line to line: read once, then reused
        if (attempt == 0) req["chat_template_kwargs"] = {{"enable_thinking", false}};  // the line, not a think-through first
        const std::string payload = req.dump();
        HttpRequest r;
        r.url = st.localUrl + "/v1/chat/completions";
        r.headers = {"Authorization: Bearer " + st.localKey, "Content-Type: application/json"};
        r.body = &payload;
        r.timeoutMs = timeoutMs;
        long status = 0;
        std::string body;
        if (!http.perform(r, status, body, err)) {
            if (err != "cancelled" && err != "timed out") {
                why = kFailBusy;
                err = "The local model isn't answering";
            }
            return false;
        }
        const json j = json::parse(body, nullptr, false);
        if (status == 400 && attempt == 0) continue;  // an engine that doesn't know that setting: without it
        if (status != 200) {
            if (status == 503) why = kFailBusy;
            err = status == 503 ? "The local model is still loading" : "The local model answered " + std::to_string(status);
            return false;
        }
        std::string text2;
        if (j.is_object() && j.contains("choices") && j["choices"].is_array() && !j["choices"].empty() &&
            j["choices"][0].is_object() && j["choices"][0].contains("message") && j["choices"][0]["message"].is_object() &&
            j["choices"][0]["message"].contains("content") && j["choices"][0]["message"]["content"].is_string())
            text2 = j["choices"][0]["message"]["content"].get<std::string>();
        tlRaw = text2;
        if (!extractAnswer(text2, source, out, st.multiLines)) {
            why = kFailRejected;
            err = trimText(text2).empty() ? "The local model gave an empty answer" : "The local model explained instead of translating";
            return false;
        }
        return true;
    }
    return false;
}

bool aiCall(Http& h, const TranslateSetup& st, const std::string& instructions, const std::string& source,
            const std::string& text, int ms, std::string& out, std::string& err, int& why) {
    const std::string key = modelKey(st);
    int level = learned(key);
    bool ok;
    if (st.service == kClaude) {
        bool noEffort = level > 0;
        ok = claude(h, st, instructions, source, text, ms, out, err, why, noEffort);
        level = noEffort ? 1 : 0;
    } else if (st.service == kGemini) {
        ok = gemini(h, st, instructions, source, text, ms, out, err, why, level);
    } else if (st.service == kLocal) {
        ok = localModel(h, st, instructions, source, text, ms, out, err, why);
    } else {
        ok = openai(h, st, instructions, source, text, ms, out, err, why, level);
    }
    learn(key, level);
    return ok;
}

// Languages whose lines take clearly longer for an AI to write out than the same thing in English:
// the sentences are longer and each word costs the model more pieces to write.
bool longLanguage(const std::string& code) {
    return code == "fil" || code == "tl" || code == "ceb";
}

// How each AI service is doing, shared by every translator (My voice, Listen, the screen...). They
// all use the same key, so when one hits the plan's limit, all of them should ease off, and when the
// key is refused or out of credit, none of them should keep waiting on it.
struct Health {
    std::chrono::steady_clock::time_point busyUntil{};  // too many requests / overloaded: no extra copies
    std::chrono::steady_clock::time_point deadUntil{};  // refused / no credit: go straight to the backup
    std::string deadWhy;
};
std::mutex gHealthMx;
std::map<std::string, Health> gHealth;
Health healthOf(const std::string& k) {
    std::lock_guard<std::mutex> lk(gHealthMx);
    return gHealth[k];
}
void markBusy(const std::string& k, int seconds) {
    std::lock_guard<std::mutex> lk(gHealthMx);
    auto& h = gHealth[k];
    h.busyUntil = std::max(h.busyUntil, std::chrono::steady_clock::now() + std::chrono::seconds(seconds));
}
void markDead(const std::string& k, const std::string& why) {
    std::lock_guard<std::mutex> lk(gHealthMx);
    auto& h = gHealth[k];
    h.deadUntil = std::chrono::steady_clock::now() + std::chrono::minutes(3);  // then one line tries it again
    h.deadWhy = why;
}
void markAlive(const std::string& k) {
    std::lock_guard<std::mutex> lk(gHealthMx);
    gHealth[k].deadUntil = {};
}

// Which AI, model and key a Health entry is for. Per key: a new key (or one with credit added and
// pasted again) starts fresh. Only the key's last characters, and only in memory.
std::string healthKey(const TranslateSetup& st) {
    if (st.service == kLocal) return modelKey(st);
    const std::string& k = st.service == kClaude ? st.claudeKey : st.service == kGemini ? st.geminiKey : st.openaiKey;
    return modelKey(st) + "|" + (k.size() > 8 ? k.substr(k.size() - 8) : k);
}

int msSince(std::chrono::steady_clock::time_point t) {
    return int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count());
}

}  // namespace

bool Translator::readPicture(const TranslateSetup& st, const std::string& png64, const Language& to,
                             std::vector<std::pair<std::string, std::string>>& lines, std::string& err) {
    lines.clear();
    if (!isOnlineAi(st.service)) { err = "AI reading needs Claude, Gemini or OpenAI as the translation service."; return false; }
    const std::string prompt =
        "This is a picture of a game or app window. Read every line of text people wrote or that the game shows "
        "(chat messages, signs, menus), top to bottom. Keep a chat line whole, with the name in front if there is one. "
        "Translate each line into natural " + std::string(to.name) + ", keeping slang and swearing. If a line is already in " +
        to.name + ", repeat it. Skip single interface words like buttons and numbers. Answer with only JSON: "
        "{\"lines\": [{\"text\": \"as written\", \"translation\": \"...\"}]}";
    json req;
    HttpRequest r;
    std::string payload;
    if (st.service == kClaude) {
        req = {{"model", st.claudeModel.empty() ? "claude-haiku-5-5" : st.claudeModel},
               {"max_tokens", 2048},
               {"messages", json::array({{{"role", "user"},
                                          {"content", json::array({{{"type", "image"},
                                                                    {"source", {{"type", "base64"}, {"media_type", "image/png"}, {"data", png64}}}},
                                                                   {{"type", "text"}, {"text", prompt}}})}}})}};
        r.url = "https://api.anthropic.com/v1/messages";
        r.headers = {"x-api-key: " + st.claudeKey, "anthropic-version: 2023-06-01", "content-type: application/json"};
    } else if (st.service == kGemini) {
        req = {{"contents", json::array({{{"role", "user"},
                                          {"parts", json::array({{{"inline_data", {{"mime_type", "image/png"}, {"data", png64}}}},
                                                                 {{"text", prompt}}})}}})},
               {"generationConfig", {{"maxOutputTokens", 2048}, {"responseMimeType", "application/json"}}}};
        r.url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                (st.geminiModel.empty() ? std::string("gemini-3.5-flash-lite") : st.geminiModel) + ":generateContent";
        r.headers = {"x-goog-api-key: " + st.geminiKey, "Content-Type: application/json"};
    } else {
        req = {{"model", st.openaiModel.empty() ? "gpt-6-luna" : st.openaiModel},
               {"input", json::array({{{"role", "user"},
                                       {"content", json::array({{{"type", "input_text"}, {"text", prompt}},
                                                                {{"type", "input_image"}, {"image_url", "data:image/png;base64," + png64}}})}}})},
               {"max_output_tokens", 2048},
               {"store", false}};
        const int reasoning = learned(modelKey(st));
        if (reasoning < 2) req["reasoning"] = {{"effort", reasoning == 0 ? "none" : "low"}};
        r.url = "https://api.openai.com/v1/responses";
        r.headers = {"Authorization: Bearer " + st.openaiKey, "Content-Type: application/json"};
    }
    payload = req.dump();
    r.body = &payload;
    r.timeoutSec = 40;
    long status = 0;
    std::string body;
    if (!http.perform(r, status, body, err)) return false;
    const json j = json::parse(body, nullptr, false);
    const char* name = st.service == kClaude ? "Claude" : st.service == kGemini ? "Gemini" : "OpenAI";
    if (status != 200) { err = httpProblem(name, status, errorMessage(j)); return false; }
    std::string text;
    if (st.service == kClaude) {
        if (j.contains("usage")) countTokens(0, tokenField(j["usage"], "input_tokens"), tokenField(j["usage"], "output_tokens"));
        if (j.contains("content") && j["content"].is_array())
            for (const json& b : j["content"])
                if (b.is_object() && b.value("type", "") == "text") text += b.value("text", "");
    } else if (st.service == kGemini) {
        if (j.contains("usageMetadata"))
            countTokens(1, tokenField(j["usageMetadata"], "promptTokenCount"),
                        tokenField(j["usageMetadata"], "candidatesTokenCount") + tokenField(j["usageMetadata"], "thoughtsTokenCount"));
        if (j.contains("candidates") && j["candidates"].is_array() && !j["candidates"].empty()) {
            const json& c = j["candidates"][0];
            if (c.contains("content") && c["content"].contains("parts") && c["content"]["parts"].is_array())
                for (const json& part : c["content"]["parts"])
                    if (part.is_object() && !part.value("thought", false)) text += part.value("text", "");
        }
    } else {
        if (j.contains("usage")) countTokens(2, tokenField(j["usage"], "input_tokens"), tokenField(j["usage"], "output_tokens"));
        if (j.contains("output") && j["output"].is_array())
            for (const json& item : j["output"])
                if (item.is_object() && item.value("type", "") == "message" && item.contains("content") && item["content"].is_array())
                    for (const json& part : item["content"])
                        if (part.is_object() && part.value("type", "") == "output_text") text += part.value("text", "");
    }
    const size_t a = text.find('{'), b = text.rfind('}');
    const json out = a == std::string::npos || b == std::string::npos ? json() : json::parse(text.substr(a, b - a + 1), nullptr, false);
    if (!out.is_object() || !out.contains("lines") || !out["lines"].is_array()) {
        err = std::string(name) + " didn't read the picture";
        return false;
    }
    for (const json& l : out["lines"])
        if (l.is_object() && l.contains("text") && l["text"].is_string() && !l["text"].get<std::string>().empty())
            lines.emplace_back(l["text"].get<std::string>(), l.value("translation", std::string()));
    return true;
}

bool Translator::romanize(const TranslateSetup& st, const std::string& text, const std::string& language, std::string& out,
                          std::string& err) {
    if (!isAiService(st.service)) { err = "Needs Claude, Gemini or OpenAI"; return false; }
    const std::string instructions =
        "Write how to read this " + (language.empty() ? std::string("text") : language + " text") +
        " in Latin letters, for a learner: Hepburn romaji for Japanese, pinyin with tone marks for Chinese, Jyutping for "
        "Cantonese, Revised Romanization for Korean, a simple common transliteration for Arabic, Russian, Hindi, Thai and "
        "others. Keep names and punctuation. Don't translate or explain. Answer with only the reading, inside <line></line>.";
    int why = kFailOther;
    const int ms = std::max(4000, st.timeoutMs * 2);
    return aiCall(http, st, instructions, text, text, ms, out, err, why);
}

void Translator::warm(const TranslateSetup& st) {
    // Opens (or checks) each connection a line will use, all at once, while the person is still
    // talking. A connection that doesn't answer in 3 s is dropped, and the real request opens a new one.
    auto one = [](Http& h, const std::string& url) {
        if (h.idleSeconds() < 15.0) return;
        HttpRequest r;
        r.url = url;
        r.headOnly = true;  // just opens the connection, so the real request skips that step
        r.timeoutSec = 3;
        long status = 0;
        std::string body, err;
        h.perform(r, status, body, err);
    };
    // The backup a slow line goes to first: DeepL, else Microsoft, else Free.
    const std::string msUrl = "https://api.cognitive.microsofttranslator.com/languages?api-version=3.0";
    const std::string deeplOrFreeUrl = !st.deeplKey.empty() ? deeplHost(st.deeplKey) + "/v2/languages"
                                       : !st.msKey.empty()  ? msUrl
                                                            : std::string("https://api.mymemory.translated.net/");
    if (!isAiService(st.service)) {
        if (st.service == kDeepL && st.deeplKey.empty()) return;
        if (st.service == kMicrosoft && st.msKey.empty()) return;
        one(http, st.service == kMicrosoft ? msUrl : st.service == kMyMemory ? std::string("https://api.mymemory.translated.net/") : deeplOrFreeUrl);
        return;
    }
    if (st.service == kLocal) {  // on this PC: no connection to get ready, only the backup's
        one(backupHttp_, deeplOrFreeUrl);
        return;
    }
    const std::string aiUrl = st.service == kClaude   ? "https://api.anthropic.com/v1/messages"
                              : st.service == kGemini ? "https://generativelanguage.googleapis.com/v1beta/models"
                                                      : "https://api.openai.com/v1/models";
    auto a = std::async(std::launch::async, [&] { one(spare_, aiUrl); });
    auto b = std::async(std::launch::async, [&] { one(backupHttp_, deeplOrFreeUrl); });
    auto c = std::async(std::launch::async, [&] { one(wordsHttp_, deeplOrFreeUrl); });  // listed or swear words
    one(http, aiUrl);
    a.get();
    b.get();
    c.get();
}

bool Translator::translate(const TranslateSetup& st, const std::string& text, const Language* from, const Language& to,
                           std::string& out, std::string& err) {
    via.clear();
    // DeepL's own context: the sentence just before, so it reads this one the same way (not translated,
    // not billed). Only for this call.
    std::string beforeText;
    for (const auto& b : st.before) beforeText += (beforeText.empty() ? "" : " ") + b.first;
    tlContext = beforeText;
    struct ClearContext { ~ClearContext() { tlContext.clear(); } } clearContext;
    tlFormality = st.faithful || st.level == kLevelCustom ? "" : st.level == kLevelPolite ? "prefer_more" : "prefer_less";
    // Free has to be told the spoken language. When it's set to automatic and nothing reported it,
    // a guess from the words themselves (the AI and DeepL work it out on their own).
    const Language* freeFrom = from ? from : findLanguage(guessLanguage(text));
    // DeepL, then Microsoft Translator (whichever have keys), then Free (unless withFree is false).
    // A problem with one doesn't stop the next; the first one's problem is kept for the message.
    auto deeplOrFree = [&](bool deeplFirst, bool withFree = true) {
        std::string problem;
        if (deeplFirst && !st.deeplKey.empty()) {
            bool refused = false;
            if (deepl(http, st.deeplKey, text, from, to, st.deeplVariant, out, err, refused)) { via = "DeepL"; return true; }
            if (!refused) problem = err;
        }
        if (deeplFirst && !st.msKey.empty()) {
            bool refused = false;
            if (microsoft(http, st.msKey, st.msRegion, text, from, to, out, err, refused)) { via = "Microsoft"; return true; }
            if (!refused && problem.empty()) problem = err;
        }
        if (!withFree) {
            if (!problem.empty()) err = problem;
            return false;
        }
        if (!freeFrom) {
            err = problem.empty() ? "The free service needs to know the spoken language. Pick it, or add a DeepL key." : problem;
            return false;
        }
        if (myMemory(http, text, *freeFrom, to, out, err)) {
            via = problem.empty() ? "Free" : "Free (" + problem + ")";
            return true;
        }
        if (!problem.empty()) err = problem + "; Free: " + err;
        return false;
    };

    if (isAiService(st.service)) {
        const char* name = st.service == kClaude ? "Claude" : st.service == kGemini ? "Gemini" : st.service == kLocal ? "Local" : "OpenAI";
        // Several lines in one request (translateLines): only an AI can answer that. When it can't,
        // this returns false and translateLines does each line on its own, with every backup.
        const bool multi = st.multiLines > 0;
        const std::string instructions = aiInstructions(st, from, to);
        // A dialect takes the model more work: allow it half again as long (up to 1.5 s more). On top of
        // that, what this language has been found to need (see extraMs_).
        // This AI's shared health (see Health), per key too: a new key (or one with credit added and
        // pasted again) starts fresh. Only the key's last characters, and only in memory.
        const std::string hk = healthKey(st);
        // Something quick to fall back on if the AI is late: DeepL, Microsoft, or Free while it has some
        // left today.
        const bool backupReady = !st.deeplKey.empty() || !st.msKey.empty() || (freeFrom && !freeUsedUp());
        if (!st.backupOnly.empty()) {  // the AI already had its chance at this line (see translateLines)
            if (deeplOrFree(true)) {
                via += " (" + st.backupOnly + ")";
                return true;
            }
            return false;
        }
        // Another AI with a key (the person's own choice of a second one): it covers the lines this AI
        // fails on (declined, refused, out of credit, too many requests, an error), never just slow ones,
        // so a line is never paid for twice. One try, on its own connection.
        auto otherAi = [&]() -> int {
            if (!st.aiBackup) return -1;
            // The online ones with a key, then a model loaded on this PC (it's there when Local is
            // the service of another part of the app, or was until a moment ago).
            for (int svc : {kClaude, kGemini, kOpenAI, kLocal}) {
                if (svc == st.service) continue;
                TranslateSetup b = st;
                b.service = svc;
                const std::string& k = svc == kClaude ? st.claudeKey : svc == kGemini ? st.geminiKey : svc == kLocal ? st.localUrl : st.openaiKey;
                if (k.empty()) continue;
                if (std::chrono::steady_clock::now() < healthOf(healthKey(b)).deadUntil) continue;
                return svc;
            }
            return -1;
        };
        auto askOtherAi = [&](std::string& o, std::string& e) -> bool {
            const int svc = otherAi();
            if (svc < 0 || (http.cancel && *http.cancel)) return false;
            TranslateSetup b = st;
            b.service = svc;
            const std::string bk = healthKey(b);
            Http& h = otherHttp_;  // kept open between lines
            h.cancel = http.cancel;
            int why = kFailOther;
            const bool ok = aiCall(h, b, aiInstructions(b, from, to), text, text, std::max(4000, st.timeoutMs * 2), o, e, why);
            if (ok) markAlive(bk);
            else if (why == kFailDead) markDead(bk, e);
            else if (why == kFailLimited) markBusy(bk, 120);
            if (ok) {
                const char* other = svc == kClaude ? "Claude" : svc == kGemini ? "Gemini" : svc == kLocal ? "Local" : "OpenAI";
                via = other;
            }
            return ok;
        };
        // The AI refused the key or is out of credit (found out in the last few minutes, by any part of
        // the app): no point waiting on it for every line. Straight to a backup, saying why: DeepL or
        // Microsoft, then the other AI, then Free.
        {
            const Health h = healthOf(hk);
            if (multi && std::chrono::steady_clock::now() < h.deadUntil) {
                err = h.deadWhy;
                return false;
            }
            if (std::chrono::steady_clock::now() < h.deadUntil) {
                bool ok = deeplOrFree(true, false);
                if (!ok) {
                    std::string o, e;
                    ok = askOtherAi(o, e);
                    if (ok) out = o;
                }
                if (!ok && backupReady) ok = deeplOrFree(false);
                if (ok) {
                    via += " (" + h.deadWhy + ")";
                    return true;
                }
            }
        }
        const std::string langKey = hk + "|" + to.code + "|" + st.variety;
        auto found = extraMs_.find(langKey);
        if (found == extraMs_.end())  // a head start for the languages known to run long
            found = extraMs_.emplace(langKey, longLanguage(to.code) ? st.timeoutMs / 4 : 0).first;
        int& extra = found->second;
        int budget = st.timeoutMs + (st.variety.empty() ? 0 : std::min(1500, st.timeoutMs / 2)) + extra;
        // A model on this PC reads the instructions the first time (slow without a strong graphics
        // card) and is quick after that: at least 5 s, so the first lines aren't all handed to a backup.
        if (st.service == kLocal) budget = std::max(budget, 5000);
        // Nothing to fall back on (no DeepL key, and Free used up or can't tell the language): cutting the
        // AI off at the time limit would only leave the line untranslated. Wait for it instead.
        const int normalBudget = budget;  // second copies keep to this, not the longer wait below
        if (!backupReady) budget = std::max(budget, std::max(8000, st.timeoutMs * 3));
        const auto started = std::chrono::steady_clock::now();
        auto left = [&] {
            return budget - int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        };
        bool good = false;
        std::string firstErr;
        std::string ask = text;
        int lastWhy = kFailOther;
        bool declined = false;  // it gave something other than the line (held back on it) at least once
        wordsGender = 0;
        // A second copy of a slow request: same AI, same model, same instructions, on a connection of
        // its own. A slow answer is usually one stuck at the service for a moment, and a fresh copy
        // often comes back sooner. The first good answer is used and the other copy is stopped.
        // It waits a bit longer than this AI usually takes (learned as it goes), and only while there's
        // still time for the copy to answer.
        const std::string key = modelKey(st);
        if (key != typicalKey_) {
            typicalKey_ = key;
            typicalMs_ = 0;
        }
        auto spareAfter = [&](int ms) {
            if (st.service == kLocal) return 0;  // a second copy would only make this PC do the work twice
            if (std::chrono::steady_clock::now() < healthOf(hk).busyUntil) return 0;  // it's busy: a copy would only add to that
            // Clearly later than usual: a quarter longer than this AI usually takes (half the time limit
            // until that's known). The copy is only sent if it still has its usual time to answer.
            const int wait = std::max(500, typicalMs_ > 0 ? typicalMs_ * 5 / 4 + 150 : ms / 2);
            const int need = std::max(500, typicalMs_);
            return ms - wait >= need ? wait : 0;
        };
        auto call = [&](const std::string& source, const std::string& sent, int ms, std::string& o, int& why) {
            struct Shot { std::string out, err, raw; int why = kFailOther, ms = 0; };
            Shot shots[2];
            const auto t0 = std::chrono::steady_clock::now();
            const int winner = sendWithSpare(
                // Second copies cost tokens: only within the normal time limit, as before, even when
                // there's no backup and the AI is given longer.
                http, spare_, spareAfter(std::min(ms, normalBudget)),
                [&](Http& h, int i) {
                    const int time = ms - msSince(t0);
                    if (time < 300) {
                        shots[i].err = "timed out";
                        return false;
                    }
                    const auto s0 = std::chrono::steady_clock::now();
                    const bool ok = aiCall(h, st, instructions, source, sent, time, shots[i].out, shots[i].err, shots[i].why);
                    shots[i].raw = tlRaw;
                    shots[i].ms = msSince(s0);
                    return ok;
                },
                [&] { return shots[0].why != kFailRejected; });  // it declined the line: a copy would too
            // Over the plan's limit: no second copies for two minutes (they'd count against it too).
            if (shots[0].why == kFailLimited || shots[1].why == kFailLimited) markBusy(hk, 120);
            else if (shots[0].why == kFailBusy || shots[1].why == kFailBusy) markBusy(hk, 60);
            const Shot& w = shots[winner >= 0 ? winner : 0];
            o = w.out;
            why = w.why;
            tlRaw = w.raw;
            if (winner < 0) {
                err = w.err;
                return false;
            }
            const int took = std::min(w.ms, 4000);
            typicalMs_ = typicalMs_ > 0 ? (typicalMs_ * 3 + took) / 4 : took;
            return true;
        };
        // The listed (or swear) words, translated by DeepL (or Free) on a connection of their own,
        // started at the same moment as the AI call so neither waits for the other.
        struct WordResult { std::string word; int by = 0; };  // by: 1 DeepL, 2 Free, 0 neither
        const std::string formality = tlFormality;
        auto lookUpWords = [&](const std::vector<std::string>& found) {
            return std::async(std::launch::async, [this, found, &st, from, freeFrom, &to, formality, text, cancel = http.cancel] {
                Http& h = wordsHttp_;  // kept open between lines
                h.cancel = cancel;
                tlFormality = formality;
                tlContext = text;  // the whole sentence, so DeepL knows what each word means here
                std::vector<WordResult> res(found.size());
                for (size_t i = 0; i < found.size(); ++i) {
                    std::string e;
                    bool refused = false;
                    if (!st.deeplKey.empty() && deepl(h, st.deeplKey, found[i], from, to, st.deeplVariant, res[i].word, e, refused)) res[i].by = 1;
                    else if (freeFrom && myMemory(h, found[i], *freeFrom, to, res[i].word, e)) res[i].by = 2;
                    else res[i].word.clear();
                }
                tlContext.clear();
                return res;
            });
        };
        // Fills [[1]], [[2]]... in an AI answer with those words.
        // `fillBy` says which service really did them ("DeepL", "Free", or both), for the bottom strip.
        std::string fillBy;
        auto fillIn = [&](std::string masked, const std::vector<std::string>& found, std::vector<WordResult> words, std::string& filled) {
            for (size_t i = 0; i < found.size(); ++i)
                if (masked.find("[[" + std::to_string(i + 1) + "]]") == std::string::npos) return false;  // a marker got lost
            auto wordsIn = [](const std::string& t) {
                int n = 0;
                bool in = false;
                for (unsigned char ch : t) {
                    const bool sp = ch == ' ' || ch == '\n' || ch == '\t';
                    if (!sp && !in) ++n;
                    in = !sp;
                }
                return n;
            };
            bool byDeepl = false, byFree = false;
            for (size_t i = 0; i < found.size() && i < words.size(); ++i) {
                std::string word = words[i].word;
                byDeepl = byDeepl || words[i].by == 1;
                byFree = byFree || words[i].by == 2;
                // A word that comes back as a whole phrase wasn't translated, it was swapped for something
                // else (DeepL does this with some words it won't translate). Like the AI, leave it out
                // rather than put words in their mouth.
                const int in = wordsIn(found[i]), got = wordsIn(word);
                if (got >= in + 2 && got > in * 2) word.clear();
                const std::string mark = "[[" + std::to_string(i + 1) + "]]";
                for (size_t at = masked.find(mark); at != std::string::npos; at = masked.find(mark, at + word.size()))
                    masked.replace(at, mark.size(), word);
            }
            fillBy = byDeepl && byFree ? "DeepL and Free" : byFree ? "Free" : byDeepl ? "DeepL" : "nobody: left out";
            for (size_t at = masked.find("  "); at != std::string::npos; at = masked.find("  ")) masked.erase(at, 1);
            for (const char* p : {" ?", " !", " ,", " ."})
                for (size_t at = masked.find(p); at != std::string::npos; at = masked.find(p)) masked.erase(at, 1);
            while (!masked.empty() && masked.front() == ' ') masked.erase(masked.begin());
            filled = masked;
            return true;
        };
        const std::string markerNote = "\n\n(The [[1]], [[2]]... markers stand for words that get filled in later. "
                                       "Keep every marker exactly as written, where it belongs in your sentence.)";
        // "Words for DeepL": the person's own list of words the AI must never see or reword. The AI
        // translates the line around markers, and DeepL (or Free) translates just those words.
        if (!multi && !st.deeplWords.empty() && !st.faithful) {
            std::vector<std::string> found;
            const std::string marked = markListed(text, st.deeplWords, found);
            if (!found.empty()) {
                std::string masked;
                int why = kFailOther;
                auto words = lookUpWords(found);  // at the same time as the AI
                const bool aiOk = call(marked, marked + markerNote, std::max(1200, left()), masked, why);
                if (aiOk && fillIn(masked, found, words.get(), out)) {
                    via = std::string(name) + " (your words by " + fillBy + ")";
                    return true;
                }
                if (err == "cancelled") return false;
                // The AI lost a marker or failed: the whole line goes to DeepL, so your words still never
                // pass through the AI.
                if (deeplOrFree(true)) {
                    via += " (your words; " + std::string(name) + " couldn't keep them)";
                    return true;
                }
                return false;
            }
        }
        // A backup, ready early: if the AI hasn't answered 0.7 s before the time limit, DeepL (or Free)
        // starts on its own connection, so if the AI then misses the limit the backup is already
        // there (or nearly) instead of only starting then. When the AI answers in time, the backup is
        // stopped and dropped. It costs DeepL characters only on lines where the AI was slow.
        std::atomic<bool> backupStop{false}, backupNow{false};
        std::string backupOut, backupErr, backupVia;
        auto backup = std::async(std::launch::async, [&, formality = tlFormality, userCancel = http.cancel] {
            if (multi) return false;  // the lines go one by one through the backups instead
            // DeepL usually answers in well under 0.7 s: starting then leaves the AI (and its second
            // copy) as much of the time as possible, and DeepL is only paid for on lines that need it.
            const auto wait = std::chrono::milliseconds(std::max(600, budget - 700));
            while (std::chrono::steady_clock::now() - started < wait && !backupStop && !backupNow &&
                   !(http.cancel && *http.cancel))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (backupStop || (http.cancel && *http.cancel)) return false;
            Http& h = backupHttp_;  // kept open between lines
            h.cancel = userCancel;
            h.stop = &backupStop;
            struct Unstop { Http& h; ~Unstop() { h.stop = nullptr; } } unstop{h};
            tlFormality = formality;
            tlContext = beforeText;
            bool refused = false;
            if (!st.deeplKey.empty() && deepl(h, st.deeplKey, text, from, to, st.deeplVariant, backupOut, backupErr, refused)) {
                backupVia = "DeepL";
                return true;
            }
            if (!st.msKey.empty() && !backupStop &&
                microsoft(h, st.msKey, st.msRegion, text, from, to, backupOut, backupErr, refused)) {
                backupVia = "Microsoft";
                return true;
            }
            if (backupStop) return false;
            if (freeFrom && myMemory(h, text, *freeFrom, to, backupOut, backupErr)) {
                backupVia = "Free";
                return true;
            }
            return false;
        });
        // Whatever way this function returns, the backup is told to stop first (declared after the
        // future, so it runs before the future's destructor waits for the backup thread).
        struct StopBackup { std::atomic<bool>& f; ~StopBackup() { f = true; } } stopBackup{backupStop};
        for (int attempt = 0; attempt < 2 && !good; ++attempt) {
            const int ms = left();
            if (ms < 350) {  // not enough time for another try
                if (attempt == 0) err = "timed out";
                break;
            }
            int why = kFailOther;
            good = call(text, ask, ms, out, why);
            lastWhy = why;
            // It held back on the line: start the backup now, alongside the second try, so whichever
            // answers first is ready (the AI's answer still wins if it comes in time).
            if (why == kFailRejected) {
                declined = true;
                backupNow = true;
            }
            if (good || err == "cancelled") break;
            if (attempt == 0) firstErr = err;
            // It said no outright: that's its answer. No second, firmer ask (and no tokens for one).
            if (why == kFailRejected && tlRaw.find("<skip") != std::string::npos) break;
            // Declined a line with swearing in it: asking again almost never changes the answer, and it
            // costs a whole extra wait. Go straight to the markers below (the swear words by DeepL).
            if (why == kFailRejected) {
                std::vector<std::string> swears;
                markSwears(text, swears);
                if (!swears.empty()) break;
            }
            // One more try, while there's time, when it's likely to work: the model answered with
            // something other than the line (asked again, more firmly), or it was busy for a moment.
            if (why == kFailRejected && multi)
                ask = text + "\n\n(Translate every numbered line. Answer with only the <l1></l1>, <l2></l2>... tags. No notes, "
                             "no refusal.)";
            else if (why == kFailRejected)
                ask = text + "\n\n(Say this line in " + std::string(st.variety.empty() ? to.name : st.variety) +
                      ". Answer with only the line, inside <line></line>. No notes, no refusal.)";
            else if (why == kFailBusy)
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(400, std::max(0, left() - 600))));
            else
                break;
        }
        if (good) {
            backupStop = true;  // not needed: stop it if it's running (the future waits for it to end)
            via = name;
            markAlive(hk);
            if (budget - left() < st.timeoutMs * 6 / 10) extra = std::max(0, extra - 100);  // plenty of room: ease off
            if (st.readGender) {  // the speaker's own words showed their gender
                if (tlRaw.find("<g>f</g>") != std::string::npos) wordsGender = kGenderWoman;
                else if (tlRaw.find("<g>m</g>") != std::string::npos) wordsGender = kGenderMan;
            }
            return true;
        }
        if (err == "cancelled") {
            backupStop = true;
            return false;
        }
        // The AI declined the line twice, and it has swearing in it: the AI translates the rest with
        // markers where the swear words were, and DeepL (or the free service) translates just those.
        if (lastWhy == kFailRejected && !multi) {
            std::vector<std::string> found;
            const std::string marked = markSwears(text, found);
            std::string masked;
            int why = kFailOther;
            std::future<std::vector<WordResult>> words;
            if (!found.empty()) words = lookUpWords(found);
            // Whatever is left of the time limit (at least 1.2 s), not a whole new time limit on top.
            if (!found.empty() && call(marked, marked + markerNote, std::max(1200, left()), masked, why) &&
                fillIn(masked, found, words.get(), out)) {
                via = std::string(name) + " (swear words by " + fillBy + ")";
                backupStop = true;
                return true;
            }
        }
        if (!firstErr.empty() && firstErr != err && lowerCopy(err).find("timed out") == std::string::npos) err = firstErr;
        // DeepL and Free only know the standard language (or DeepL's few regional spellings).
        const std::string standardNote =
            st.varietyName.empty() || !st.deeplVariant.empty() ? std::string() : "; standard " + std::string(to.name);
        // Too slow or failed: this phrase goes through DeepL (or Free) so the conversation keeps going.
        const bool slow = lowerCopy(err).find("timed out") != std::string::npos || lowerCopy(err).find("timeout") != std::string::npos;
        // Said as it was: a line the AI held back on isn't a slow AI.
        const std::string why = declined ? std::string(name) + " declined this line"
                              : slow     ? std::string(name) + " too slow"
                                         : err;
        // Ran out of time (not because the service said it was busy, and not over a line it held back
        // on): give this language a bit more next time, up to double the limit.
        if (slow && !declined && !multi && backupReady && std::chrono::steady_clock::now() >= healthOf(hk).busyUntil)
            extra = std::min(st.timeoutMs, extra + st.timeoutMs / 4);
        if (lastWhy == kFailDead) markDead(hk, err);
        backupNow = true;  // the AI is done one way or another: the backup goes now if it hasn't yet
        // It declined the line, or ran out of time, and a model on this PC is loaded for this: that one
        // tries it next, while DeepL or Free get ready alongside. It has its own rules, and when it says
        // no or is late too, the usual backups take the line as before. Not for other failures.
        if ((declined || slow) && !multi && st.localOnDecline && st.service != kLocal && !st.localUrl.empty() &&
            !(http.cancel && *http.cancel)) {
            TranslateSetup b = st;
            b.service = kLocal;
            localHttp_.cancel = http.cancel;
            std::string o, e;
            int lw = kFailOther;
            // A late line is late already: the local model gets one more time limit, no longer.
            const int localMs = declined ? std::max(3000, st.timeoutMs * 2) : std::max(1500, st.timeoutMs);
            if (aiCall(localHttp_, b, aiInstructions(b, from, to), text, text, localMs, o, e, lw)) {
                backupStop = true;
                out = o;
                via = "Local (" + why + ")";
                return true;
            }
            if (e == "cancelled") {
                backupStop = true;
                err = e;
                return false;
            }
        }
        const bool gotBackup = backup.get();
        auto useBackup = [&] {
            out = backupOut;
            via = backupVia + " (" + why + (backupVia == "Free" && !st.varietyName.empty() ? "; standard " + std::string(to.name) : standardNote) + ")";
            return true;
        };
        if (multi) {  // another AI may still answer it whole; otherwise translateLines goes line by line
            std::string o, e;
            if (askOtherAi(o, e)) {
                out = o;
                via += " (" + why + ")";
                return true;
            }
            err = why;
            return false;
        }
        // DeepL or Microsoft: used as is. Free, when the AI only ran late: used too (it's ready now).
        if (gotBackup && (backupVia != "Free" || (slow && !declined))) return useBackup();
        // The AI failed on the line (declined, refused, an error) and there's a key for another AI: that
        // one says it far better than Free. Only now, so a line is never paid for at two AIs.
        {
            std::string o, e;
            if (askOtherAi(o, e)) {
                out = o;
                via += " (" + why + ")";
                return true;
            }
        }
        if (gotBackup) return useBackup();
        if (deeplOrFree(true)) {
            via += " (" + why + (via == "Free" && !st.varietyName.empty() ? "; standard " + std::string(to.name) : standardNote) + ")";
            return true;
        }
        err = why + "; fallback failed too: " + err;
        return false;
    }
    if (st.service == kMicrosoft) {
        if (st.msKey.empty()) { err = "Add your Microsoft Translator key, or switch the translation service to Free"; return false; }
        bool refused = false;
        if (microsoft(http, st.msKey, st.msRegion, text, from, to, out, err, refused)) {
            via = !st.varietyName.empty() ? "Microsoft (standard " + std::string(to.name) + ")" : "Microsoft";
            return true;
        }
        if (!refused) return false;  // a problem to show (the key, the month's characters), not to hide
        // A language Microsoft doesn't have: DeepL if there's a key, else Free.
        if (!st.deeplKey.empty()) {
            bool dlRefused = false;
            if (deepl(http, st.deeplKey, text, from, to, st.deeplVariant, out, err, dlRefused)) {
                via = "DeepL (Microsoft doesn't do " + std::string(to.name) + ")";
                return true;
            }
        }
        if (freeFrom && myMemory(http, text, *freeFrom, to, out, err)) {
            via = "Free (Microsoft doesn't do " + std::string(to.name) + ")";
            return true;
        }
        err = "Microsoft Translator doesn't translate " + std::string(to.name) + ". Pick another service or language.";
        return false;
    }
    if (st.service == kDeepL) {
        if (st.deeplKey.empty()) { err = "Add your DeepL API key, or switch the translation service to Free"; return false; }
        bool refused = false;
        if (deepl(http, st.deeplKey, text, from, to, st.deeplVariant, out, err, refused)) {
            via = !st.varietyName.empty() && st.deeplVariant.empty() ? "DeepL (standard " + std::string(to.name) + ")" : "DeepL";
            return true;
        }
        if (!refused) return false;
        // DeepL doesn't translate this language (pair): Microsoft Translator if there's a key, else Free.
        if (!st.msKey.empty()) {
            bool msRefused = false;
            if (microsoft(http, st.msKey, st.msRegion, text, from, to, out, err, msRefused)) {
                via = "Microsoft (DeepL doesn't do " + std::string(to.name) + ")";
                return true;
            }
        }
        if (!freeFrom) {
            err = "DeepL doesn't translate " + std::string(to.name) +
                  ". Pick the spoken language so the free service can, or pick another language.";
            return false;
        }
        if (myMemory(http, text, *freeFrom, to, out, err)) { via = "Free (DeepL doesn't do " + std::string(to.name) + ")"; return true; }
        err = "DeepL doesn't translate this language, and the free service failed too: " + err;
        return false;
    }
    const bool ok = deeplOrFree(false);
    if (ok && !st.varietyName.empty()) via += " (standard " + std::string(to.name) + ")";
    return ok;
}

bool Translator::checkBack(const TranslateSetup& st, const std::string& text, const Language& from, const Language& to,
                           std::string& out, std::string& err) {
    via.clear();
    std::string problem;
    const bool spareFree = st.service == kMyMemory && (!st.msKey.empty() || !st.deeplKey.empty());
    if (!freeUsedUp() && !spareFree) {
        if (myMemory(http, text, from, to, out, err)) { via = "Free"; return true; }
        problem = err;
    }
    if (!st.msKey.empty()) {
        bool refused = false;
        if (microsoft(http, st.msKey, st.msRegion, text, &from, to, out, err, refused)) { via = "Microsoft"; return true; }
        if (problem.empty()) problem = err;
    }
    if (!st.deeplKey.empty()) {
        const std::string keep = tlFormality;
        tlFormality.clear();  // as it is, not made politer or looser
        bool refused = false;
        const bool ok = deepl(http, st.deeplKey, text, &from, to, std::string(), out, err, refused);
        tlFormality = keep;
        if (ok) { via = "DeepL"; return true; }
        if (problem.empty()) problem = err;
    }
    err = problem.empty() ? "No translator left for the check today" : problem;
    return false;
}

bool Translator::translateLines(const TranslateSetup& st, const std::vector<std::string>& texts,
                                const std::vector<const Language*>& froms, const std::vector<int>& speakers, const Language& to,
                                std::vector<std::string>& outs, std::vector<int>& genders, std::string& err) {
    const size_t n = texts.size();
    outs.assign(n, std::string());
    genders.assign(n, 0);
    auto oneByOne = [&] {
        std::string lastVia;
        for (size_t i = 0; i < n; ++i) {
            TranslateSetup one = st;
            one.multiLines = 0;
            if (i < speakers.size()) one.speaker = speakers[i];
            std::string e;
            if (translate(one, texts[i], i < froms.size() ? froms[i] : nullptr, to, outs[i], e)) {
                genders[i] = wordsGender;
                lastVia = via;
            } else {
                outs[i].clear();
                if (e == "cancelled") { err = e; return false; }
                if (err.empty()) err = e;
            }
        }
        if (!lastVia.empty()) via = lastVia;
        return true;
    };
    if (n < 2 || !isAiService(st.service) || !st.faithful) return oneByOne();

    TranslateSetup all = st;
    all.multiLines = int(n);
    all.readGender = false;         // per line, it would need a tag each; the voice guesses go in the text
    all.speaker = kGenderUnknown;
    all.timeoutMs = st.timeoutMs + std::min(st.timeoutMs, 300 * int(n - 1));  // a little longer to write more
    std::string numbered;
    for (size_t i = 0; i < n; ++i) {
        const int g = i < speakers.size() ? speakers[i] : kGenderUnknown;
        numbered += std::to_string(i + 1) + ". " + (g == kGenderMan ? "[man] " : g == kGenderWoman ? "[woman] " : "") +
                    texts[i] + "\n";
    }
    const Language* from = n > 0 && froms.size() == n ? froms[0] : nullptr;
    for (size_t i = 1; i < froms.size() && from; ++i)
        if (froms[i] != from) from = nullptr;  // mixed: it works each line's language out
    std::string out, e;
    if (translate(all, numbered, from, to, out, e)) {
        std::vector<std::string> parts;
        size_t a = 0;
        for (;;) {
            const size_t b = out.find('\x1E', a);
            parts.push_back(out.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (parts.size() == n) {
            outs = parts;
            return true;
        }
    }
    if (e == "cancelled") { err = e; return false; }
    // It only ran late: asking the same AI again line by line would cost as much again and take
    // longer still. The quick backups do the lines instead, or, with none, they stay as they were.
    if (e.find("too slow") != std::string::npos) {
        const bool quick = !st.deeplKey.empty() || !st.msKey.empty() || !freeUsedUp();
        if (!quick) {
            err = e;
            return true;
        }
        TranslateSetup fast = st;
        fast.backupOnly = e;
        std::string e2;
        for (size_t i = 0; i < n; ++i) {
            std::string o;
            if (translate(fast, texts[i], i < froms.size() ? froms[i] : nullptr, to, o, e2)) outs[i] = o;
            else if (e2 == "cancelled") { err = e2; return false; }
            else if (err.empty()) err = e + "; fallback failed too: " + e2;
        }
        return true;
    }
    return oneByOne();  // it failed on the lines (declined one, an error): each line with the usual backups
}

bool checkServiceKey(int service, const std::string& rawKey, std::string& msg) {
    std::string key = rawKey;
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(key.begin());
    if (key.empty()) { msg = "Paste a key first."; return false; }
    Http http;
    HttpRequest r;
    std::string payload;
    if (service == kDeepL) {
        r.url = deeplHost(key) + "/v2/usage";
        r.headers = {"Authorization: DeepL-Auth-Key " + key};
    } else if (service == kClaude) {  // one tiny message: also tells whether there are credits on the account
        payload = json({{"model", "claude-haiku-5-5"}, {"max_tokens", 1},
                        {"messages", json::array({{{"role", "user"}, {"content", "Hi"}}})}}).dump();
        r.url = "https://api.anthropic.com/v1/messages";
        r.headers = {"x-api-key: " + key, "anthropic-version: 2023-06-01", "content-type: application/json"};
        r.body = &payload;
    } else if (service == kOpenAI) {  // one tiny answer: also tells whether the plan can use the model
        payload = json({{"model", "gpt-6-luna"}, {"input", "Hi"}, {"max_output_tokens", 16}, {"store", false},
                        {"reasoning", {{"effort", "none"}}}}).dump();
        r.url = "https://api.openai.com/v1/responses";
        r.headers = {"Authorization: Bearer " + key, "Content-Type: application/json"};
        r.body = &payload;
    } else {
        r.url = "https://generativelanguage.googleapis.com/v1beta/models";
        r.headers = {"x-goog-api-key: " + key};
    }
    r.timeoutSec = 20;
    long status = 0;
    std::string body, err;
    if (!http.perform(r, status, body, err)) {
        msg = "Couldn't reach the service. Check your internet connection.";
        return false;
    }
    const std::string low = lowerCopy(body);
    if (status == 200) {
        msg = "It works.";
        return true;
    }
    if (service == kOpenAI && (low.find("insufficient_quota") != std::string::npos || low.find("billing") != std::string::npos))
        msg = "The key is right, but the account has no credit. Add credits on platform.openai.com (Billing), or pick the Build plan.";
    else if (service == kOpenAI && (status == 404 || low.find("model_not_found") != std::string::npos ||
                                    (status == 403 && low.find("model") != std::string::npos)))
        msg = "The key is right, but your OpenAI plan can't use gpt-6-luna yet. Adding $5 of credit (Build plan) unlocks it.";
    else if (service == kClaude && low.find("credit balance") != std::string::npos)
        msg = "The key is right, but the account has no credits yet. Buy credits in the Claude Console (Billing).";
    else if (status == 401 || status == 403 || (service == kGemini && status == 400))
        msg = "That key was refused. Copy it again, the whole thing.";
    else if (status == 456)
        msg = "The key works, but this month's DeepL characters are used up.";
    else if (status == 429)
        msg = "The key works, but it's busy or out of credit right now.";
    else
        msg = "The service answered with an error (" + std::to_string(status) + "). Try again in a minute.";
    return false;
}
