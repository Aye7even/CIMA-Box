#include "translate.h"

#include "extras.h"

#include <atomic>

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
        {"Filipino", "fil", "fil tgl tl", "tl", "FIL", "FIL", true, "Tagalog"},
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

bool myMemory(Http& http, const std::string& text, const Language& from, const Language& to, std::string& out,
              std::string& err) {
    out.clear();
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
            if (warning || apiStatus == 429) err = "Free translation limit reached for today. Switch to DeepL or try again tomorrow.";
            else err = "Translation failed (" + std::to_string(apiStatus) + ")" + (details.empty() ? "" : ": " + details);
            return false;
        }
        if (!out.empty()) out += ' ';
        out += decodeEntities(translated);
    }
    return true;
}

}  // namespace

std::string deeplHost(const std::string& key) {
    const bool free = key.size() > 3 && key.compare(key.size() - 3, 3, ":fx") == 0;
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

bool deeplOnce(Http& http, const std::string& key, const std::string& text, const Language* from, const std::string& target,
               std::string& out, std::string& err, bool& languageRefused, bool formality = true) {
    languageRefused = false;
    json req = {{"text", json::array({text})}, {"target_lang", target}};
    if (from) req["source_lang"] = from->deeplSource;
    const bool formal = formality && !tlFormality.empty();
    if (formal) req["formality"] = tlFormality;
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
            return deeplOnce(http, key, text, from, target, out, err, languageRefused, false);
        if (status == 400 && lower.find("lang") != std::string::npos) languageRefused = true;
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

bool extractAnswer(const std::string& raw, const std::string& source, std::string& out) {
    const size_t a = raw.find("<line>");
    if (a != std::string::npos) {
        const size_t b = raw.find("</line>", a + 6);
        out = cleanAnswer(raw.substr(a + 6, b == std::string::npos ? std::string::npos : b - a - 6));
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
    else if (status == 429) why = "too many requests or out of credit";
    else if (status == 529 || status == 503) why = "the service is overloaded right now";
    return std::string(service) + " " + std::to_string(status) + (why.empty() ? "" : ": " + why);
}

thread_local std::string tlRaw;  // the last AI answer as it came (for the <g> gender note)

// Why an AI call failed, to decide whether one more try is worth it.
enum AiFail { kFailOther = 0, kFailRejected, kFailBusy };

std::string errorMessage(const json& j) {
    if (j.is_object() && j.contains("error") && j["error"].is_object() && j["error"].contains("message") &&
        j["error"]["message"].is_string())
        return j["error"]["message"].get<std::string>();
    return {};
}

// `source`: what was said (to tell an explanation from a translation); `text`: what is sent.
bool claude(Http& http, const TranslateSetup& st, const std::string& instructions, const std::string& source,
            const std::string& text, int timeoutMs, std::string& out, std::string& err, int& why, bool& noEffort) {
    why = kFailOther;
    if (st.claudeKey.empty()) { err = "Add your Claude (Anthropic) API key"; return false; }
    for (int attempt = 0; attempt < 2; ++attempt) {
        json req = {{"model", st.claudeModel.empty() ? "claude-haiku-5-5" : st.claudeModel},
                    {"max_tokens", 1024},  // room for any brief thinking; the answer itself is one line
                    {"system", instructions},
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
            if ((status == 429 && lowerCopy(msg).find("credit") == std::string::npos) || status == 529 || status == 503)
                why = kFailBusy;
            err = httpProblem("Claude", status, msg);
            return false;
        }
        if (j.is_object() && j.contains("usage"))
            countTokens(0, tokenField(j["usage"], "input_tokens"), tokenField(j["usage"], "output_tokens"));
        std::string text2;
        if (j.is_object() && j.contains("content") && j["content"].is_array())
            for (const json& block : j["content"])
                if (block.is_object() && block.value("type", "") == "text" && block.contains("text") && block["text"].is_string())
                    text2 += block["text"].get<std::string>();
        tlRaw = text2;
        if (!extractAnswer(text2, source, out)) {
            why = kFailRejected;
            err = out.empty() && trimText(text2).empty() ? "Claude gave an empty answer" : "Claude explained instead of translating";
            return false;
        }
        return true;
    }
    return false;
}

bool gemini(Http& http, const TranslateSetup& st, const std::string& instructions, const std::string& source,
            const std::string& text, int timeoutMs, std::string& out, std::string& err, int& why, bool& noThinkingLevel) {
    why = kFailOther;
    if (st.geminiKey.empty()) { err = "Add your Gemini (Google AI Studio) API key"; return false; }
    for (int attempt = 0; attempt < 2; ++attempt) {
        json gen = {{"maxOutputTokens", 1024}};
        if (!noThinkingLevel) gen["thinkingConfig"] = {{"thinkingLevel", "low"}};  // as little thinking as allowed
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
            if (status == 400 && !noThinkingLevel && lowerCopy(msg).find("thinking") != std::string::npos) {
                noThinkingLevel = true;  // a model without thinking levels: ask again without the setting
                continue;
            }
            if (status == 429 || status == 503) why = kFailBusy;  // the free plan's per-minute limit, or busy
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
        if (!extractAnswer(text2, source, out)) {
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
            if (code == "insufficient_quota" || low.find("quota") != std::string::npos)
                err = "OpenAI: no credit left on the account (add credits in Billing)";
            else if (status == 404 || code == "model_not_found")
                err = "OpenAI: your plan can't use " + (st.openaiModel.empty() ? std::string("gpt-6-luna") : st.openaiModel) +
                      " (adding credit usually unlocks it)";
            else {
                if (status == 429 || status == 503 || status == 500) why = kFailBusy;
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
        if (!extractAnswer(text2, source, out)) {
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
        {"fil", "everyday Tagalog with the English words people mix in, and particles like naman, kasi, eh"},
        {"hi", "tu or tum (never aap), the English words people really mix in, and words like yaar"},
        {"vi", "pronouns friends use (tao/mày between close friends, mình/bạn otherwise) and particles like nhé, nha, á"},
        {"th", "pronouns friends use (เรา, แก, มึง/กู only for very close friends) and particles like อะ, ว่ะ, นะ"},
        {"en", "contractions, dropped words, and everyday slang and fillers like gonna, kinda, like, bro"},
    };
    for (const auto& h : kHints)
        if (code == h.first) return h.second;
    return {};
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
        if (st.readGender)
            return s + "\n\nAnswer with only the translation, inside <line></line>. Then, only if the speaker's own words in "
                       "the original clearly show their gender (like Spanish \"estoy cansada\", or \"I'm a girl\"), add "
                       "<g>f</g> or <g>m</g>. Nothing else outside the tags.";
        return s + "\n\nAnswer with only the translation, inside <line></line>, and nothing outside it.";
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
    const std::string hint = casualHint(to.code);
    const bool custom = st.level == kLevelCustom && !trimText(st.customStyle).empty();
    const bool polite = !custom && st.level == kLevelPolite;
    const bool veryCasual = !custom && st.level == kLevelVeryCasual;
    const bool casual = !custom && !polite;  // Casual and Very casual
    std::string level, check;
    if (custom) {
        level = "HOW TO SOUND (the most important rule): exactly the way they describe it: \"" + trimText(st.customStyle) +
                "\". Follow that closely; it matters more than sounding neutral, and more than how the original was worded.";
        check = "Before you answer, check the line against their description (\"" + trimText(st.customStyle) +
                "\"). If it sounds more neutral or more polite than that, rewrite it.";
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
                       "shortened and slurred words, filler words, teasing, and swearing where the mood fits."
                     : "CASUAL. You're talking to a friend you hang out with a lot. Talk the relaxed way friends talk to "
                       "each other: contractions, dropped words, everyday slang and filler words.") +
                "\n  - Do NOT translate the sentence as it is worded. First get what they mean, then say that the way a "
                "native would say it to " + (veryCasual ? "their best friend" : "a friend") + ". A tidy, complete or polite "
                "original does NOT make the result tidy or polite: the style setting decides, not the original's wording.\n"
                "  - Use the informal speech level and informal \"you\" (Korean 반말, Japanese タメ口, tu/tú/du/ты and so on). "
                "Polite and formal forms are forbidden: no 요/습니다 endings, no です/ます, no usted/vous/Sie/вы/您/aap/Anda, "
                "no polite request formulas (the equivalents of \"could you please\", \"would you mind\", \"I would like "
                "to\", \"excuse me\", \"thank you very much\"), and no full textbook sentences where people would say half of it.\n"
                "  - Shorten it the way speech is shortened: drop subjects, particles and words natives drop, use the "
                "contracted and slurred forms, and start or end with the small filler words natives use.\n" +
                (veryCasual
                     ? "  - Slang is expected, not optional: wherever a slang word or a rougher everyday word exists for what "
                       "they mean, use it instead of the dictionary word. Swear the way friends do when they're excited, "
                       "annoyed or joking, as long as it fits what was said.\n"
                     : "  - Prefer the everyday word over the dictionary word, and use light slang where friends would.\n") +
                (hint.empty() ? std::string() : "  - In " + target + " that means: " + hint + ".\n") +
                "  - How far to go (shown in English, do the same in " + spoken + "): \"Could you tell me where the mirror "
                "is?\" becomes " + (veryCasual ? "\"yo where's the mirror at\"" : "\"hey where's the mirror?\"") +
                "; \"I am very tired today\" becomes " + (veryCasual ? "\"bro I'm so dead today\"" : "\"ugh I'm so tired today\"") +
                "; \"Thank you very much, that was fun\" becomes " + (veryCasual ? "\"ayy thanks man, that was sick\"" : "\"thanks, that was fun!\"") +
                "; \"I do not understand\" becomes " + (veryCasual ? "\"wait what, I don't get it\"" : "\"huh? I don't get it\"") + ".";
        check = std::string("Before you answer, read your line again. Would a native really say this out loud to ") +
                (veryCasual ? "their best friend" : "a friend") + "? If it could be a line from a textbook, a subtitle, "
                "a customer service chat or a polite stranger, it is wrong: rewrite it shorter, looser and more " +
                (veryCasual ? "slangy" : "relaxed") + " in the informal speech level, then answer.";
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
        "laughter written as letters (no ㅋㅋ, lol, www, jaja, хаха), no quotation marks, no notes.\n"
        "- If it is already in " + spoken + ", just make it sound natural in that style.\n"
        "- If what they said is garbled or unclear, say your best guess of what they meant. Never explain or comment.\n";
    if (!st.context.empty())
        s += std::string("\nRecent conversation, for context only (don't translate it). Use it to understand what's being talked "
             "about and to reuse the words the others use. ") +
             (casual ? "Keep the style described above even if the others talk politely or formally: never become more "
                       "polite because of them. If they are even more casual than you, you may match them:\n"
              : polite ? "Keep the style described above: if the others are clearly more casual with you, you may move one "
                         "small step toward them, never further:\n"
                       : "Keep the style described above:\n") +
             st.context;
    if (!check.empty()) s += "\n" + check;
    return s + "\nAnswer with only the line to say, inside <line></line>, and nothing outside it.";
}

bool Translator::readPicture(const TranslateSetup& st, const std::string& png64, const Language& to,
                             std::vector<std::pair<std::string, std::string>>& lines, std::string& err) {
    lines.clear();
    if (!isAiService(st.service)) { err = "AI reading needs Claude, Gemini or OpenAI as the translation service."; return false; }
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
        if (openaiReasoning_ < 2) req["reasoning"] = {{"effort", openaiReasoning_ == 0 ? "none" : "low"}};
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
    return st.service == kClaude   ? claude(http, st, instructions, text, text, ms, out, err, why, claudeNoEffort_)
           : st.service == kGemini ? gemini(http, st, instructions, text, text, ms, out, err, why, geminiNoThinking_)
                                   : openai(http, st, instructions, text, text, ms, out, err, why, openaiReasoning_);
}

void Translator::warm(const TranslateSetup& st) {
    if (http.idleSeconds() < 15.0) return;
    HttpRequest r;
    if (st.service == kDeepL) {
        if (st.deeplKey.empty()) return;
        r.url = deeplHost(st.deeplKey) + "/v2/languages";
    } else if (st.service == kClaude) {
        r.url = "https://api.anthropic.com/v1/messages";
    } else if (st.service == kGemini) {
        r.url = "https://generativelanguage.googleapis.com/v1beta/models";
    } else if (st.service == kOpenAI) {
        r.url = "https://api.openai.com/v1/models";
    } else {
        r.url = "https://api.mymemory.translated.net/";
    }
    r.headOnly = true;  // just opens the connection, so the real request skips that step
    r.timeoutSec = 5;
    long status = 0;
    std::string body, err;
    http.perform(r, status, body, err);
}

bool Translator::translate(const TranslateSetup& st, const std::string& text, const Language* from, const Language& to,
                           std::string& out, std::string& err) {
    via.clear();
    tlFormality = st.faithful || st.level == kLevelCustom ? "" : st.level == kLevelPolite ? "prefer_more" : "prefer_less";
    auto deeplOrFree = [&](bool deeplFirst) {
        if (deeplFirst && !st.deeplKey.empty()) {
            bool refused = false;
            if (deepl(http, st.deeplKey, text, from, to, st.deeplVariant, out, err, refused)) { via = "DeepL"; return true; }
            if (!refused) return false;
        }
        if (!from) {
            err = "The free service needs to know the spoken language. Pick it, or add a DeepL key.";
            return false;
        }
        if (myMemory(http, text, *from, to, out, err)) { via = "Free"; return true; }
        return false;
    };

    if (isAiService(st.service)) {
        const char* name = st.service == kClaude ? "Claude" : st.service == kGemini ? "Gemini" : "OpenAI";
        const std::string instructions = aiInstructions(st, from, to);
        // A dialect takes the model more work: allow it half again as long (up to 1.5 s more).
        const int budget = st.timeoutMs + (st.variety.empty() ? 0 : std::min(1500, st.timeoutMs / 2));
        const auto started = std::chrono::steady_clock::now();
        auto left = [&] {
            return budget - int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        };
        bool good = false;
        std::string firstErr;
        std::string ask = text;
        int lastWhy = kFailOther;
        wordsGender = 0;
        auto call = [&](const std::string& source, const std::string& sent, int ms, std::string& o, int& why) {
            return st.service == kClaude   ? claude(http, st, instructions, source, sent, ms, o, err, why, claudeNoEffort_)
                   : st.service == kGemini ? gemini(http, st, instructions, source, sent, ms, o, err, why, geminiNoThinking_)
                                           : openai(http, st, instructions, source, sent, ms, o, err, why, openaiReasoning_);
        };
        for (int attempt = 0; attempt < 2 && !good; ++attempt) {
            const int ms = left();
            if (ms < 350) {  // not enough time for another try
                if (attempt == 0) err = "timed out";
                break;
            }
            int why = kFailOther;
            good = call(text, ask, ms, out, why);
            lastWhy = why;
            if (good || err == "cancelled") break;
            if (attempt == 0) firstErr = err;
            // One more try, while there's time, when it's likely to work: the model answered with
            // something other than the line (asked again, more firmly), or it was busy for a moment.
            if (why == kFailRejected)
                ask = text + "\n\n(Say this line in " + std::string(st.variety.empty() ? to.name : st.variety) +
                      ". Answer with only the line, inside <line></line>. No notes, no refusal.)";
            else if (why == kFailBusy)
                std::this_thread::sleep_for(std::chrono::milliseconds(std::min(400, std::max(0, left() - 600))));
            else
                break;
        }
        if (good) {
            via = name;
            if (st.readGender) {  // the speaker's own words showed their gender
                if (tlRaw.find("<g>f</g>") != std::string::npos) wordsGender = kGenderWoman;
                else if (tlRaw.find("<g>m</g>") != std::string::npos) wordsGender = kGenderMan;
            }
            return true;
        }
        if (err == "cancelled") return false;
        // The AI declined the line twice, and it has swearing in it: the AI translates the rest with
        // markers where the swear words were, and DeepL (or the free service) translates just those.
        if (lastWhy == kFailRejected) {
            std::vector<std::string> found;
            const std::string marked = markSwears(text, found);
            std::string masked;
            int why = kFailOther;
            const std::string ask2 = marked + "\n\n(The [[1]], [[2]]... markers stand for words that get filled in later. "
                                              "Keep every marker exactly as written, where it belongs in your sentence.)";
            if (!found.empty() && call(marked, ask2, std::max(1200, st.timeoutMs), masked, why)) {
                bool allThere = true;
                for (size_t i = 0; i < found.size(); ++i)
                    allThere = allThere && masked.find("[[" + std::to_string(i + 1) + "]]") != std::string::npos;
                if (allThere) {
                    for (size_t i = 0; i < found.size(); ++i) {
                        std::string word = found[i], e;
                        bool refused = false;
                        if (!st.deeplKey.empty() && deepl(http, st.deeplKey, found[i], from, to, st.deeplVariant, word, e, refused)) {}
                        else if (from && myMemory(http, found[i], *from, to, word, e)) {}
                        else word = found[i];
                        const std::string mark = "[[" + std::to_string(i + 1) + "]]";
                        for (size_t at = masked.find(mark); at != std::string::npos; at = masked.find(mark, at + word.size()))
                            masked.replace(at, mark.size(), word);
                    }
                    out = masked;
                    via = std::string(name) + " (swear words by " + (st.deeplKey.empty() ? "Free" : "DeepL") + ")";
                    return true;
                }
            }
        }
        if (!firstErr.empty() && firstErr != err && lowerCopy(err).find("timed out") == std::string::npos) err = firstErr;
        // DeepL and Free only know the standard language (or DeepL's few regional spellings).
        const std::string standardNote =
            st.varietyName.empty() || !st.deeplVariant.empty() ? std::string() : "; standard " + std::string(to.name);
        // Too slow or failed: this phrase goes through DeepL (or Free) so the conversation keeps going.
        const bool slow = lowerCopy(err).find("timed out") != std::string::npos || lowerCopy(err).find("timeout") != std::string::npos;
        const std::string why = slow ? std::string(name) + " too slow" : err;
        if (deeplOrFree(true)) {
            via += " (" + why + (via == "Free" && !st.varietyName.empty() ? "; standard " + std::string(to.name) : standardNote) + ")";
            return true;
        }
        err = why + "; fallback failed too: " + err;
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
        // DeepL doesn't translate this language (pair): use the free service for it instead.
        if (!from) {
            err = "DeepL doesn't translate " + std::string(to.name) +
                  ". Pick the spoken language so the free service can, or pick another language.";
            return false;
        }
        if (myMemory(http, text, *from, to, out, err)) { via = "Free (DeepL doesn't do " + std::string(to.name) + ")"; return true; }
        err = "DeepL doesn't translate this language, and the free service failed too: " + err;
        return false;
    }
    const bool ok = deeplOrFree(false);
    if (ok && !st.varietyName.empty()) via += " (standard " + std::string(to.name) + ")";
    return ok;
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
