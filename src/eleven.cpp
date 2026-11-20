#include "eleven.h"
#include <chrono>
#include <future>
#include "core.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

const char* kBase = "https://api.elevenlabs.io";

std::string apiError(long code, const std::string& body) {
    std::string msg;
    json j = json::parse(body, nullptr, false);
    if (j.is_object() && j.contains("detail")) {
        const json& d = j["detail"];
        if (d.is_string()) msg = d.get<std::string>();
        else if (d.is_object() && d.contains("message") && d["message"].is_string()) msg = d["message"].get<std::string>();
        else msg = d.dump();
    } else {
        msg = body.substr(0, 300);
    }
    return "ElevenLabs " + std::to_string(code) + ": " + msg;
}

bool contains(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

// Runs one request; true only for HTTP 200.
std::string trimmed(const std::string& s);

bool call(Eleven& api, HttpRequest& r, std::string& body, std::string& err, long* statusOut = nullptr) {
    const std::string key = trimmed(api.apiKey);  // a pasted key often brings a space or line break along
    if (key.empty()) { err = "Add your ElevenLabs API key first"; return false; }
    r.headers.push_back("xi-api-key: " + key);
    long status = 0;
    if (statusOut) *statusOut = 0;
    if (!api.http.perform(r, status, body, err)) return false;
    if (statusOut) *statusOut = status;
    if (status != 200) { err = apiError(status, body); return false; }
    return true;
}

std::string str(const json& j, const char* key) {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

std::string trimmed(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws), b = s.find_last_not_of(ws);
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

void Eleven::warm() {
    if (apiKey.empty()) return;
    // Both connections at once. One that doesn't answer in 3 s is dropped, and the real request
    // opens a new one instead of waiting on it.
    auto one = [this](Http& h) {
        if (h.idleSeconds() < 15.0) return;  // connection is still fresh
        HttpRequest r;
        r.url = std::string(kBase) + "/v1/models";
        r.headers.push_back("xi-api-key: " + apiKey);
        r.headOnly = true;
        r.timeoutSec = 3;
        long status = 0;
        std::string body, err;
        h.perform(r, status, body, err);  // the result doesn't matter, only the open connection
    };
    auto spare = std::async(std::launch::async, [&] { one(spare_); });
    one(http);
    spare.get();
}

bool Eleven::listVoices(std::vector<Voice>& out, std::string& err) {
    HttpRequest r;
    r.url = std::string(kBase) + "/v1/voices";
    r.timeoutSec = 20;
    std::string body;
    if (!call(*this, r, body, err)) return false;
    json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("voices") || !j["voices"].is_array()) { err = "Unexpected /v1/voices response"; return false; }
    out.clear();
    for (const json& v : j["voices"]) {  // read leniently: one odd voice mustn't stop the whole list
        Voice x;
        x.id = str(v, "voice_id");
        x.name = str(v, "name");
        if (x.name.empty()) x.name = x.id;
        x.category = str(v, "category");
        x.previewUrl = str(v, "preview_url");
        if (!x.id.empty()) out.push_back(std::move(x));
    }
    return true;
}

bool Eleven::listModels(std::vector<Model>& out, std::string& err) {
    HttpRequest r;
    r.url = std::string(kBase) + "/v1/models";
    r.timeoutSec = 20;
    std::string body;
    if (!call(*this, r, body, err)) return false;
    json j = json::parse(body, nullptr, false);
    if (!j.is_array()) { err = "Unexpected /v1/models response"; return false; }
    out.clear();
    auto flag = [](const json& m, const char* k) { return m.contains(k) && m[k].is_boolean() && m[k].get<bool>(); };
    for (const json& m : j) {
        Model x;
        x.id = str(m, "model_id");
        x.name = str(m, "name");
        if (x.name.empty()) x.name = x.id;
        x.voiceConversion = flag(m, "can_do_voice_conversion");
        x.textToSpeech = flag(m, "can_do_text_to_speech");
        if (!x.id.empty()) out.push_back(std::move(x));
    }
    return true;
}

bool Eleven::speechToSpeech(const std::string& voiceId, const std::string& modelId, const VoiceSettings& vs,
                            bool removeNoise, const std::vector<int16_t>& pcm, const AudioSink& sink, std::string& err,
                            const std::string& outputFormat, const std::vector<int16_t>* fullPcm, int fullRate) {
    json settings = {{"stability", vs.stability}, {"similarity_boost", vs.similarity},
                     {"style", vs.style}, {"use_speaker_boost", vs.speakerBoost}};
    std::vector<HttpPart> parts;
    if (fullPcm && fullRate > 0 && !fullPcm->empty()) {
        // Full-quality WAV: three times the upload of 16 kHz, but nothing of your voice is cut off.
        parts.push_back({"audio", makeWav(*fullPcm, fullRate), "audio.wav", "audio/wav"});
    } else {
        std::string raw(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(int16_t));  // little-endian hosts
        parts.push_back({"audio", std::move(raw), "audio.pcm", "application/octet-stream"});
        parts.push_back({"file_format", "pcm_s16le_16", "", ""});  // raw 16 kHz mono input = smallest upload
    }
    parts.push_back({"model_id", modelId, "", ""});
    parts.push_back({"voice_settings", settings.dump(), "", ""});
    parts.push_back({"remove_background_noise", removeNoise ? "true" : "false", "", ""});
    HttpRequest r;
    r.url = std::string(kBase) + "/v1/speech-to-speech/" + voiceId + "/stream?output_format=" + outputFormat;
    r.parts = &parts;
    r.sink = &sink;
    std::string body;
    return call(*this, r, body, err);
}

bool Eleven::transcribe(const std::vector<int16_t>& pcm, const std::string& languageCode, std::string& text,
                        std::string& detectedLanguage, std::string& err, std::vector<HeardToken>* tokens) {
    // A clip that stops dead on the last syllable tends to lose its final word; give it room to end.
    std::vector<int16_t> padded(pcm);
    padded.resize(padded.size() + 16000 * 3 / 10, 0);
    const std::string key = trimmed(apiKey);
    if (key.empty()) { err = "Add your ElevenLabs API key first"; return false; }
    std::string body;
    for (int attempt = 0;; ++attempt) {
        std::vector<HttpPart> parts;
        if (rawPcmUpload_) {
            // Raw 16 kHz mono PCM: ElevenLabs answers sooner than for a WAV file (no decoding step).
            std::string raw(reinterpret_cast<const char*>(padded.data()), padded.size() * sizeof(int16_t));
            parts.push_back({"file", std::move(raw), "audio.pcm", "application/octet-stream"});
            parts.push_back({"file_format", "pcm_s16le_16", "", ""});
        } else {
            parts.push_back({"file", makeWav(padded, 16000), "audio.wav", "audio/wav"});
        }
        parts.push_back({"model_id", "scribe_v2", "", ""});
        parts.push_back({"tag_audio_events", tokens ? "true" : "false", "", ""});
        if (!languageCode.empty()) parts.push_back({"language_code", languageCode, "", ""});
        HttpRequest r;
        r.url = std::string(kBase) + "/v1/speech-to-text";
        r.parts = &parts;
        r.headers.push_back("xi-api-key: " + key);
        r.timeoutSec = 20;  // a phrase is a few seconds of audio: anything near this is stuck, not working
        // If the answer is slow once the audio is sent, a second copy goes out on the other connection
        // and the first answer is used. Same audio, same settings: only the waiting changes.
        const int wait = sttWaitMs_ > 0 ? std::max(900, std::min(3000, sttWaitMs_ * 2 + 300)) : 1500;
        std::string bodies[2], errs[2];
        long statuses[2] = {0, 0};
        int answered[2] = {0, 0};
        const int winner = sendWithSpare(
            http, spare_, wait,
            [&](Http& h, int i) {
                const bool ok = h.perform(r, statuses[i], bodies[i], errs[i]) && statuses[i] == 200;
                const long long sent = h.sentAtMs.load();
                if (ok && sent > 0)
                    answered[i] = int(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()).count() - sent);
                return ok;
            },
            // A refusal (bad key, no credits, a setting it doesn't take) would be refused again.
            [&] { return !(statuses[0] >= 400 && statuses[0] < 500 && statuses[0] != 429); });
        if (winner >= 0) {
            body.swap(bodies[winner]);
            if (answered[winner] > 0) {
                const int took = std::min(answered[winner], 4000);
                sttWaitMs_ = sttWaitMs_ > 0 ? (sttWaitMs_ * 3 + took) / 4 : took;
            }
            break;
        }
        if (attempt == 0 && rawPcmUpload_ && (statuses[0] == 400 || statuses[0] == 422)) {
            rawPcmUpload_ = false;  // not accepted for this account or model: use a WAV file from now on
            continue;
        }
        err = statuses[0] != 0 && statuses[0] != 200 ? apiError(statuses[0], bodies[0]) : errs[0];
        return false;
    }
    json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("text") || !j["text"].is_string()) { err = "Unexpected speech-to-text response"; return false; }
    text = j["text"].get<std::string>();
    detectedLanguage = str(j, "language_code");
    if (tokens) {
        tokens->clear();
        if (j.contains("words") && j["words"].is_array()) {
            std::string words;
            for (const json& w : j["words"]) {
                const std::string type = str(w, "type"), t = str(w, "text");
                if (t.empty()) continue;
                HeardToken tk;
                tk.text = t;
                tk.kind = type == "audio_event" ? kTokSound : type == "spacing" ? kTokSpace : kTokWord;
                if (w.contains("start") && w["start"].is_number()) tk.start = w["start"].get<float>();
                if (w.contains("end") && w["end"].is_number()) tk.end = w["end"].get<float>();
                if (tk.kind != kTokSound) words += t;
                tokens->push_back(std::move(tk));
            }
            std::string single;  // a removed sound leaves two spaces behind
            for (char c : words)
                if (!(c == ' ' && !single.empty() && single.back() == ' ')) single += c;
            text = trimmed(single);  // the plain "text" field has the sounds written into it
        } else {
            tokens->push_back({text, kTokWord, -1.f, -1.f});
        }
    }
    return true;
}

