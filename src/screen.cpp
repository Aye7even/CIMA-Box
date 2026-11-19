#include "screen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "translate.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#ifndef CIMA_NO_WINRT
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#endif
#endif

// ---------------------------------------------------------------- the window's picture

#ifdef _WIN32
namespace {

std::string exeName(DWORD pid) {
    std::string out;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return out;
    wchar_t path[MAX_PATH];
    DWORD n = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, path, &n)) {
        const wchar_t* slash = wcsrchr(path, L'\\');
        const wchar_t* name = slash ? slash + 1 : path;
        const int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
        if (len > 1) {
            out.resize(size_t(len - 1));
            WideCharToMultiByte(CP_UTF8, 0, name, -1, &out[0], len, nullptr, nullptr);
        }
    }
    CloseHandle(h);
    return out;
}

bool sameNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

struct Find { std::string exe; HWND best = nullptr; long area = 0; bool minimized = false; };

BOOL CALLBACK eachWindow(HWND w, LPARAM lp) {
    Find& f = *reinterpret_cast<Find*>(lp);
    if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (!sameNoCase(exeName(pid), f.exe)) return TRUE;
    if (IsIconic(w)) { f.minimized = true; return TRUE; }
    RECT r;
    GetClientRect(w, &r);
    const long area = long(r.right - r.left) * long(r.bottom - r.top);
    if (area > f.area) { f.area = area; f.best = w; }  // the program's main (biggest) window
    return TRUE;
}

// Bitmap -> RGBA, top row first.
bool readBitmap(HDC dc, HBITMAP bmp, int w, int h, Picture& out) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> bgra(size_t(w) * size_t(h) * 4);
    if (GetDIBits(dc, bmp, 0, UINT(h), bgra.data(), &bi, DIB_RGB_COLORS) != h) return false;
    out.width = w;
    out.height = h;
    out.rgba.resize(bgra.size());
    for (size_t i = 0; i < bgra.size(); i += 4) {
        out.rgba[i] = bgra[i + 2];
        out.rgba[i + 1] = bgra[i + 1];
        out.rgba[i + 2] = bgra[i];
        out.rgba[i + 3] = 255;
    }
    return true;
}

bool mostlyBlack(const Picture& p) {
    if (p.rgba.empty()) return true;
    uint64_t sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < p.rgba.size(); i += 4 * 97, ++n) sum += p.rgba[i] + p.rgba[i + 1] + p.rgba[i + 2];
    return n == 0 || sum / n < 6;
}

}  // namespace