bool Eleven::transcribeTurns(const std::vector<int16_t>& pcm, const std::string& languageCode, bool tellSpeakersApart,
                             Transcript& out, std::string& err, bool events, bool labels) {
    out = Transcript();
    std::vector<int16_t> padded(pcm);
    padded.resize(padded.size() + 16000 * 3 / 10, 0);  // room for the last word to end

    std::string body;
    for (int attempt = 0;; ++attempt) {
        std::vector<HttpPart> parts;
        if (rawPcmUpload_) {
            // Raw 16 kHz mono PCM skips the decoding step on their side.
            std::string raw(reinterpret_cast<const char*>(padded.data()), padded.size() * sizeof(int16_t));
            parts.push_back({"file", std::move(raw), "audio.pcm", "application/octet-stream"});
            parts.push_back({"file_format", "pcm_s16le_16", "", ""});
        } else {
            parts.push_back({"file", makeWav(padded, 16000), "audio.wav", "audio/wav"});
        }
        parts.push_back({"model_id", "scribe_v2", "", ""});
        parts.push_back({"tag_audio_events", events ? "true" : "false", "", ""});
        parts.push_back({"diarize", tellSpeakersApart ? "true" : "false", "", ""});
        if (!languageCode.empty()) parts.push_back({"language_code", languageCode, "", ""});
        HttpRequest r;
        r.url = std::string(kBase) + "/v1/speech-to-text";
        r.parts = &parts;
        r.timeoutSec = 30;
        long status = 0;
        if (call(*this, r, body, err, &status)) break;
        if (attempt == 0 && rawPcmUpload_ && (status == 400 || status == 422)) {
            rawPcmUpload_ = false;  // not accepted for this account or model: use a WAV file from now on
            continue;
        }
        return false;
    }

    json j = json::parse(body, nullptr, false);
    if (!j.is_object() || !j.contains("text") || !j["text"].is_string()) { err = "Unexpected speech-to-text response"; return false; }
    out.text = trimmed(j["text"].get<std::string>());
    out.language = str(j, "language_code");
    if (j.contains("language_probability") && j["language_probability"].is_number())
        out.languageSure = j["language_probability"].get<float>();
    // Sounds: "(laughter)" -> "laughter". Without labels they're taken out of the text again.
    auto eventName = [](std::string t) {
        while (!t.empty() && (t.front() == '(' || t.front() == '[' || t.front() == ' ')) t.erase(t.begin());
        while (!t.empty() && (t.back() == ')' || t.back() == ']' || t.back() == ' ')) t.pop_back();
        return t;
    };
    if (j.contains("words") && j["words"].is_array()) {
        std::string clean;
        for (const json& w : j["words"]) {
            const std::string type = str(w, "type"), text = str(w, "text");
            if (type == "audio_event") {
                out.events.push_back(eventName(text));
                if (labels) clean += "[" + eventName(text) + "]";
                continue;
            }
            if (type == "word") ++out.words;
            clean += text;
        }
        if (events) out.text = trimmed(clean);
    }

    if (tellSpeakersApart && j.contains("words") && j["words"].is_array()) {
        std::vector<std::string> ids;  // speaker ids in the order they first speak
        for (const json& w : j["words"]) {
            const std::string type = str(w, "type");
            std::string text = str(w, "text");
            if (type == "audio_event") {
                if (!labels || out.turns.empty()) continue;
                out.turns.back().text += " [" + eventName(text) + "] ";
                continue;
            }
            if (text.empty()) continue;
            if (type == "spacing") {
                if (!out.turns.empty()) out.turns.back().text += text;
                continue;
            }
            const std::string id = str(w, "speaker_id");
            int speaker = 0;
            while (speaker < int(ids.size()) && ids[size_t(speaker)] != id) ++speaker;
            if (speaker == int(ids.size())) ids.push_back(id);
            if (out.turns.empty() || out.turns.back().speaker != speaker) out.turns.push_back({speaker, std::string()});
            SpeakerTurn& turn = out.turns.back();
            turn.text += text;
            const float ws = w.contains("start") && w["start"].is_number() ? w["start"].get<float>() : -1.f;
            const float we = w.contains("end") && w["end"].is_number() ? w["end"].get<float>() : -1.f;
            if (ws >= 0 && (turn.start < 0 || ws < turn.start)) turn.start = ws;
            if (we > turn.end) turn.end = we;
        }
        for (SpeakerTurn& t : out.turns) t.text = trimmed(t.text);
        for (size_t i = out.turns.size(); i-- > 0;)
            if (out.turns[i].text.empty()) out.turns.erase(out.turns.begin() + long(i));
        out.speakers = std::max<int>(1, int(ids.size()));
    }
    if (out.turns.empty() && !out.text.empty()) {
        out.turns.push_back({0, out.text});
        out.speakers = 1;
    }
    return true;
}

bool Eleven::textToSpeech(const std::string& voiceId, const std::string& modelId, const VoiceSettings& vs,
                          const std::string& text, const std::string& languageCode, const AudioSink& sink,
                          std::string& err, const std::string& outputFormat) {
    json settings;
    if (contains(modelId, "v4")) {
        // v4 / v4 Turbo expose only stability and similarity.
        settings = {{"stability", vs.stability}, {"similarity_boost", vs.similarity}};
        // Only v4 takes clone strength (others refuse the request), and only professional clones use it.
        if (vs.setCloneStrength) settings["clone_strength"] = std::max(0.f, std::min(1.f, vs.cloneStrength));
    } else {
        float stability = vs.stability;
        if (contains(modelId, "v3")) stability = std::round(stability * 2.f) / 2.f;  // v3 takes 0 / 0.5 / 1 only
        settings = {{"stability", stability}, {"similarity_boost", vs.similarity}, {"style", vs.style},
                    {"use_speaker_boost", vs.speakerBoost}, {"speed", vs.speed}};
    }
    json req = {{"text", text}, {"model_id", modelId}, {"voice_settings", settings}};
    // Forcing the language helps the v2.5 models pronounce translated text; other models infer it.
    // Tells the model which language the text is in (better pronunciation). Flash/Turbo v2.5 and the
    // v4 family take it; other models reject it.
    if (!languageCode.empty() &&
        (contains(modelId, "flash_v2_5") || contains(modelId, "turbo_v2_5") || contains(modelId, "eleven_v4")))
        req["language_code"] = languageCode;
    const std::string payload = req.dump();
    HttpRequest r;
    r.url = std::string(kBase) + "/v1/text-to-speech/" + voiceId + "/stream?output_format=" + outputFormat;
    r.headers.push_back("Content-Type: application/json");
    r.body = &payload;
    r.sink = &sink;
    std::string body;
    return call(*this, r, body, err);
}