bool captureWindow(const std::string& exe, Picture& out, std::string& err) {
    Find f;
    f.exe = exe;
    EnumWindows(eachWindow, reinterpret_cast<LPARAM>(&f));
    if (!f.best) {
        err = f.minimized ? "The window is minimized. Bring it back on screen to translate it."
                          : exe.empty() ? "Pick the window to translate." : exe + " isn't open.";
        return false;
    }
    RECT rc;
    GetClientRect(f.best, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w < 16 || h < 16) { err = "The window is too small to read."; return false; }
    HDC wdc = GetDC(f.best);
    HDC mem = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    // First ask the window to draw itself (works even when something covers it). Games that draw
    // straight to the screen come back black that way: then copy what's on screen where it is.
    bool ok = PrintWindow(f.best, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT) && readBitmap(mem, bmp, w, h, out) && !mostlyBlack(out);
    if (!ok) {
        POINT at = {0, 0};
        ClientToScreen(f.best, &at);
        HDC screen = GetDC(nullptr);
        ok = BitBlt(mem, 0, 0, w, h, screen, at.x, at.y, SRCCOPY | CAPTUREBLT) && readBitmap(mem, bmp, w, h, out);
        ReleaseDC(nullptr, screen);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(f.best, wdc);
    if (!ok) err = "Couldn't take a picture of " + exe + ".";
    return ok;
}
#else
bool captureWindow(const std::string&, Picture&, std::string& err) {
    err = "Screen translation needs Windows.";
    return false;
}
#endif

// ---------------------------------------------------------------- picture tools

Picture cropPicture(const Picture& p, const float area[4]) {
    const int x0 = std::max(0, std::min(p.width - 1, int(area[0] * p.width)));
    const int y0 = std::max(0, std::min(p.height - 1, int(area[1] * p.height)));
    const int x1 = std::max(x0 + 1, std::min(p.width, int(std::ceil(area[2] * p.width))));
    const int y1 = std::max(y0 + 1, std::min(p.height, int(std::ceil(area[3] * p.height))));
    Picture out;
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.rgba.resize(size_t(out.width) * size_t(out.height) * 4);
    for (int y = 0; y < out.height; ++y)
        std::memcpy(&out.rgba[size_t(y) * size_t(out.width) * 4], &p.rgba[(size_t(y0 + y) * size_t(p.width) + size_t(x0)) * 4],
                    size_t(out.width) * 4);
    return out;
}

Picture scaledDown(const Picture& p, int maxSide) {
    const int big = std::max(p.width, p.height);
    if (big <= maxSide || big == 0) return p;
    const float k = float(maxSide) / float(big);
    Picture out;
    out.width = std::max(1, int(p.width * k));
    out.height = std::max(1, int(p.height * k));
    out.rgba.resize(size_t(out.width) * size_t(out.height) * 4);
    // box filter: each output pixel averages the input pixels it covers
    for (int y = 0; y < out.height; ++y) {
        const int sy0 = int(y / k), sy1 = std::max(sy0 + 1, std::min(p.height, int((y + 1) / k)));
        for (int x = 0; x < out.width; ++x) {
            const int sx0 = int(x / k), sx1 = std::max(sx0 + 1, std::min(p.width, int((x + 1) / k)));
            unsigned sum[4] = {};
            unsigned n = 0;
            for (int yy = sy0; yy < sy1; ++yy)
                for (int xx = sx0; xx < sx1; ++xx, ++n)
                    for (int c = 0; c < 4; ++c) sum[c] += p.rgba[(size_t(yy) * size_t(p.width) + size_t(xx)) * 4 + size_t(c)];
            for (int c = 0; c < 4; ++c) out.rgba[(size_t(y) * size_t(out.width) + size_t(x)) * 4 + size_t(c)] = uint8_t(sum[c] / std::max(1u, n));
        }
    }
    return out;
}

namespace {
uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        made = true;
    }
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void put32(std::string& s, uint32_t v) {
    for (int i = 3; i >= 0; --i) s += char((v >> (8 * i)) & 0xFF);
}
void chunk(std::string& png, const char* type, const std::string& data) {
    put32(png, uint32_t(data.size()));
    const std::string td = std::string(type, 4) + data;
    png += td;
    put32(png, crc32(reinterpret_cast<const uint8_t*>(td.data()), td.size()) ^ 0xFFFFFFFFu);
}
}  // namespace

std::string encodePng(const Picture& p) {
    // Uncompressed ("stored") deflate: simple and quick; the picture is scaled down before this.
    std::string raw;
    raw.reserve(size_t(p.height) * (size_t(p.width) * 3 + 1));
    for (int y = 0; y < p.height; ++y) {
        raw += char(0);  // no filter
        const uint8_t* row = &p.rgba[size_t(y) * size_t(p.width) * 4];
        for (int x = 0; x < p.width; ++x) raw.append(reinterpret_cast<const char*>(row + x * 4), 3);
    }
    std::string z = "\x78\x01";
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    for (size_t at = 0; at < raw.size() || at == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z += char(last ? 1 : 0);
        z += char(n & 0xFF);
        z += char(n >> 8);
        z += char(~n & 0xFF);
        z += char((~n >> 8) & 0xFF);
        z.append(raw, at, n);
        at += n;
        if (last) break;
    }
    put32(z, (b << 16) | a);
    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    put32(ihdr, uint32_t(p.width));
    put32(ihdr, uint32_t(p.height));
    ihdr += std::string("\x08\x02\x00\x00\x00", 5);  // 8-bit RGB
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", "");
    return png;
}

std::string base64(const std::string& in) {
    static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) | uint8_t(in[i + 2]);
        out += k[v >> 18]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
    }
    if (i < in.size()) {
        const uint32_t v = (uint8_t(in[i]) << 16) | (i + 1 < in.size() ? uint8_t(in[i + 1]) << 8 : 0);
        out += k[v >> 18];
        out += k[(v >> 12) & 63];
        out += i + 1 < in.size() ? k[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// ---------------------------------------------------------------- Windows' text reader

#if defined(_WIN32) && !defined(CIMA_NO_WINRT)
namespace {
void ensureRuntime() {
    thread_local bool done = false;
    if (done) return;
    done = true;
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch (...) {}  // already set up: fine
}
}  // namespace

bool readText(const Picture& p, const std::string& lang, std::vector<std::string>& lines, std::string& err) {
    using namespace winrt::Windows::Media::Ocr;
    using namespace winrt::Windows::Graphics::Imaging;
    lines.clear();
    ensureRuntime();
    try {
        OcrEngine engine{nullptr};
        if (lang.empty()) {
            engine = OcrEngine::TryCreateFromUserProfileLanguages();
        } else {
            for (const auto& l : OcrEngine::AvailableRecognizerLanguages()) {  // "ja", "en-US", "zh-Hans-CN"...
                const std::string tag = winrt::to_string(l.LanguageTag());
                if (tag == lang || tag.rfind(lang + "-", 0) == 0) { engine = OcrEngine::TryCreateFromLanguage(l); break; }
            }
            if (!engine) {
                err = "Windows can't read this language yet. Add it in Windows Settings > Time & language > Language "
                      "(with its text recognition), or use AI reading.";
                return false;
            }
        }
        if (!engine) { err = "Windows' text reader isn't available on this PC. Use AI reading instead."; return false; }
        const Picture q = scaledDown(p, int(OcrEngine::MaxImageDimension()));
        std::vector<uint8_t> bgra(q.rgba.size());
        for (size_t i = 0; i < q.rgba.size(); i += 4) {  // RGBA -> BGRA
            bgra[i] = q.rgba[i + 2];
            bgra[i + 1] = q.rgba[i + 1];
            bgra[i + 2] = q.rgba[i];
            bgra[i + 3] = 255;
        }
        const auto buf = winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
            winrt::array_view<const uint8_t>(bgra.data(), bgra.data() + bgra.size()));
        const SoftwareBitmap bmp = SoftwareBitmap::CreateCopyFromBuffer(buf, BitmapPixelFormat::Bgra8, q.width, q.height, BitmapAlphaMode::Premultiplied);
        const OcrResult r = engine.RecognizeAsync(bmp).get();
        for (const auto& line : r.Lines()) lines.push_back(winrt::to_string(line.Text()));
        return true;
    } catch (const winrt::hresult_error& e) {
        err = "Windows' text reader: " + winrt::to_string(e.message());
        return false;
    } catch (...) {
        err = "Windows' text reader didn't work.";
        return false;
    }
}

std::vector<std::string> readerLanguages() {
    std::vector<std::string> out;
    ensureRuntime();
    try {
        for (const auto& l : winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages())
            out.push_back(winrt::to_string(l.LanguageTag()));
    } catch (...) {}
    return out;
}
#else
bool readText(const Picture&, const std::string&, std::vector<std::string>& lines, std::string& err) {
    lines.clear();
#ifdef _WIN32
    err = "This copy was built without Windows' text reader. Use AI reading instead.";
#else
    err = "Windows' text reader needs Windows. Use AI reading instead.";
#endif
    return false;
}
std::vector<std::string> readerLanguages() { return {}; }
#endif

// ---------------------------------------------------------------- the rest

int pictureTokens(int service, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    if (service == kClaude) {  // about width x height / 750, after Claude shrinks it to 1568 on the long side
        const float k = std::min(1.f, 1568.f / float(std::max(w, h)));
        return int(float(w) * k * float(h) * k / 750.f) + 1;
    }
    if (service == kGemini) {  // 258 per 768-pixel tile
        if (w <= 384 && h <= 384) return 258;
        return int(std::ceil(w / 768.0) * std::ceil(h / 768.0)) * 258;
    }
    // OpenAI: fit in 2048, short side 768, then 170 per 512-pixel tile plus 85
    float fw = float(w), fh = float(h);
    const float k1 = std::min(1.f, 2048.f / std::max(fw, fh));
    fw *= k1; fh *= k1;
    const float k2 = std::min(1.f, 768.f / std::min(fw, fh));
    fw *= k2; fh *= k2;
    return 85 + 170 * int(std::ceil(fw / 512.f) * std::ceil(fh / 512.f));
}

bool sameLine(const std::string& a, const std::string& b) {
    auto norm = [](const std::string& s) {
        std::string o;
        for (unsigned char c : s) {
            if (c < 0x80 && !std::isalnum(c)) continue;  // spaces and punctuation are read differently each time
            o += char(c < 0x80 ? std::tolower(c) : c);
        }
        return o;
    };
    const std::string x = norm(a), y = norm(b);
    if (x == y) return true;
    if (x.empty() || y.empty()) return false;
    const size_t allowed = std::max<size_t>(1, std::max(x.size(), y.size()) / 8);
    if ((x.size() > y.size() ? x.size() - y.size() : y.size() - x.size()) > allowed) return false;
    std::vector<size_t> prev(y.size() + 1), cur(y.size() + 1);
    for (size_t j = 0; j <= y.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= x.size(); ++i) {
        cur[0] = i;
        size_t best = cur[0];
        for (size_t j = 1; j <= y.size(); ++j) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1)});
            best = std::min(best, cur[j]);
        }
        if (best > allowed) return false;
        std::swap(prev, cur);
    }
    return prev[y.size()] <= allowed;
}
